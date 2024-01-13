#![cfg(target_os = "ios")]

use smoltcp::{phy::Medium};
use std::io;
use std::os::unix::io::{AsRawFd, RawFd};

use std::cell::RefCell;
use std::rc::Rc;
use std::vec::Vec;

use smoltcp::phy::{self, Device, DeviceCapabilities};
use smoltcp::time::Instant;

use crate::IosContext;

#[derive(Debug)]
pub struct ContextInterfaceDesc {
    context: *mut libc::c_void,
    read_fd: libc::c_int,
    get_read_packet_context_data_fn: unsafe extern "C" fn(*mut libc::c_void, *mut libc::c_void) -> *const libc::c_void,
    get_read_packet_context_size_fn: unsafe extern "C" fn(*mut libc::c_void, *mut libc::c_void) -> libc::size_t,
    free_read_packet_context_size_fn: unsafe extern "C" fn(*mut libc::c_void, *mut libc::c_void),
    write_packets_fn: unsafe extern "C" fn(*mut libc::c_void, *const *const libc::c_void, *const libc::size_t, libc::c_int),
    previous_read_ctx: *mut libc::c_void,
    mtu: usize,
}

impl AsRawFd for ContextInterfaceDesc {
    fn as_raw_fd(&self) -> RawFd {
        self.read_fd
    }
}

impl ContextInterfaceDesc {
    pub fn from_context(context: *mut libc::c_void,
                        read_fd: libc::c_int,
                        get_read_packet_context_data_fn: unsafe extern "C" fn(*mut libc::c_void, *mut libc::c_void) -> *const libc::c_void,
                        get_read_packet_context_size_fn: unsafe extern "C" fn(*mut libc::c_void, *mut libc::c_void) -> libc::size_t,
                        free_read_packet_context_size_fn: unsafe extern "C" fn(*mut libc::c_void, *mut libc::c_void),
                        write_packets_fn: unsafe extern "C" fn(*mut libc::c_void, *const *const libc::c_void,  *const libc::size_t, libc::c_int),
                        mtu: usize) -> io::Result<ContextInterfaceDesc> {
        Ok(ContextInterfaceDesc {
          context: context, read_fd: read_fd,
          get_read_packet_context_data_fn: get_read_packet_context_data_fn,
          get_read_packet_context_size_fn: get_read_packet_context_size_fn,
          free_read_packet_context_size_fn: free_read_packet_context_size_fn,
          write_packets_fn: write_packets_fn,
          previous_read_ctx: 0 as *mut libc::c_void,
          mtu: mtu
        })
    }

    pub fn interface_mtu(&self) -> io::Result<usize> {
        Ok(self.mtu)
    }

    pub fn recv(&mut self) -> io::Result<(*const libc::c_void, usize)> {
        unsafe {
            if self.previous_read_ctx != 0 as *mut libc::c_void {
                (self.free_read_packet_context_size_fn)(self.context, self.previous_read_ctx);
                self.previous_read_ctx = 0 as *mut libc::c_void;
            }
            let mut context_buffer = vec![0; 8];
            let len = libc::read(
                self.read_fd,
                context_buffer.as_mut_ptr() as *mut libc::c_void,
                context_buffer.len(),
            );
            if len == -1 {
                return Err(io::Error::last_os_error());
            }
            let context: *mut libc::c_void = *(context_buffer.as_ptr() as *mut *mut libc::c_void);
            let context_ptr: *const libc::c_void = (self.get_read_packet_context_data_fn)(self.context, context);
            let context_size: libc::size_t = (self.get_read_packet_context_size_fn)(self.context, context);
            self.previous_read_ctx = context;
            Ok((context_ptr, context_size as usize))
        }
    }

    pub fn send(&mut self, buffer: &[u8]) -> io::Result<usize> {
        unsafe {
            let packet : *const libc::c_void = buffer.as_ptr() as *const libc::c_void;
            let packet_length : libc::size_t = buffer.len();
            (self.write_packets_fn)(self.context, &packet, &packet_length, 1);
            Ok(buffer.len() as usize)
        }
    }
}

