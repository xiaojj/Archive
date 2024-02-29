import { DialogRef } from "@/components/base";
import { useMessage } from "@/hooks/use-notification";
import { useVerge } from "@/hooks/use-verge";
import {
  collectLogs,
  openAppDir,
  openCoreDir,
  openLogsDir,
} from "@/services/cmds";
import getSystem from "@/utils/get-system";
import { ArrowForward, IosShare } from "@mui/icons-material";
import {
  Chip,
  CircularProgress,
  IconButton,
  MenuItem,
  Select,
  Tooltip,
  Typography,
} from "@mui/material";
import { version } from "@root/package.json";
import { checkUpdate } from "@tauri-apps/api/updater";
import { useLockFn } from "ahooks";
import { useRef, useState } from "react";
import { useTranslation } from "react-i18next";
import { ConfigViewer } from "./mods/config-viewer";
import { GuardState } from "./mods/guard-state";
import { HotkeyViewer } from "./mods/hotkey-viewer";
import { LayoutViewer } from "./mods/layout-viewer";
import { MiscViewer } from "./mods/misc-viewer";
import { SettingItem, SettingList } from "./mods/setting-comp";
import { TasksViewer } from "./mods/tasks-viewer";
import { ThemeModeSwitch } from "./mods/theme-mode-switch";
import { ThemeViewer } from "./mods/theme-viewer";
import { UpdateViewer } from "./mods/update-viewer";
import MDYSwitch from "../common/mdy-switch";

interface Props {
  onError?: (err: Error) => void;
}

const OS = getSystem();

