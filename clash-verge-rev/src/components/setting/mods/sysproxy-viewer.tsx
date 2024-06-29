import { forwardRef, useImperativeHandle, useState } from "react";
import { useLockFn } from "ahooks";
import { useTranslation } from "react-i18next";
import {
  InputAdornment,
  List,
  ListItem,
  ListItemText,
  styled,
  TextField,
  Typography,
  Button,
} from "@mui/material";
import { useVerge } from "@/hooks/use-verge";
import { getSystemProxy, getAutotemProxy } from "@/services/cmds";
import { BaseDialog, DialogRef, Notice, Switch } from "@/components/base";
import { Edit } from "@mui/icons-material";
import { EditorViewer } from "@/components/profile/editor-viewer";
import { BaseFieldset } from "@/components/base/base-fieldset";
import getSystem from "@/utils/get-system";
import { TooltipIcon } from "@/components/base/base-tooltip-icon";
const DEFAULT_PAC = `function FindProxyForURL(url, host) {
  return "PROXY 127.0.0.1:%mixed-port%; SOCKS5 127.0.0.1:%mixed-port%; DIRECT;";
}`;

export const SysproxyViewer = forwardRef<DialogRef>((props, ref) => {
  const { t } = useTranslation();
  let validReg;
  if (getSystem() === "windows") {
    validReg =
      /^((\*\.)?([a-zA-Z0-9-]+\.)+[a-zA-Z]{2,}|(\d{1,3}\.){1,3}\d{1,3}|\d{1,3}\.\d{1,3}\.\d{1,3}\.\*|\d{1,3}\.\d{1,3}\.\*|\d{1,3}\.\*|([a-fA-F0-9:]+:+)+[a-fA-F0-9]+|localhost|<local>)(;((\*\.)?([a-zA-Z0-9-]+\.)+[a-zA-Z]{2,}|(\d{1,3}\.){1,3}\d{1,3}|\d{1,3}\.\d{1,3}\.\d{1,3}\.\*|\d{1,3}\.\d{1,3}\.\*|\d{1,3}\.\*|([a-fA-F0-9:]+:+)+[a-fA-F0-9]+|localhost|<local>))*;?$/;
  } else {
    validReg =
      /^((\*\.)?([a-zA-Z0-9-]+\.)+[a-zA-Z]{2,}|(\d{1,3}\.){1,3}\d{1,3}(\/\d{1,2}|\/3[0-2])?|\d{1,3}\.\d{1,3}\.\d{1,3}\.\*(\/\d{1,2}|\/3[0-2])?|\d{1,3}\.\d{1,3}\.\*(\/\d{1,2}|\/3[0-2])?|\d{1,3}\.\*(\/\d{1,2}|\/3[0-2])?|([a-fA-F0-9:]+:+)+[a-fA-F0-9]+(\/\d{1,3})?|localhost|<local>)(,((\*\.)?([a-zA-Z0-9-]+\.)+[a-zA-Z]{2,}|(\d{1,3}\.){1,3}\d{1,3}(\/\d{1,2}|\/3[0-2])?|\d{1,3}\.\d{1,3}\.\d{1,3}\.\*(\/\d{1,2}|\/3[0-2])?|\d{1,3}\.\d{1,3}\.\*(\/\d{1,2}|\/3[0-2])?|\d{1,3}\.\*(\/\d{1,2}|\/3[0-2])?|([a-fA-F0-9:]+:+)+[a-fA-F0-9]+(\/\d{1,3})?|localhost|<local>))*,?$/;
  }

  const [open, setOpen] = useState(false);
  const [editorOpen, setEditorOpen] = useState(false);
  const { verge, patchVerge } = useVerge();

  type SysProxy = Awaited<ReturnType<typeof getSystemProxy>>;
  const [sysproxy, setSysproxy] = useState<SysProxy>();

  type AutoProxy = Awaited<ReturnType<typeof getAutotemProxy>>;
  const [autoproxy, setAutoproxy] = useState<AutoProxy>();

  const {
    enable_system_proxy: enabled,
    proxy_auto_config,
    pac_file_content,
    enable_proxy_guard,
    system_proxy_bypass,
    proxy_guard_duration,
  } = verge ?? {};

  const [value, setValue] = useState({
    guard: enable_proxy_guard,
    bypass: system_proxy_bypass,
    duration: proxy_guard_duration ?? 10,
    pac: proxy_auto_config,
    pac_content: pac_file_content ?? DEFAULT_PAC,
  });

  useImperativeHandle(ref, () => ({
    open: () => {
      setOpen(true);
      setValue({
        guard: enable_proxy_guard,
        bypass: system_proxy_bypass,
        duration: proxy_guard_duration ?? 10,
        pac: proxy_auto_config,
        pac_content: pac_file_content ?? DEFAULT_PAC,
      });
      getSystemProxy().then((p) => setSysproxy(p));
      getAutotemProxy().then((p) => setAutoproxy(p));
    },
    close: () => setOpen(false),
  }));

  const onSave = useLockFn(async () => {
    if (value.duration < 1) {
      Notice.error(t("Proxy Daemon Duration Cannot be Less than 1 Second"));
      return;
    }

    const patch: Partial<IVergeConfig> = {};

    if (value.guard !== enable_proxy_guard) {
      patch.enable_proxy_guard = value.guard;
    }
    if (value.duration !== proxy_guard_duration) {
      patch.proxy_guard_duration = value.duration;
    }
    if (value.bypass !== system_proxy_bypass) {
      patch.system_proxy_bypass = value.bypass;
    }
    if (value.pac !== proxy_auto_config) {
      patch.proxy_auto_config = value.pac;
    }
    if (value.pac_content !== pac_file_content) {
      patch.pac_file_content = value.pac_content;
    }
    if (value.bypass && !validReg.test(value.bypass)) {
      Notice.error(t("Invalid Bypass Format"));
      return;
    }
    try {
      await patchVerge(patch);
      setOpen(false);
    } catch (err: any) {
      Notice.error(err.message || err.toString());
    }
  });

  return (
    <BaseDialog
      open={open}
      title={t("System Proxy Setting")}
      contentSx={{ width: 450, maxHeight: 565 }}
      okBtn={t("Save")}
      cancelBtn={t("Cancel")}
      onClose={() => setOpen(false)}
      onCancel={() => setOpen(false)}
      onOk={onSave}
    >
      <List>
        <BaseFieldset label={t("Current System Proxy")} padding="15px 10px">
          <FlexBox>
            <Typography className="label">{t("Enable status")}</Typography>
            <Typography className="value">
              {value.pac
                ? autoproxy?.enable
                  ? t("Enabled")
                  : t("Disabled")
                : sysproxy?.enable
                ? t("Enabled")
                : t("Disabled")}
            </Typography>
          </FlexBox>
          {!value.pac && (
            <>
              <FlexBox>
                <Typography className="label">{t("Server Addr")}</Typography>
                <Typography className="value">
                  {sysproxy?.server ? sysproxy.server : t("Not available")}
                </Typography>
              </FlexBox>
            </>
          )}
          {value.pac && (
            <FlexBox>
              <Typography className="label">{t("PAC URL")}</Typography>
              <Typography className="value">{autoproxy?.url || "-"}</Typography>
            </FlexBox>
          )}
        </BaseFieldset>
        <ListItem sx={{ padding: "5px 2px" }}>
          <ListItemText primary={t("Use PAC Mode")} />
          <Switch
            edge="end"
            disabled={!enabled}
            checked={value.pac}
            onChange={(_, e) => setValue((v) => ({ ...v, pac: e }))}
          />
        </ListItem>

        <ListItem sx={{ padding: "5px 2px" }}>
          <ListItemText
            primary={t("Proxy Guard")}
            sx={{ maxWidth: "fit-content" }}
          />
          <TooltipIcon title={t("Proxy Guard Info")} />
          <Switch
            edge="end"
            disabled={!enabled}
            checked={value.guard}
            onChange={(_, e) => setValue((v) => ({ ...v, guard: e }))}
            sx={{ marginLeft: "auto" }}
          />
        </ListItem>

        <ListItem sx={{ padding: "5px 2px" }}>
          <ListItemText primary={t("Guard Duration")} />
          <TextField
            disabled={!enabled}
            size="small"
            value={value.duration}
            sx={{ width: 100 }}
            InputProps={{
              endAdornment: <InputAdornment position="end">s</InputAdornment>,
            }}
            onChange={(e) => {
              setValue((v) => ({
                ...v,
                duration: +e.target.value.replace(/\D/, ""),
              }));
            }}
          />
        </ListItem>
        {!value.pac && (
          <>
            <ListItemText primary={t("Proxy Bypass")} />
            <TextField
              error={value.bypass ? !validReg.test(value.bypass) : false}
              disabled={!enabled}
              size="small"
              autoComplete="off"
              multiline
              rows={4}
              sx={{ width: "100%" }}
              value={value.bypass}
              onChange={(e) => {
                setValue((v) => ({ ...v, bypass: e.target.value }));
              }}
            />
            <ListItemText primary={t("Bypass")} />
            <FlexBox>
              <TextField
                disabled={true}
                size="small"
                autoComplete="off"
                multiline
                rows={4}
                sx={{ width: "100%" }}
                value={sysproxy?.bypass || "-"}
              />
            </FlexBox>
          </>
        )}
        {value.pac && (
          <>
            <ListItem sx={{ padding: "5px 2px", alignItems: "start" }}>
              <ListItemText
                primary={t("PAC Script Content")}
                sx={{ padding: "3px 0" }}
              />
              <Button
                startIcon={<Edit />}
                variant="outlined"
                onClick={() => {
                  setEditorOpen(true);
                }}
              >
                {t("Edit")} PAC
              </Button>
              <EditorViewer
                title={`${t("Edit")} PAC`}
                mode="text"
                property={value.pac_content ?? ""}
                open={editorOpen}
                language="javascript"
                onChange={(_prev, curr) => {
                  let pac = DEFAULT_PAC;
                  if (curr && curr.trim().length > 0) {
                    pac = curr;
                  }
                  setValue((v) => ({ ...v, pac_content: pac }));
                }}
                onClose={() => {
                  setEditorOpen(false);
                }}
              />
            </ListItem>
          </>
        )}
      </List>
    </BaseDialog>
  );
});

const FlexBox = styled("div")`
  display: flex;
  margin-top: 4px;

  .label {
    flex: none;
    //width: 85px;
  }
`;
