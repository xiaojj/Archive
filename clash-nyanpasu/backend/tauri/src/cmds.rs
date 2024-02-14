use crate::{
    config::*,
    core::{tasks::jobs::ProfilesJobGuard, updater::ManifestVersionLatest, *},
    feat, ret_err,
    utils::{
        candy, dirs, help,
        resolve::{self, save_window_state},
    },
    wrap_err,
};
use anyhow::{Context, Result};
use chrono::Local;
use log::debug;
use serde_yaml::Mapping;
use std::collections::{HashMap, VecDeque};
use sysproxy::Sysproxy;

use tauri::api::dialog::FileDialogBuilder;

type CmdResult<T = ()> = Result<T, String>;

#[tauri::command]
pub fn get_profiles() -> CmdResult<IProfiles> {
    Ok(Config::profiles().data().clone())
}

#[cfg(target_os = "windows")]
#[tauri::command]
pub fn is_portable() -> CmdResult<bool> {
    Ok(crate::utils::dirs::get_portable_flag())
}

#[cfg(not(target_os = "windows"))]
#[tauri::command]
pub fn is_portable() -> CmdResult<bool> {
    Ok(false)
}

#[tauri::command]
pub async fn enhance_profiles() -> CmdResult {
    wrap_err!(CoreManager::global().update_config().await)?;
    handle::Handle::refresh_clash();
    Ok(())
}

#[tauri::command]
pub async fn import_profile(url: String, option: Option<PrfOption>) -> CmdResult {
    let item = wrap_err!(PrfItem::from_url(&url, None, None, option).await)?;
    wrap_err!(Config::profiles().data().append_item(item))
}

#[tauri::command]
pub async fn create_profile(item: PrfItem, file_data: Option<String>) -> CmdResult {
    let item = wrap_err!(PrfItem::from(item, file_data).await)?;
    wrap_err!(Config::profiles().data().append_item(item))
}

#[tauri::command]
pub async fn reorder_profile(active_id: String, over_id: String) -> CmdResult {
    wrap_err!(Config::profiles().data().reorder(active_id, over_id))
}

#[tauri::command]
pub async fn update_profile(index: String, option: Option<PrfOption>) -> CmdResult {
    wrap_err!(feat::update_profile(index, option).await)
}

#[tauri::command]
pub async fn delete_profile(index: String) -> CmdResult {
    let should_update = wrap_err!({ Config::profiles().data().delete_item(index) })?;
    if should_update {
        wrap_err!(CoreManager::global().update_config().await)?;
        handle::Handle::refresh_clash();
    }

    Ok(())
}

/// 修改profiles的
#[tauri::command]
pub async fn patch_profiles_config(profiles: IProfiles) -> CmdResult {
    wrap_err!({ Config::profiles().draft().patch_config(profiles) })?;

    match CoreManager::global().update_config().await {
        Ok(_) => {
            handle::Handle::refresh_clash();
            Config::profiles().apply();
            wrap_err!(Config::profiles().data().save_file())?;
            Ok(())
        }
        Err(err) => {
            Config::profiles().discard();
            log::error!(target: "app", "{err}");
            Err(format!("{err}"))
        }
    }
}

/// 修改某个profile item的
#[tauri::command]
pub fn patch_profile(index: String, profile: PrfItem) -> CmdResult {
    wrap_err!(Config::profiles().data().patch_item(index, profile))?;
    ProfilesJobGuard::global().lock().refresh();
    Ok(())
}

#[tauri::command]
pub fn view_profile(app_handle: tauri::AppHandle, index: String) -> CmdResult {
    let file = {
        wrap_err!(Config::profiles().latest().get_item(&index))?
            .file
            .clone()
            .ok_or("the file field is null")
    }?;

    let path = wrap_err!(dirs::app_profiles_dir())?.join(file);
    if !path.exists() {
        ret_err!("the file not found");
    }

    wrap_err!(help::open_file(app_handle, path))
}

#[tauri::command]
pub fn read_profile_file(index: String) -> CmdResult<String> {
    let profiles = Config::profiles();
    let profiles = profiles.latest();
    let item = wrap_err!(profiles.get_item(&index))?;
    let data = wrap_err!(item.read_file())?;
    Ok(data)
}