const SettingVerge = ({ onError }: Props) => {
  const { t } = useTranslation();

  const { verge, patchVerge } = useVerge();
  const { theme_mode, language, disbale_auto_check_update } = verge ?? {};

  const [loading, setLoading] = useState({
    theme_mode: false,
    language: false,
    onCheckUpdate: false,
  });

  const tipChips = useRef({
    onCheckUpdate: "",
  });

  const configRef = useRef<DialogRef>(null);
  const hotkeyRef = useRef<DialogRef>(null);
  const miscRef = useRef<DialogRef>(null);
  const themeRef = useRef<DialogRef>(null);
  const layoutRef = useRef<DialogRef>(null);
  const updateRef = useRef<DialogRef>(null);
  const tasksRef = useRef<DialogRef>(null);

  const onCheckUpdate = useLockFn(async () => {
    try {
      setLoading((prevLoading) => ({
        ...prevLoading,
        onCheckUpdate: true,
      }));

      const info = await checkUpdate();

      if (!info?.shouldUpdate) {
        tipChips.current.onCheckUpdate = t("No update available");
      } else {
        updateRef.current?.open();
      }
    } catch (err: any) {
      useMessage(err.message || err.toString(), {
        title: t("Error"),
        type: "error",
      });
    } finally {
      setLoading((prevLoading) => ({
        ...prevLoading,
        onCheckUpdate: false,
      }));
    }
  });

  const onSwitchFormat = (_e: any, value: boolean) => value;

  return (
    <SettingList title={t("Nyanpasu Setting")}>
      <ThemeViewer ref={themeRef} />
      <ConfigViewer ref={configRef} />
      <HotkeyViewer ref={hotkeyRef} />
      <MiscViewer ref={miscRef} />
      <LayoutViewer ref={layoutRef} />
      <UpdateViewer ref={updateRef} />
      <TasksViewer ref={tasksRef} />

      <SettingItem label={t("Language")}>
        <GuardState
          value={language ?? "en"}
          onCatch={onError}
          onFormat={(e: any) => e.target.value}
          onGuard={(e) => patchVerge({ language: e })}
          loading={loading["language"]}
        >
          <Select size="small" sx={{ width: 100, "> div": { py: "7.5px" } }}>
            <MenuItem value="zh">中文</MenuItem>
            <MenuItem value="en">English</MenuItem>
            <MenuItem value="ru">Русский</MenuItem>
          </Select>
        </GuardState>
      </SettingItem>

      <SettingItem label={t("Theme Mode")}>
        <GuardState
          value={theme_mode}
          onCatch={onError}
          onGuard={(e) => patchVerge({ theme_mode: e })}
          loading={loading["theme_mode"]}
        >
          <ThemeModeSwitch />
        </GuardState>
      </SettingItem>

      <SettingItem label={t("Theme Setting")}>
        <IconButton
          color="inherit"
          size="small"
          sx={{ my: "2px" }}
          onClick={() => themeRef.current?.open()}
        >
          <ArrowForward />
        </IconButton>
      </SettingItem>

      <SettingItem label={t("Layout Setting")}>
        <IconButton
          color="inherit"
          size="small"
          sx={{ my: "2px" }}
          onClick={() => layoutRef.current?.open()}
        >
          <ArrowForward />
        </IconButton>
      </SettingItem>

      <SettingItem label={t("Tasks")}>
        <IconButton
          color="inherit"
          size="small"
          sx={{ my: "2px" }}
          onClick={() => tasksRef.current?.open()}
        >
          <ArrowForward />
        </IconButton>
      </SettingItem>

      <SettingItem label={t("Miscellaneous")}>
        <IconButton
          color="inherit"
          size="small"
          sx={{ my: "2px" }}
          onClick={() => miscRef.current?.open()}
        >
          <ArrowForward />
        </IconButton>
      </SettingItem>

      <SettingItem label={t("Hotkey Setting")}>
        <IconButton
          color="inherit"
          size="small"
          sx={{ my: "2px" }}
          onClick={() => hotkeyRef.current?.open()}
        >
          <ArrowForward />
        </IconButton>
      </SettingItem>

      <SettingItem label={t("Runtime Config")}>
        <IconButton
          color="inherit"
          size="small"
          sx={{ my: "2px" }}
          onClick={() => configRef.current?.open()}
        >
          <ArrowForward />
        </IconButton>
      </SettingItem>

      <SettingItem label={t("Open App Dir")}>
        <IconButton
          color="inherit"
          size="small"
          sx={{ my: "2px" }}
          onClick={openAppDir}
        >
          <ArrowForward />
        </IconButton>
      </SettingItem>

      <SettingItem label={t("Open Core Dir")}>
        <IconButton
          color="inherit"
          size="small"
          sx={{ my: "2px" }}
          onClick={openCoreDir}
        >
          <ArrowForward />
        </IconButton>
      </SettingItem>

      <SettingItem
        label={t("Open Logs Dir")}
        extra={
          <Tooltip title={t("Collect Logs")}>
            <IconButton
              color="inherit"
              size="small"
              onClick={() => {
                collectLogs();
              }}
            >
              <IosShare
                fontSize="inherit"
                style={{ cursor: "pointer", opacity: 0.75 }}
              />
            </IconButton>
          </Tooltip>
        }
      >
        <IconButton
          color="inherit"
          size="small"
          sx={{ my: "2px" }}
          onClick={openLogsDir}
        >
          <ArrowForward />
        </IconButton>
      </SettingItem>

      {!(OS === "windows" && WIN_PORTABLE) && (
        <>
          <SettingItem
            label={t("Check for Updates")}
            extra={
              tipChips.current.onCheckUpdate && (
                <Chip label={tipChips.current.onCheckUpdate} size="small" />
              )
            }
          >
            <IconButton
              color="inherit"
              size="small"
              sx={{ my: "2px" }}
              onClick={onCheckUpdate}
              disabled={loading["onCheckUpdate"]}
            >
              {loading["onCheckUpdate"] ? (
                <CircularProgress color="inherit" size={24} />
              ) : (
                <ArrowForward />
              )}
            </IconButton>
          </SettingItem>

          <SettingItem label={t("Auto Check Updates")}>
            <GuardState
              value={!disbale_auto_check_update}
              valueProps="checked"
              onFormat={onSwitchFormat}
              onCatch={onError}
              onGuard={(e) => patchVerge({ disbale_auto_check_update: !e })}
            >
              <MDYSwitch edge="end" />
            </GuardState>
          </SettingItem>
        </>
      )}

      <SettingItem label={t("Nyanpasu Version")}>
        <Typography sx={{ py: "7px", pr: 1 }}>v{version}</Typography>
      </SettingItem>
    </SettingList>
  );
};

export default SettingVerge;
