use std::{
    collections::{HashMap, HashSet},
    fs::File,
    io::{BufRead, BufReader, ErrorKind},
    net::{IpAddr, SocketAddr},
    str::FromStr,
    time::{Duration, Instant},
};

use itertools::Itertools;
use mio::{event::Event, Interest, net::UdpSocket, Poll, Token};
use trust_dns_proto::{
    op::{Message, MessageType, Query, ResponseCode},
    rr::{DNSClass, Name, RData, Record, RecordType},
    serialize::binary::BinDecodable,
};

use crate::{
    dns::{DNS_LOCAL, DNS_POISONED, DNS_TRUSTED, domain::DomainMap},
    OPTIONS,
    proto::MAX_PACKET_SIZE,
    wintun::route_add_with_if,
};

pub struct DnsServer {
    listener: UdpSocket,
    trusted: UdpSocket,
    poisoned: UdpSocket,
    buffer: Vec<u8>,
    arp_data: Vec<u8>,
    blocked_domains: DomainMap,
    store: HashMap<String, QueryResult>,
    ptr_name: String,
    trusted_addr: SocketAddr,
    poisoned_addr: SocketAddr,
    route_added: HashSet<u32>,
    adapter_index: u32,
}

struct QueryResult {
    addresses: Vec<(SocketAddr, u16)>,
    response: Option<Message>,
    expire_time: Instant,
}

impl DnsServer {
    pub fn new(index: u32) -> Self {
        let default_addr = "0.0.0.0:0".to_owned();
        let trusted_dns_addr = OPTIONS.dns_args().trusted_dns.clone() + ":53";
        let poisoned_dns_addr = OPTIONS.dns_args().poisoned_dns.clone() + ":53";
        let trusted_addr = trusted_dns_addr.as_str().parse().unwrap();
        let poisoned_addr = poisoned_dns_addr.as_str().parse().unwrap();

        Self {
            trusted_addr,
            poisoned_addr,
            listener: UdpSocket::bind(
                OPTIONS
                    .dns_args()
                    .dns_listen_address
                    .as_str()
                    .parse()
                    .unwrap(),
            )
                .unwrap(),
            trusted: UdpSocket::bind(default_addr.as_str().parse().unwrap()).unwrap(),
            poisoned: UdpSocket::bind(default_addr.as_str().parse().unwrap()).unwrap(),
            buffer: vec![0; MAX_PACKET_SIZE],
            blocked_domains: DomainMap::new(),
            arp_data: vec![],
            store: HashMap::new(),
            ptr_name: String::new(),
            route_added: HashSet::new(),
            adapter_index: index,
        }
    }

    pub fn name_server(&self) -> String {
        self.listener.local_addr().unwrap().ip().to_string()
    }

    pub fn setup(&mut self, poll: &Poll) {
        poll.registry()
            .register(&mut self.trusted, Token(DNS_TRUSTED), Interest::READABLE)
            .unwrap();
        poll.registry()
            .register(&mut self.poisoned, Token(DNS_POISONED), Interest::READABLE)
            .unwrap();
        poll.registry()
            .register(&mut self.listener, Token(DNS_LOCAL), Interest::READABLE)
            .unwrap();

        let mut domain_map = DomainMap::new();
        let file = File::open(OPTIONS.dns_args().blocked_domain_list.as_str()).unwrap();
        let reader = BufReader::new(file);
        reader.lines().for_each(|line| {
            if let Ok(line) = line {
                domain_map.add_domain(line.as_str());
            }
        });
        self.blocked_domains = domain_map;

        let mut message = Message::new();
        message.set_message_type(MessageType::Response);
        message.set_id(1);
        message.set_recursion_desired(true);
        message.set_recursion_available(true);
        message.set_response_code(ResponseCode::NoError);
        let mut query = Query::new();
        let address: String = self
            .listener
            .local_addr()
            .unwrap()
            .ip()
            .to_string()
            .split('.')
            .rev()
            .join(".");
        let name = address + ".in-addr.arpa.";
        self.ptr_name = name.clone();
        let name = Name::from_str(name.as_str()).unwrap();
        query.set_name(name.clone());
        query.set_query_type(RecordType::PTR);
        query.set_query_class(DNSClass::IN);
        message.add_query(query);
        let mut record = Record::new();
        record.set_name(name);
        record.set_record_type(RecordType::PTR);
        record.set_dns_class(DNSClass::IN);
        record.set_ttl(20567);
        record.set_data(Some(RData::PTR(Name::from_str("trojan.dns").unwrap())));
        message.add_answer(record);
        self.arp_data = message.to_vec().unwrap();
    }