#[tauri::command]
pub fn save_profile_file(index: String, file_data: Option<String>) -> CmdResult {
    if file_data.is_none() {
        return Ok(());
    }

    let profiles = Config::profiles();
    let profiles = profiles.latest();
    let item = wrap_err!(profiles.get_item(&index))?;
    wrap_err!(item.save_file(file_data.unwrap()))
}

#[tauri::command]
pub fn get_clash_info() -> CmdResult<ClashInfo> {
    Ok(Config::clash().latest().get_client_info())
}

#[tauri::command]
pub fn get_runtime_config() -> CmdResult<Option<Mapping>> {
    Ok(Config::runtime().latest().config.clone())
}

#[tauri::command]
pub fn get_runtime_yaml() -> CmdResult<String> {
    let runtime = Config::runtime();
    let runtime = runtime.latest();
    let config = runtime.config.as_ref();
    wrap_err!(config
        .ok_or(anyhow::anyhow!("failed to parse config to yaml file"))
        .and_then(
            |config| serde_yaml::to_string(config).context("failed to convert config to yaml")
        ))
}

#[tauri::command]
pub fn get_runtime_exists() -> CmdResult<Vec<String>> {
    Ok(Config::runtime().latest().exists_keys.clone())
}

#[tauri::command]
pub fn get_runtime_logs() -> CmdResult<HashMap<String, Vec<(String, String)>>> {
    Ok(Config::runtime().latest().chain_logs.clone())
}

#[tauri::command]
pub async fn patch_clash_config(payload: Mapping) -> CmdResult {
    wrap_err!(feat::patch_clash(payload).await)?;
    feat::update_proxies_buff(None);
    Ok(())
}

#[tauri::command]
pub fn get_verge_config() -> CmdResult<IVerge> {
    Ok(Config::verge().data().clone())
}

#[tauri::command]
pub async fn patch_verge_config(payload: IVerge) -> CmdResult {
    wrap_err!(feat::patch_verge(payload).await)
}

#[tauri::command]
pub async fn change_clash_core(clash_core: Option<ClashCore>) -> CmdResult {
    wrap_err!(CoreManager::global().change_core(clash_core).await)
}

/// restart the sidecar
#[tauri::command]
pub async fn restart_sidecar() -> CmdResult {
    wrap_err!(CoreManager::global().run_core().await)
}

#[tauri::command]
pub fn grant_permission(_core: String) -> CmdResult {
    #[cfg(any(target_os = "macos", target_os = "linux"))]
    return wrap_err!(manager::grant_permission(_core));

    #[cfg(not(any(target_os = "macos", target_os = "linux")))]
    return Err("Unsupported target".into());
}

/// get the system proxy
#[tauri::command]
pub fn get_sys_proxy() -> CmdResult<Mapping> {
    let current = wrap_err!(Sysproxy::get_system_proxy())?;

    let mut map = Mapping::new();
    map.insert("enable".into(), current.enable.into());
    map.insert(
        "server".into(),
        format!("{}:{}", current.host, current.port).into(),
    );
    map.insert("bypass".into(), current.bypass.into());

    Ok(map)
}

#[tauri::command]
pub fn get_clash_logs() -> CmdResult<VecDeque<String>> {
    Ok(logger::Logger::global().get_log())
}

#[tauri::command]
pub fn open_app_dir() -> CmdResult<()> {
    let app_dir = wrap_err!(dirs::app_home_dir())?;
    wrap_err!(open::that(app_dir))
}

#[tauri::command]
pub fn open_core_dir() -> CmdResult<()> {
    let core_dir = wrap_err!(tauri::utils::platform::current_exe())?;
    let core_dir = core_dir
        .parent()
        .ok_or("failed to get core dir".to_string())?;
    wrap_err!(open::that(core_dir))
}

#[tauri::command]
pub fn open_logs_dir() -> CmdResult<()> {
    let log_dir = wrap_err!(dirs::app_logs_dir())?;
    wrap_err!(open::that(log_dir))
}

#[tauri::command]
pub fn open_web_url(url: String) -> CmdResult<()> {
    wrap_err!(open::that(url))
}

#[tauri::command]
pub fn save_window_size_state() -> CmdResult<()> {
    let handle = handle::Handle::global().app_handle.lock().clone().unwrap();
    wrap_err!(save_window_state(&handle, true))
}

#[tauri::command]
pub async fn fetch_latest_core_versions() -> CmdResult<ManifestVersionLatest> {
    let mut updater = updater::Updater::global().write().await; // It is intended to block here
    wrap_err!(updater.fetch_latest().await)?;
    Ok(updater.get_latest_versions())
}

