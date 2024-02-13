import LogoSvg from "@/assets/image/logo.svg?react";
import { LayoutControl } from "@/components/layout/layout-control";
import { LayoutItem } from "@/components/layout/layout-item";
import { LayoutTraffic } from "@/components/layout/layout-traffic";
import { UpdateButton } from "@/components/layout/update-button";
import { useCustomTheme } from "@/components/layout/use-custom-theme";
import { NotificationType, useNotification } from "@/hooks/use-notification";
import { useVerge } from "@/hooks/use-verge";
import { getAxios } from "@/services/api";
import getSystem from "@/utils/get-system";
import { List, Paper, ThemeProvider, alpha } from "@mui/material";
import { listen } from "@tauri-apps/api/event";
import { appWindow } from "@tauri-apps/api/window";
import dayjs from "dayjs";
import "dayjs/locale/ru";
import "dayjs/locale/zh-cn";
import relativeTime from "dayjs/plugin/relativeTime";
import { AnimatePresence } from "framer-motion";
import i18next from "i18next";
import React, { useEffect } from "react";
import { useTranslation } from "react-i18next";
import { useLocation, useRoutes } from "react-router-dom";
import { SWRConfig, mutate } from "swr";
import { routers } from "./_routers";

dayjs.extend(relativeTime);

const OS = getSystem();

export default function Layout() {
  const { t } = useTranslation();

  const { theme } = useCustomTheme();

  const { verge } = useVerge();
  const { theme_blur, language } = verge || {};

  const location = useLocation();
  const routes = useRoutes(routers);
  if (!routes) return null;

  useEffect(() => {
    window.addEventListener("keydown", (e) => {
      // macOS有cmd+w
      if (e.key === "Escape" && OS !== "macos") {
        appWindow.close();
      }
    });

    listen("verge://refresh-clash-config", async () => {
      // the clash info may be updated
      await getAxios(true);
      mutate("getProxies");
      mutate("getVersion");
      mutate("getClashConfig");
      mutate("getProviders");
    });

    // update the verge config
    listen("verge://refresh-verge-config", () => mutate("getVergeConfig"));

    // 设置提示监听
    listen("verge://notice-message", ({ payload }) => {
      const [status, msg] = payload as [string, string];
      switch (status) {
        case "set_config::ok":
          useNotification({
            title: t("Success"),
            body: "Refresh Clash Config",
            type: NotificationType.Success,
          });
          break;
        case "set_config::error":
          useNotification({
            title: t("Error"),
            body: msg,
            type: NotificationType.Error,
          });
          break;
        default:
          break;
      }
    });

    listen("verge://mutate-proxies", () => {
      mutate("getProxies");
      mutate("getProviders");
    });

    setTimeout(() => {
      appWindow.show();
      appWindow.unminimize();
      appWindow.setFocus();
    }, 50);
  }, []);

  useEffect(() => {
    if (language) {
      dayjs.locale(language === "zh" ? "zh-cn" : language);
      i18next.changeLanguage(language);
    }
  }, [language]);

  return (
    <SWRConfig value={{ errorRetryCount: 3 }}>
      <ThemeProvider theme={theme}>
        <Paper
          square
          elevation={0}
          className={`${OS} layout`}
          onPointerDown={(e: any) => {
            if (e.target?.dataset?.windrag) appWindow.startDragging();
          }}
          onContextMenu={(e) => {
            // only prevent it on Windows
            const validList = ["input", "textarea"];
            const target = e.currentTarget;
            if (
              OS === "windows" &&
              !(
                validList.includes(target.tagName.toLowerCase()) ||
                target.isContentEditable
              )
            ) {
              e.preventDefault();
            }
          }}
          sx={[
            ({ palette }) => ({
              bgcolor: alpha(palette.background.paper, theme_blur ? 0.8 : 1),
            }),
          ]}
        >
          <div className="layout__left" data-windrag>
            <div className="the-logo" data-windrag>
              <LogoSvg />

              {!(OS === "windows" && WIN_PORTABLE) && (
                <UpdateButton className="the-newbtn" />
              )}
            </div>

            <List className="the-menu">
              {routers.map((router) => (
                <LayoutItem key={router.label} to={router.path}>
                  {t(router.label)}
                </LayoutItem>
              ))}
            </List>

            <div className="the-traffic" data-windrag>
              <LayoutTraffic />
            </div>
          </div>

          <div className="layout__right">
            {OS === "windows" && (
              <div className="the-bar">
                <LayoutControl />
              </div>
            )}

            <div className="drag-mask" data-windrag />

            <AnimatePresence mode="wait">
              {React.cloneElement(routes, { key: location.pathname })}
            </AnimatePresence>
          </div>
        </Paper>
      </ThemeProvider>
    </SWRConfig>
  );
}
