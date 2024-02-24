use crate::{cmds, config::Config, feat, utils::resolve};
use anyhow::Result;
use rust_i18n::t;
use tauri::{
    api, AppHandle, CustomMenuItem, Manager, SystemTrayEvent, SystemTrayMenu, SystemTrayMenuItem,
    SystemTraySubmenu,
};

use super::storage;

pub struct Tray {}

pub mod proxies;

use self::proxies::SystemTrayMenuProxiesExt;

impl Tray {
    pub fn tray_menu(_app_handle: &AppHandle) -> SystemTrayMenu {
        let version = env!("NYANPASU_VERSION");

        SystemTrayMenu::new()
            .add_item(CustomMenuItem::new("open_window", t!("tray.dashboard")))
            .add_native_item(SystemTrayMenuItem::Separator)
            .setup_proxies() // Setup the proxies menu
            .add_native_item(SystemTrayMenuItem::Separator)
            .add_item(CustomMenuItem::new("rule_mode", t!("tray.rule_mode")))
            .add_item(CustomMenuItem::new("global_mode", t!("tray.global_mode")))
            .add_item(CustomMenuItem::new("direct_mode", t!("tray.direct_mode")))
            .add_item(CustomMenuItem::new("script_mode", t!("tray.script_mode")))
            .add_native_item(SystemTrayMenuItem::Separator)
            .add_item(CustomMenuItem::new("system_proxy", t!("tray.system_proxy")))
            .add_item(CustomMenuItem::new("tun_mode", t!("tray.tun_mode")))
            .add_item(CustomMenuItem::new("copy_env_sh", t!("tray.copy_env.sh")))
            .add_item(CustomMenuItem::new("copy_env_cmd", t!("tray.copy_env.cmd")))
            .add_item(CustomMenuItem::new("copy_env_ps", t!("tray.copy_env.ps")))
            .add_submenu(SystemTraySubmenu::new(
                t!("tray.open_dir.menu"),
                SystemTrayMenu::new()
                    .add_item(CustomMenuItem::new(
                        "open_app_dir",
                        t!("tray.open_dir.app_dir"),
                    ))
                    .add_item(CustomMenuItem::new(
                        "open_core_dir",
                        t!("tray.open_dir.core_dir"),
                    ))
                    .add_item(CustomMenuItem::new(
                        "open_logs_dir",
                        t!("tray.open_dir.log_dir"),
                    )),
            ))
            .add_submenu(SystemTraySubmenu::new(
                t!("tray.more.menu"),
                SystemTrayMenu::new()
                    .add_item(CustomMenuItem::new(
                        "restart_clash",
                        t!("tray.more.restart_clash"),
                    ))
                    .add_item(CustomMenuItem::new(
                        "restart_app",
                        t!("tray.more.restart_app"),
                    ))
                    .add_item(
                        CustomMenuItem::new("app_version", format!("Version {version}")).disabled(),
                    ),
            ))
            .add_native_item(SystemTrayMenuItem::Separator)
            .add_item(CustomMenuItem::new("quit", t!("tray.quit")).accelerator("CmdOrControl+Q"))
    }

    pub fn update_systray(app_handle: &AppHandle) -> Result<()> {
        app_handle
            .tray_handle()
            .set_menu(Tray::tray_menu(app_handle))?;
        Tray::update_part(app_handle)?;
        Ok(())
    }

    pub fn update_part(app_handle: &AppHandle) -> Result<()> {
        let mode = crate::utils::config::get_current_clash_mode();

        let tray = app_handle.tray_handle();

        let _ = tray.get_item("rule_mode").set_selected(mode == "rule");
        let _ = tray.get_item("global_mode").set_selected(mode == "global");
        let _ = tray.get_item("direct_mode").set_selected(mode == "direct");
        let _ = tray.get_item("script_mode").set_selected(mode == "script");

        let verge = Config::verge();
        let verge = verge.latest();
        let system_proxy = verge.enable_system_proxy.as_ref().unwrap_or(&false);
        let tun_mode = verge.enable_tun_mode.as_ref().unwrap_or(&false);

        #[cfg(target_os = "windows")]
        {
            let indication_icon = if *tun_mode {
                include_bytes!("../../../icons/win-tray-icon-blue.png").to_vec()
            } else if *system_proxy {
                include_bytes!("../../../icons/win-tray-icon-pink.png").to_vec()
            } else {
                include_bytes!("../../../icons/win-tray-icon.png").to_vec()
            };

            let _ = tray.set_icon(tauri::Icon::Raw(indication_icon));
        }

        let _ = tray.get_item("system_proxy").set_selected(*system_proxy);
        let _ = tray.get_item("tun_mode").set_selected(*tun_mode);

        #[cfg(not(target_os = "linux"))]
        {
            let switch_map = {
                let mut map = std::collections::HashMap::new();
                map.insert(true, t!("tray.proxy_action.on"));
                map.insert(false, t!("tray.proxy_action.off"));
                map
            };

            let _ = tray.set_tooltip(&format!(
                "{}: {}\n{}: {}",
                t!("tray.system_proxy"),
                switch_map[system_proxy],
                t!("tray.tun_mode"),
                switch_map[tun_mode]
            ));
        }

        Ok(())
    }

    pub fn on_system_tray_event(app_handle: &AppHandle, event: SystemTrayEvent) {
        match event {
            SystemTrayEvent::MenuItemClick { id, .. } => match id.as_str() {
                mode @ ("rule_mode" | "global_mode" | "direct_mode" | "script_mode") => {
                    let mode = &mode[0..mode.len() - 5];
                    feat::change_clash_mode(mode.into());
                }

                "open_window" => resolve::create_window(app_handle),
                "system_proxy" => feat::toggle_system_proxy(),
                "tun_mode" => feat::toggle_tun_mode(),
                "copy_env_sh" => feat::copy_clash_env("sh"),
                #[cfg(target_os = "windows")]
                "copy_env_cmd" => feat::copy_clash_env("cmd"),
                #[cfg(target_os = "windows")]
                "copy_env_ps" => feat::copy_clash_env("ps"),
                "open_app_dir" => crate::log_err!(cmds::open_app_dir()),
                "open_core_dir" => crate::log_err!(cmds::open_core_dir()),
                "open_logs_dir" => crate::log_err!(cmds::open_logs_dir()),
                "restart_clash" => feat::restart_clash_core(),
                "restart_app" => api::process::restart(&app_handle.env()),
                "quit" => {
                    let _ = resolve::save_window_state(app_handle, true);

                    resolve::resolve_reset();
                    api::process::kill_children();
                    app_handle.exit(0);
                    // flush all data to disk
                    storage::Storage::global().destroy().unwrap();
                    std::process::exit(0);
                }
                _ => {
                    proxies::on_system_tray_event(&id);
                }
            },
            #[cfg(target_os = "windows")]
            SystemTrayEvent::LeftClick { .. } => {
                resolve::create_window(app_handle);
            }
            _ => {}
        }
    }
}
