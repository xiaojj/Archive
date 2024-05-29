import {
  alpha,
  Box,
  List,
  ListItem,
  Paper,
  Typography,
  useTheme,
} from "@mui/material";
import { BaseCard } from "@nyanpasu/ui";
import { useTranslation } from "react-i18next";
import LogoSvg from "@/assets/image/logo.svg?react";
import { version } from "~/package.json";
import LoadingButton from "@mui/lab/LoadingButton";
import { useEffect, useState } from "react";
import { useLockFn } from "ahooks";
import { checkUpdate } from "@tauri-apps/api/updater";
import { useMessage } from "@/hooks/use-notification";
import { useNyanpasu } from "@nyanpasu/interface";
import { LabelSwitch } from "./modules/clash-field";

const AutoCheckUpdate = () => {
  const { t } = useTranslation();

  const { nyanpasuConfig, setNyanpasuConfig } = useNyanpasu();

  return (
    <LabelSwitch
      label={t("Auto Check Updates")}
      checked={nyanpasuConfig?.enable_auto_check_update}
      onChange={() =>
        setNyanpasuConfig({
          enable_auto_check_update: !nyanpasuConfig?.enable_auto_check_update,
        })
      }
    />
  );
};

export const SettingNyanpasuVersion = () => {
  const { t } = useTranslation();

  const { palette } = useTheme();

  const [loading, setLoading] = useState(false);

  const { nyanpasuConfig, setNyanpasuConfig } = useNyanpasu();

  const onCheckUpdate = useLockFn(async () => {
    try {
      setLoading(true);

      const info = await checkUpdate();

      if (!info?.shouldUpdate) {
        useMessage(t("No update available."), {
          title: t("Info"),
          type: "info",
        });
      } else {
        useMessage(`New Version: ${info.manifest?.version}`, {
          title: t("New Version"),
          type: "info",
        });
      }
    } catch (e) {
      useMessage(
        "Update check failed. Please verify your network connection.",
        {
          title: t("Error"),
          type: "error",
        },
      );
    } finally {
      setLoading(false);
    }
  });

  return (
    <BaseCard label={t("Nyanpasu Version")}>
      <List disablePadding>
        <ListItem sx={{ pl: 0, pr: 0 }}>
          <Paper
            elevation={0}
            sx={{
              mt: 1,
              padding: 2,
              backgroundColor: alpha(palette.primary.main, 0.1),
              borderRadius: 6,
              width: "100%",
            }}
          >
            <Box
              display="flex"
              flexDirection="column"
              alignItems="center"
              gap={2}
            >
              <LogoSvg className="w-32 h-32" />

              <Typography fontWeight={700} noWrap>
                {"Clash Nyanpasu~(∠・ω< )⌒☆"}​
              </Typography>

              <Typography>
                <b>Version: </b>v{version}
              </Typography>
            </Box>
          </Paper>
        </ListItem>

        <div className="mt-1 mb-1">
          <AutoCheckUpdate />
        </div>

        <ListItem sx={{ pl: 0, pr: 0 }}>
          <LoadingButton
            variant="contained"
            size="large"
            loading={loading}
            onClick={onCheckUpdate}
            sx={{ width: "100%" }}
          >
            {t("Check for Updates")}
          </LoadingButton>
        </ListItem>
      </List>
    </BaseCard>
  );
};

export default SettingNyanpasuVersion;
