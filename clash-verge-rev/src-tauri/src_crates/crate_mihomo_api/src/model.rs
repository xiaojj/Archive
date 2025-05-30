use std::sync::{Arc, Mutex};

pub struct MihomoData {
    pub(crate) proxies: serde_json::Value,
    pub(crate) providers_proxies: serde_json::Value,
}

#[derive(Clone)]
pub struct MihomoManager {
    pub(crate) mihomo_server: String,
    pub(crate) data: Arc<Mutex<MihomoData>>,
    pub(crate) client: reqwest::Client,
}

#[cfg(feature = "debug")]
impl Drop for MihomoData {
    fn drop(&mut self) {
        println!("Dropping MihomoData");
    }
}

#[cfg(feature = "debug")]
impl Drop for MihomoManager {
    fn drop(&mut self) {
        println!("Dropping MihomoManager");
    }
}