#[tauri::command]
pub async fn get_core_version(core_type: ClashCore) -> CmdResult<String> {
    match tokio::task::spawn_blocking(move || resolve::resolve_core_version(&core_type)).await {
        Ok(Ok(version)) => Ok(version),
        Ok(Err(err)) => Err(format!("{err}")),
        Err(err) => Err(format!("{err}")),
    }
}

#[tauri::command]
pub async fn collect_logs() -> CmdResult {
    let now = Local::now().format("%Y-%m-%d");
    let fname = format!("{}-log", now);
    let builder = FileDialogBuilder::new();
    builder
        .add_filter("archive files", &["zip"])
        .set_file_name(&fname)
        .set_title("Save log archive")
        .save_file(|file_path| match file_path {
            None => (),
            Some(path) => {
                debug!("{:#?}", path.as_os_str());
                match candy::collect_logs(&path) {
                    Ok(_) => (),
                    Err(err) => {
                        log::error!(target: "app", "{err}");
                    }
                }
            }
        });
    Ok(())
}

#[tauri::command]
pub async fn update_core(core_type: ClashCore) -> CmdResult {
    wrap_err!(
        updater::Updater::global()
            .read()
            .await
            .update_core(&core_type)
            .await
    )
}

#[tauri::command]
pub async fn clash_api_get_proxy_delay(
    name: String,
    url: Option<String>,
) -> CmdResult<clash::api::DelayRes> {
    match clash::api::get_proxy_delay(name, url).await {
        Ok(res) => Ok(res),
        Err(err) => Err(err.to_string()),
    }
}

#[tauri::command]
pub async fn get_proxies() -> CmdResult<crate::core::clash::proxies::Proxies> {
    use crate::core::clash::proxies::ProxiesGuard;
    use crate::core::clash::proxies::ProxiesGuardExt;
    {
        let guard = ProxiesGuard::global().read();
        if guard.is_updated() {
            return Ok(guard.inner().clone());
        }
    }
    match ProxiesGuard::global().update().await {
        Ok(_) => {
            let proxies = ProxiesGuard::global().read().inner().clone();
            Ok(proxies)
        }
        Err(err) => Err(err.to_string()),
    }
}

#[tauri::command]
pub async fn select_proxy(group: String, name: String) -> CmdResult<()> {
    use crate::core::clash::proxies::ProxiesGuard;
    use crate::core::clash::proxies::ProxiesGuardExt;
    wrap_err!(ProxiesGuard::global().select_proxy(&group, &name).await)?;
    Ok(())
}

#[tauri::command]
pub async fn update_proxy_provider(name: String) -> CmdResult<()> {
    use crate::core::clash::{
        api,
        proxies::{ProxiesGuard, ProxiesGuardExt},
    };
    wrap_err!(api::update_providers_proxies_group(&name).await)?;
    wrap_err!(ProxiesGuard::global().update().await)?;
    Ok(())
}

#[cfg(windows)]
pub mod uwp {
    use super::*;
    use crate::core::win_uwp;

    #[tauri::command]
    pub async fn invoke_uwp_tool() -> CmdResult {
        wrap_err!(win_uwp::invoke_uwptools().await)
    }
}

#[cfg(windows)]
pub mod service {
    use super::*;
    use crate::core::win_service;

    #[tauri::command]
    pub async fn check_service() -> CmdResult<win_service::JsonResponse> {
        wrap_err!(win_service::check_service().await)
    }

    #[tauri::command]
    pub async fn install_service() -> CmdResult {
        wrap_err!(win_service::install_service().await)
    }

    #[tauri::command]
    pub async fn uninstall_service() -> CmdResult {
        wrap_err!(win_service::uninstall_service().await)
    }
}

#[cfg(not(windows))]
pub mod service {
    use super::*;

    #[tauri::command]
    pub async fn check_service() -> CmdResult {
        Ok(())
    }
    #[tauri::command]
    pub async fn install_service() -> CmdResult {
        Ok(())
    }
    #[tauri::command]
    pub async fn uninstall_service() -> CmdResult {
        Ok(())
    }
}

#[cfg(not(windows))]
pub mod uwp {
    use super::*;

    #[tauri::command]
    pub async fn invoke_uwp_tool() -> CmdResult {
        Ok(())
    }
}
