use std::{
    io::Write,
    net::{IpAddr, SocketAddr},
    time::Instant,
};

use mio::{
    net::{TcpStream, UdpSocket},
    Poll, Token,
};

use crate::{
    config::OPTIONS,
    proto::{CONNECT, Sock5Address, TrojanRequest},
    resolver::DnsResolver,
    server::{
        CHANNEL_BACKEND,
        CHANNEL_CNT,
        CHANNEL_PROXY,
        tcp_backend::TcpBackend, tls_server::{Backend, PollEvent}, udp_backend::UdpBackend,
    },
    status::StatusProvider,
    tls_conn::TlsConn,
};

enum Status {
    HandShake,
    DnsWait,
    TCPForward,
    UDPForward,
}

pub struct Connection {
    index: usize,
    proxy: TlsConn,
    status: Status,
    sock5_addr: Sock5Address,
    command: u8,
    last_active_time: Instant,
    backend: Option<Box<dyn Backend>>,
    target_addr: Option<SocketAddr>,
    data: Vec<u8>,
    read_backend: bool,
    read_proxy: bool,
}

impl Connection {
    pub fn new(index: usize, proxy: TlsConn) -> Connection {
        Connection {
            index,
            proxy,
            status: Status::HandShake,
            command: 0,
            sock5_addr: Sock5Address::None,
            last_active_time: Instant::now(),
            backend: None,
            target_addr: None,
            data: Vec::new(),
            read_proxy: false,
            read_backend: false,
        }
    }

    pub fn destroy(&mut self, poll: &Poll) {
        self.proxy.shutdown();
        self.proxy.check_status(poll);
        if let Some(backend) = &mut self.backend {
            backend.shutdown();
            backend.check_status(poll);
        }
    }

    pub fn timeout(&self, recent_active_time: Instant) -> bool {
        if let Some(backend) = &self.backend {
            backend.timeout(self.last_active_time, recent_active_time)
        } else {
            self.last_active_time.elapsed().as_secs() > OPTIONS.tcp_idle_timeout
        }
    }

    fn proxy_token(&self, token: Token) -> bool {
        token.0 % CHANNEL_CNT == CHANNEL_PROXY
    }

    pub fn ready(&mut self, poll: &Poll, event: PollEvent, resolver: Option<&mut DnsResolver>) {
        self.last_active_time = Instant::now();

        match event {
            PollEvent::Network(event) => {
                if self.proxy_token(event.token()) {
                    if event.is_readable() {
                        let writable = if let Some(backend) = self.backend.as_ref() {
                            backend.writable()
                        } else {
                            true
                        };
                        if writable {
                            self.try_read_proxy(poll, resolver);
                        } else {
                            log::trace!(
                                "backend connection:{} is not writable, stop reading from proxy",
                                self.index
                            );
                            self.read_proxy = true;
                        }
                    }
                    if event.is_writable() {
                        self.proxy.established();
                        self.try_send_proxy();
                        if self.proxy.writable() && self.read_backend {
                            if let Some(backend) = self.backend.as_mut() {
                                backend.do_read(&mut self.proxy);
                            }
                            log::trace!(
                                "proxy connection:{} is writable, restore reading from backend",
                                self.index
                            );
                            self.read_backend = false;
                        }
                    }
                } else {
                    match self.status {
                        Status::UDPForward | Status::TCPForward => {
                            if let Some(backend) = self.backend.as_mut() {
                                if event.is_readable() {
                                    if self.proxy.writable() {
                                        backend.do_read(&mut self.proxy);
                                    } else {
                                        log::trace!("proxy connection:{} is not writable, stop reading from backend", self.index);
                                        self.read_backend = true;
                                    }
                                }
                                if event.is_writable() {
                                    backend.dispatch(&[]);
                                    if backend.writable() && self.read_proxy {
                                        log::trace!("backend connection:{} is writable, restore reading from proxy", self.index);
                                        self.try_read_proxy(poll, resolver);
                                        self.read_proxy = false;
                                    }
                                }
                            } else {
                                log::error!("connection:{} has invalid status", self.index);
                            }
                        }
                        _ => {}
                    }
                }
            }
            PollEvent::Dns((_, ip)) => self.try_resolve(poll, ip),
        }

        if let Some(backend) = &mut self.backend {
            if self.proxy.is_shutdown() {
                backend.peer_closed();
            }
            if backend.is_shutdown() {
                self.proxy.peer_closed();
            }
        }
        self.proxy.check_status(poll);
        if let Some(backend) = &mut self.backend {
            backend.check_status(poll);
        }
    }

    pub fn try_resolve(&mut self, poll: &Poll, ip: Option<IpAddr>) {
        if let Status::DnsWait = self.status {
            if let Sock5Address::Domain(domain, port) = &self.sock5_addr {
                if let Some(address) = ip {
                    log::debug!(
                        "connection:{} got resolve result {} = {}",
                        self.index,
                        domain,
                        address
                    );
                    let addr = SocketAddr::new(address, *port);
                    self.target_addr.replace(addr);
                    self.dispatch(&[], poll, None);
                } else {
                    log::error!("connection:{} resolve host:{} failed", self.index, domain);
                    self.proxy.shutdown();
                }
            } else {
                log::error!("connection:{} got bug, not a resolver status", self.index);
            }
        } else {
            log::error!(
                "connection:{} status is not DnsWait, but received dns event",
                self.index
            );
        }
    }