    pub fn ready(&mut self, event: &Event, poll: &Poll) {
        match event.token() {
            Token(DNS_LOCAL) => {
                self.dispatch_local(poll);
            }
            Token(DNS_TRUSTED) => {
                self.dispatch_trusted(poll);
            }
            Token(DNS_POISONED) => {
                self.dispatch_poisoned(poll);
            }
            _ => unreachable!(),
        }
    }

    fn dispatch_local(&mut self, poll: &Poll) {
        let now = Instant::now();
        loop {
            match self.listener.recv_from(self.buffer.as_mut_slice()) {
                Ok((length, from)) => {
                    let data = &self.buffer.as_slice()[..length];
                    if let Ok(message) = Message::from_bytes(data) {
                        if message.query_count() == 1 {
                            let query = &message.queries()[0];
                            let name = query.name().to_utf8();
                            if query.query_type() == RecordType::PTR && name == self.ptr_name {
                                log::debug!("found ptr query");
                                if let Err(err) =
                                self.listener.send_to(self.arp_data.as_slice(), from)
                                {
                                    log::error!("send response to {} failed:{}", from, err);
                                }
                                continue;
                            }
                            let key = Self::get_message_key(&message);
                            if let Some(result) = self.store.get_mut(&key) {
                                if result.response.is_some() && result.expire_time > now {
                                    log::info!("query:{} found in cache", key);
                                    result.response.as_mut().unwrap().set_id(message.id());
                                    if let Err(err) = self.listener.send_to(
                                        result
                                            .response
                                            .as_ref()
                                            .unwrap()
                                            .to_vec()
                                            .unwrap()
                                            .as_slice(),
                                        from,
                                    ) {
                                        log::error!("send response to {} failed:{}", from, err);
                                    }
                                    continue;
                                }
                            }
                            log::info!("query:{} not cached", key);
                            if self.is_blocked(&name) {
                                if let Err(err) = self.trusted.send_to(data, self.trusted_addr) {
                                    log::error!("send to trusted dns failed:{}", err);
                                    continue;
                                }
                                log::info!("domain:{} is blocked", name);
                            } else {
                                if let Err(err) = self.poisoned.send_to(data, self.poisoned_addr) {
                                    log::error!("send to poisoned dns failed:{}", err);
                                    continue;
                                }
                                log::info!("domain:{} is not blocked", name);
                            }
                            self.add_request(key, from, message.id());
                        } else {
                            log::error!(
                                "query count:{} found in message:{:?}",
                                message.query_count(),
                                message
                            );
                        }
                    } else {
                        log::error!("invalid request message received from {}", from);
                    }
                }
                Err(err) if err.kind() == ErrorKind::WouldBlock => break,
                Err(err) if err.kind() == ErrorKind::ConnectionReset => continue,
                Err(err) => {
                    log::error!("dns request recv failed:{}, kind:{:?}", err, err.kind());
                    poll.registry()
                        .reregister(&mut self.listener, Token(DNS_LOCAL), Interest::READABLE)
                        .unwrap();
                    break;
                }
            }
        }
    }

    fn get_message_key(message: &Message) -> String {
        let query = &message.queries()[0];
        let name = query.name().to_utf8();
        name + "|" + query.query_type().to_string().as_str()
    }