impl Drop for ContextInterfaceDesc {
    fn drop(&mut self) {
        if self.previous_read_ctx != 0 as *mut libc::c_void {
            unsafe { (self.free_read_packet_context_size_fn)(self.context, self.previous_read_ctx); }
            self.previous_read_ctx = 0 as *mut libc::c_void;
        }
        unsafe {
            libc::close(self.read_fd);
        }
    }
}

/// A virtual TUN (IP) or TAP (Ethernet) interface.
#[derive(Debug)]
pub struct ContextInterface {
    lower: Rc<RefCell<ContextInterfaceDesc>>,
    mtu: usize,
    medium: Medium,
}

impl AsRawFd for ContextInterface {
    fn as_raw_fd(&self) -> RawFd {
        self.lower.borrow().as_raw_fd()
    }
}

impl ContextInterface {
    /// Attaches to a TUN/TAP interface specified by file descriptor `fd`.
    ///
    /// On platforms like Android, a file descriptor to a tun interface is exposed.
    /// On these platforms, a ContextInterface cannot be instantiated with a name.
    pub fn from_context(context: &IosContext, medium: Medium, mtu: usize) -> io::Result<ContextInterface> {
        let lower = ContextInterfaceDesc::from_context(context.context,
          context.read_fd,
          context.get_read_packet_context_data_fn,
          context.get_read_packet_context_size_fn,
          context.free_read_packet_context_size_fn,
          context.write_packets_fn,
          mtu)?;
        Ok(ContextInterface {
            lower: Rc::new(RefCell::new(lower)),
            mtu,
            medium,
        })
    }
}

impl Device for ContextInterface {
    type RxToken<'a> = RxToken;
    type TxToken<'a> = TxToken;

    fn capabilities(&self) -> DeviceCapabilities {
        let mut caps = DeviceCapabilities::default();
        caps.max_transmission_unit = self.mtu;
        caps.medium = self.medium;
        caps
    }

    fn receive(&mut self, _timestamp: Instant) -> Option<(Self::RxToken<'_>, Self::TxToken<'_>)> {
        let mut lower = self.lower.borrow_mut();
        match lower.recv() {
            Ok((ptr, size)) => {
                let mut buffer = vec![0; self.mtu];
                // FIXME remove memcpy
                unsafe { libc::memcpy(buffer.as_mut_ptr() as *mut libc::c_void, ptr, size)};
                buffer.resize(size, 0);
                let rx = RxToken { buffer };
                let tx = TxToken {
                    lower: self.lower.clone(),
                };
                Some((rx, tx))
            }
            Err(err) if err.kind() == io::ErrorKind::WouldBlock => None,
            Err(err) => panic!("{}", err),
        }
    }

    fn transmit(&mut self, _timestamp: Instant) -> Option<Self::TxToken<'_>> {
        Some(TxToken {
            lower: self.lower.clone(),
        })
    }
}

#[doc(hidden)]
pub struct RxToken {
    buffer: Vec<u8>,
}

impl phy::RxToken for RxToken {
    fn consume<R, F>(mut self, f: F) -> R
    where
        F: FnOnce(&mut [u8]) -> R,
    {
        f(&mut self.buffer[..])
    }
}

#[doc(hidden)]
pub struct TxToken {
    lower: Rc<RefCell<ContextInterfaceDesc>>,
}

impl phy::TxToken for TxToken {
    fn consume<R, F>(self, len: usize, f: F) -> R
    where
        F: FnOnce(&mut [u8]) -> R,
    {
        let mut lower = self.lower.borrow_mut();
        let mut buffer = vec![0; len];
        let result = f(&mut buffer);
        match lower.send(&buffer[..]) {
            Ok(_) => {}
            Err(err) if err.kind() == io::ErrorKind::WouldBlock => {
                // net_debug!("phy: tx failed due to WouldBlock")
            }
            Err(err) => panic!("{}", err),
        }
        result
    }
}
