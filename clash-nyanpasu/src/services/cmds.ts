import { NotificationType, useNotification } from "@/hooks/use-notification";
import type { ManifestVersion } from "@root/scripts/generate-latest-version";
import { invoke } from "@tauri-apps/api/tauri";
import dayjs from "dayjs";
import { t } from "i18next";
import type { IProxies } from "./api";
export async function getClashLogs() {
  const regex = /time="(.+?)"\s+level=(.+?)\s+msg="(.+?)"/;
  const newRegex = /(.+?)\s+(.+?)\s+(.+)/;
  const logs = await invoke<string[]>("get_clash_logs");

  return logs
    .map((log) => {
      const result = log.match(regex);
      if (result) {
        const [_, _time, type, payload] = result;
        const time = dayjs(_time).format("MM-DD HH:mm:ss");
        return { time, type, payload };
      }

      const result2 = log.match(newRegex);
      if (result2) {
        const [_, time, type, payload] = result2;
        return { time, type, payload };
      }
      return null;
    })
    .filter(Boolean) as ILogItem[];
}

export async function getProfiles() {
  return invoke<IProfilesConfig>("get_profiles");
}

export async function enhanceProfiles() {
  return invoke<void>("enhance_profiles");
}

export async function patchProfilesConfig(profiles: IProfilesConfig) {
  return invoke<void>("patch_profiles_config", { profiles });
}

export async function createProfile(
  item: Partial<IProfileItem>,
  fileData?: string | null,
) {
  return invoke<void>("create_profile", { item, fileData });
}

export async function viewProfile(index: string) {
  return invoke<void>("view_profile", { index });
}

export async function readProfileFile(index: string) {
  return invoke<string>("read_profile_file", { index });
}

export async function saveProfileFile(index: string, fileData: string) {
  return invoke<void>("save_profile_file", { index, fileData });
}

export async function importProfile(url: string) {
  return invoke<void>("import_profile", {
    url,
    option: { with_proxy: true },
  });
}

export async function reorderProfile(activeId: string, overId: string) {
  return invoke<void>("reorder_profile", {
    activeId,
    overId,
  });
}

export async function updateProfile(index: string, option?: IProfileOption) {
  return invoke<void>("update_profile", { index, option });
}

export async function deleteProfile(index: string) {
  return invoke<void>("delete_profile", { index });
}

export async function patchProfile(
  index: string,
  profile: Partial<IProfileItem>,
) {
  return invoke<void>("patch_profile", { index, profile });
}

export async function getClashInfo() {
  return invoke<IClashInfo | null>("get_clash_info");
}

export async function getRuntimeConfig() {
  return invoke<any | null>("get_runtime_config");
}

export async function getRuntimeYaml() {
  return invoke<string | null>("get_runtime_yaml");
}

export async function getRuntimeExists() {
  return invoke<string[]>("get_runtime_exists");
}

export async function getRuntimeLogs() {
  return invoke<Record<string, [string, string][]>>("get_runtime_logs");
}

export async function patchClashConfig(payload: Partial<IConfigData>) {
  return invoke<void>("patch_clash_config", { payload });
}

export async function getVergeConfig() {
  return invoke<IVergeConfig>("get_verge_config");
}

export async function patchVergeConfig(payload: IVergeConfig) {
  return invoke<void>("patch_verge_config", { payload });
}

export async function getSystemProxy() {
  return invoke<{
    enable: boolean;
    server: string;
    bypass: string;
  }>("get_sys_proxy");
}

export async function changeClashCore(clashCore: string) {
  return invoke<any>("change_clash_core", { clashCore });
}

export async function restartSidecar() {
  return invoke<void>("restart_sidecar");
}

export async function grantPermission(core: string) {
  return invoke<void>("grant_permission", { core });
}

export async function openAppDir() {
  return invoke<void>("open_app_dir").catch((err) =>
    useNotification({
      title: t("Error"),
      body: err.message || err.toString(),
      type: NotificationType.Error,
    }),
  );
}

export async function openCoreDir() {
  return invoke<void>("open_core_dir").catch((err) =>
    useNotification({
      title: t("Error"),
      body: err.message || err.toString(),
      type: NotificationType.Error,
    }),
  );
}

export async function openLogsDir() {
  return invoke<void>("open_logs_dir").catch((err) =>
    useNotification({
      title: t("Error"),
      body: err.message || err.toString(),
      type: NotificationType.Error,
    }),
  );
}

export async function openWebUrl(url: string) {
  return invoke<void>("open_web_url", { url });
}

/// service mode

export async function checkService() {
  try {
    const result = await invoke<any>("check_service");
    if (result?.code === 0) return "active";
    if (result?.code === 400) return "installed";
    return "unknown";
  } catch (err: any) {
    return "uninstall";
  }
}

export async function installService() {
  return invoke<void>("install_service");
}

export async function uninstallService() {
  return invoke<void>("uninstall_service");
}

export async function invoke_uwp_tool() {
  return invoke<void>("invoke_uwp_tool").catch((err) =>
    useNotification({
      title: t("Error"),
      body: err.message || err.toString(),
      type: NotificationType.Error,
    }),
  );
}

export async function save_window_size_state() {
  return invoke<void>("save_window_size_state").catch((err) =>
    useNotification({
      title: t("Error"),
      body: err.message || err.toString(),
      type: NotificationType.Error,
    }),
  );
}

export async function getCoreVersion(
  coreType: Required<IVergeConfig>["clash_core"],
) {
  return invoke<string>("get_core_version", { coreType });
}

export async function fetchLatestCoreVersions() {
  return invoke<ManifestVersion["latest"]>("fetch_latest_core_versions");
}

export async function updateCore(
  coreType: Required<IVergeConfig>["clash_core"],
) {
  return invoke<void>("update_core", { coreType });
}

export async function collectLogs() {
  return invoke<void>("collect_logs");
}

export async function cmdGetProxyDelay(name: string, url?: string) {
  name = encodeURIComponent(name);
  return invoke<{ delay: number }>("clash_api_get_proxy_delay", { name, url });
}

export async function isPortable() {
  if (OS_PLATFORM !== "win32") return false;
  return invoke<boolean>("is_portable");
}

export async function getProxies() {
  return invoke<IProxies>("get_proxies");
}

export async function selectProxy(group: string, name: string) {
  return invoke<void>("select_proxy", { group, name });
}

export async function updateProxyProvider(name: string) {
  return invoke<void>("update_proxy_provider", { name });
}