    fn dispatch_server(
        recv_socket: &UdpSocket,
        send_socket: &UdpSocket,
        buffer: &mut [u8],
        store: &mut HashMap<String, QueryResult>,
        route_added: &mut HashSet<u32>,
        adapter_index: u32,
        blocked: bool,
    ) -> bool {
        let now = Instant::now();
        loop {
            match recv_socket.recv_from(buffer) {
                Ok((length, from)) => {
                    let data = &buffer[..length];
                    if let Ok(mut message) = Message::from_bytes(data) {
                        log::debug!("response:{:?}", message);
                        let name = Self::get_message_key(&message);
                        if let Some(result) = store.get_mut(&name) {
                            for (address, id) in &result.addresses {
                                message.set_id(*id);
                                if let Err(err) = send_socket
                                    .send_to(message.to_vec().unwrap().as_slice(), *address)
                                {
                                    log::error!("send to {} failed:{}", address, err);
                                } else {
                                    log::debug!("send response to {}", address);
                                }
                            }
                            let mut timeout = 0;
                            for record in message.answers() {
                                timeout = record.ttl();
                                if let Some(addr) = record.data().and_then(|data| data.to_ip_addr())
                                {
                                    if OPTIONS.dns_args().add_route && blocked && addr.is_ipv4() {
                                        if let IpAddr::V4(addr) = addr {
                                            let addr: u32 = addr.into();
                                            if !route_added.contains(&addr)
                                                && route_add_with_if(addr, !0, 0, adapter_index)
                                                .is_ok()
                                            {
                                                route_added.insert(addr);
                                            }
                                        }
                                    }
                                }
                                log::info!(
                                    "got response {} -> {}, expire in {} seconds",
                                    name,
                                    record.to_string(),
                                    timeout,
                                );
                            }
                            result.expire_time = now + Duration::new(timeout as u64, 0);
                            result.addresses.clear();
                            result.response.replace(message);
                        } else {
                            log::error!("key:{} not found in store", name);
                        }
                    } else {
                        log::error!("invalid response message received from {}", from);
                    }
                }
                Err(err) if err.kind() == ErrorKind::WouldBlock => break,
                Err(err) if err.kind() == ErrorKind::ConnectionReset => continue,
                Err(err) => {
                    log::error!(
                        "dns response from:{:?} recv failed:{}",
                        recv_socket.local_addr(),
                        err
                    );
                    return false;
                }
            }
        }
        true
    }

    fn dispatch_trusted(&mut self, poll: &Poll) {
        if !Self::dispatch_server(
            &self.trusted,
            &self.listener,
            self.buffer.as_mut_slice(),
            &mut self.store,
            &mut self.route_added,
            self.adapter_index,
            true,
        ) {
            poll.registry()
                .reregister(&mut self.trusted, Token(DNS_TRUSTED), Interest::READABLE)
                .unwrap();
        }
    }

    fn dispatch_poisoned(&mut self, poll: &Poll) {
        if !Self::dispatch_server(
            &self.poisoned,
            &self.listener,
            self.buffer.as_mut_slice(),
            &mut self.store,
            &mut self.route_added,
            self.adapter_index,
            false,
        ) {
            poll.registry()
                .reregister(&mut self.poisoned, Token(DNS_POISONED), Interest::READABLE)
                .unwrap();
        }
    }

    fn is_blocked(&self, name: &str) -> bool {
        self.blocked_domains.contains(name)
    }
    fn add_request(&mut self, name: String, address: SocketAddr, id: u16) {
        let result = if let Some(result) = self.store.get_mut(&name) {
            result
        } else {
            self.store.insert(
                name.clone(),
                QueryResult {
                    addresses: vec![],
                    response: None,
                    expire_time: Instant::now()
                        + Duration::new(OPTIONS.dns_args().dns_cache_time, 0),
                },
            );
            self.store.get_mut(&name).unwrap()
        };
        result.addresses.push((address, id));
    }
}