    fn try_send_proxy(&mut self) {
        self.proxy.do_send();
    }

    fn try_read_proxy(&mut self, poll: &Poll, resolver: Option<&mut DnsResolver>) {
        if let Some(buffer) = self.proxy.do_read() {
            self.dispatch(buffer.as_slice(), poll, resolver);
        }
    }

    fn try_handshake(&mut self, buffer: &mut &[u8], resolver: &mut &mut DnsResolver) -> bool {
        if let Some(request) = TrojanRequest::parse(buffer) {
            self.command = request.command;
            self.sock5_addr = request.address;
            *buffer = request.payload;
        } else {
            log::debug!(
                "connection:{} does not get a trojan request, pass through",
                self.index
            );
            self.command = CONNECT;
            self.sock5_addr = Sock5Address::None;
        }
        match &self.sock5_addr {
            Sock5Address::Domain(domain, port) => {
                if self.command != CONNECT {
                    //udp associate bind at 0.0.0.0:0, ignore all domain
                    return true;
                }
                log::debug!("connection:{} has to resolve {}", self.index, domain);
                if let Some(ip) = (*resolver).query_dns(domain.as_str()) {
                    self.target_addr.replace(SocketAddr::new(ip, *port));
                } else {
                    resolver.resolve(domain.clone(), Some(self.target_token()));
                }
            }
            Sock5Address::Socket(address) => {
                log::debug!(
                    "connection:{} got resolved target address:{}",
                    self.index,
                    address
                );
                self.target_addr.replace(*address);
            }
            Sock5Address::None => {
                log::debug!(
                    "connection:{} got default target address:{}",
                    self.index,
                    OPTIONS.back_addr.as_ref().unwrap()
                );
                self.target_addr = OPTIONS.back_addr;
            }
            _ => {
                unreachable!()
            }
        }
        true
    }

    fn dispatch(&mut self, mut buffer: &[u8], poll: &Poll, mut resolver: Option<&mut DnsResolver>) {
        log::debug!(
            "connection:{} dispatch {} bytes request data",
            self.index,
            buffer.len()
        );
        loop {
            match self.status {
                Status::HandShake => {
                    if self.try_handshake(&mut buffer, resolver.as_mut().unwrap()) {
                        self.status = Status::DnsWait;
                        continue;
                    }
                }
                Status::DnsWait => {
                    if self.command == CONNECT {
                        //if dns query is not done, cache data now
                        if let Err(err) = self.data.write(buffer) {
                            log::warn!("connection:{} cache data failed {}", self.index, err);
                            self.proxy.shutdown();
                        } else if self.target_addr.is_none() {
                            log::warn!("connection:{} dns query not done yet", self.index);
                        } else if self.try_setup_tcp_target(poll) {
                            buffer = &[];
                            self.status = Status::TCPForward;
                            continue;
                        }
                    } else if self.try_setup_udp_target(poll) {
                        self.status = Status::UDPForward;
                        continue;
                    }
                }
                _ => {
                    if let Some(backend) = self.backend.as_mut() {
                        backend.dispatch(buffer);
                    } else {
                        log::error!("connection:{} has no backend yet", self.index);
                    }
                }
            }
            break;
        }
    }

    fn try_setup_tcp_target(&mut self, poll: &Poll) -> bool {
        log::debug!(
            "connection:{} make a target connection to {}",
            self.index,
            self.target_addr.unwrap()
        );
        match TcpStream::connect(self.target_addr.unwrap()) {
            Ok(tcp_target) => {
                match TcpBackend::new(tcp_target, self.index, self.target_token(), poll) {
                    Ok(mut backend) => {
                        if !self.data.is_empty() {
                            backend.dispatch(self.data.as_slice());
                            self.data.clear();
                            self.data.shrink_to_fit();
                        }
                        self.backend.replace(Box::new(backend));
                    }
                    Err(err) => {
                        log::error!("connection:{} setup backend failed:{:?}", self.index, err);
                        self.proxy.shutdown();
                    }
                }
            }
            Err(err) => {
                log::warn!("connection:{} connect to target failed:{}", self.index, err);
                self.proxy.shutdown();
                return false;
            }
        }
        true
    }

    fn try_setup_udp_target(&mut self, poll: &Poll) -> bool {
        log::debug!("connection:{} got udp connection", self.index);
        match UdpSocket::bind(OPTIONS.empty_addr.unwrap()) {
            Err(err) => {
                log::error!("connection:{} bind udp socket failed:{}", self.index, err);
                self.proxy.shutdown();
                return false;
            }
            Ok(udp_target) => {
                match UdpBackend::new(udp_target, self.index, self.target_token(), poll) {
                    Ok(backend) => {
                        self.backend.replace(Box::new(backend));
                    }
                    Err(err) => {
                        log::error!("connection:{} setup backend failed:{:?}", self.index, err);
                        self.proxy.shutdown();
                    }
                }
            }
        }
        true
    }

    pub fn destroyed(&self) -> bool {
        if let Some(backend) = &self.backend {
            self.proxy.deregistered() && backend.deregistered()
        } else {
            self.proxy.deregistered()
        }
    }

    fn target_token(&self) -> Token {
        Token((self.index * CHANNEL_CNT) + CHANNEL_BACKEND)
    }
}
