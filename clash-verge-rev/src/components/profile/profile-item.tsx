import dayjs from "dayjs";
import { mutate } from "swr";
import { useEffect, useState } from "react";
import { useLockFn } from "ahooks";
import { useTranslation } from "react-i18next";
import { useSortable } from "@dnd-kit/sortable";
import { CSS } from "@dnd-kit/utilities";
import {
  Box,
  Typography,
  LinearProgress,
  IconButton,
  keyframes,
  MenuItem,
  Menu,
  CircularProgress,
} from "@mui/material";
import { RefreshRounded, DragIndicator } from "@mui/icons-material";
import { useLoadingCache, useSetLoadingCache } from "@/services/states";
import { updateProfile, viewProfile } from "@/services/cmds";
import { Notice } from "@/components/base";
import { RulesEditorViewer } from "@/components/profile/rules-editor-viewer";
import { EditorViewer } from "@/components/profile/editor-viewer";
import { ProfileBox } from "./profile-box";
import parseTraffic from "@/utils/parse-traffic";
import { ConfirmViewer } from "@/components/profile/confirm-viewer";
import { open } from "@tauri-apps/api/shell";
const round = keyframes`
  from { transform: rotate(0deg); }
  to { transform: rotate(360deg); }
`;

interface Props {
  id: string;
  selected: boolean;
  activating: boolean;
  itemData: IProfileItem;
  onSelect: (force: boolean) => void;
  onEdit: () => void;
  onChange?: (prev?: string, curr?: string) => void;
  onDelete: () => void;
}

export const ProfileItem = (props: Props) => {
  const {
    selected,
    activating,
    itemData,
    onSelect,
    onEdit,
    onChange,
    onDelete,
  } = props;
  const { attributes, listeners, setNodeRef, transform, transition } =
    useSortable({ id: props.id });

  const { t } = useTranslation();
  const [anchorEl, setAnchorEl] = useState<any>(null);
  const [position, setPosition] = useState({ left: 0, top: 0 });
  const loadingCache = useLoadingCache();
  const setLoadingCache = useSetLoadingCache();

  const { uid, name = "Profile", extra, updated = 0, option } = itemData;

  // local file mode
  // remote file mode
  // remote file mode
  const hasUrl = !!itemData.url;
  const hasExtra = !!extra; // only subscription url has extra info
  const hasHome = !!itemData.home; // only subscription url has home page

  const { upload = 0, download = 0, total = 0 } = extra ?? {};
  const from = parseUrl(itemData.url);
  const description = itemData.desc;
  const expire = parseExpire(extra?.expire);
  const progress = Math.round(((download + upload) * 100) / (total + 0.1));

  const loading = loadingCache[itemData.uid] ?? false;

  // interval update fromNow field
  const [, setRefresh] = useState({});
  useEffect(() => {
    if (!hasUrl) return;

    let timer: any = null;

    const handler = () => {
      const now = Date.now();
      const lastUpdate = updated * 1000;
      // 大于一天的不管
      if (now - lastUpdate >= 24 * 36e5) return;

      const wait = now - lastUpdate >= 36e5 ? 30e5 : 5e4;

      timer = setTimeout(() => {
        setRefresh({});
        handler();
      }, wait);
    };

    handler();

    return () => {
      if (timer) clearTimeout(timer);
    };
  }, [hasUrl, updated]);

  const [fileOpen, setFileOpen] = useState(false);
  const [rulesOpen, setRulesOpen] = useState(false);
  const [proxiesOpen, setProxiesOpen] = useState(false);
  const [groupsOpen, setGroupsOpen] = useState(false);
  const [mergeOpen, setMergeOpen] = useState(false);
  const [scriptOpen, setScriptOpen] = useState(false);
  const [confirmOpen, setConfirmOpen] = useState(false);

  const onOpenHome = () => {
    setAnchorEl(null);
    open(itemData.home ?? "");
  };

  const onEditInfo = () => {
    setAnchorEl(null);
    onEdit();
  };

  const onEditFile = () => {
    setAnchorEl(null);
    setFileOpen(true);
  };

  const onEditRules = () => {
    setAnchorEl(null);
    setRulesOpen(true);
  };

  const onEditProxies = () => {
    setAnchorEl(null);
    setProxiesOpen(true);
  };

  const onEditGroups = () => {
    setAnchorEl(null);
    setGroupsOpen(true);
  };

  const onEditMerge = () => {
    setAnchorEl(null);
    setMergeOpen(true);
  };

  const onEditScript = () => {
    setAnchorEl(null);
    setScriptOpen(true);
  };

  const onForceSelect = () => {
    setAnchorEl(null);
    onSelect(true);
  };

  const onOpenFile = useLockFn(async () => {
    setAnchorEl(null);
    try {
      await viewProfile(itemData.uid);
    } catch (err: any) {
      Notice.error(err?.message || err.toString());
    }
  });

  /// 0 不使用任何代理
  /// 1 使用订阅好的代理
  /// 2 至少使用一个代理，根据订阅，如果没订阅，默认使用系统代理
  const onUpdate = useLockFn(async (type: 0 | 1 | 2) => {
    setAnchorEl(null);
    setLoadingCache((cache) => ({ ...cache, [itemData.uid]: true }));

    const option: Partial<IProfileOption> = {};

    if (type === 0) {
      option.with_proxy = false;
      option.self_proxy = false;
    } else if (type === 1) {
      // nothing
    } else if (type === 2) {
      if (itemData.option?.self_proxy) {
        option.with_proxy = false;
        option.self_proxy = true;
      } else {
        option.with_proxy = true;
        option.self_proxy = false;
      }
    }

    try {
      await updateProfile(itemData.uid, option);
      mutate("getProfiles");
    } catch (err: any) {
      const errmsg = err?.message || err.toString();
      Notice.error(
        errmsg.replace(/error sending request for url (\S+?): /, "")
      );
    } finally {
      setLoadingCache((cache) => ({ ...cache, [itemData.uid]: false }));
    }
  });

  const urlModeMenu = (
    hasHome ? [{ label: "Home", handler: onOpenHome, disabled: false }] : []
  ).concat([
    { label: "Select", handler: onForceSelect, disabled: false },
    { label: "Edit Info", handler: onEditInfo, disabled: false },
    { label: "Edit File", handler: onEditFile, disabled: false },
    {
      label: "Edit Rules",
      handler: onEditRules,
      disabled: option?.rules === null,
    },
    {
      label: "Edit Proxies",
      handler: onEditProxies,
      disabled: option?.proxies === null,
    },
    {
      label: "Edit Groups",
      handler: onEditGroups,
      disabled: option?.groups === null,
    },
    {
      label: "Edit Merge",
      handler: onEditMerge,
      disabled: option?.merge === null,
    },
    {
      label: "Edit Script",
      handler: onEditScript,
      disabled: option?.script === null,
    },
    { label: "Open File", handler: onOpenFile, disabled: false },
    { label: "Update", handler: () => onUpdate(0), disabled: false },
    { label: "Update(Proxy)", handler: () => onUpdate(2), disabled: false },
    {
      label: "Delete",
      handler: () => {
        setAnchorEl(null);
        setConfirmOpen(true);
      },
      disabled: false,
    },
  ]);
  const fileModeMenu = [
    { label: "Select", handler: onForceSelect, disabled: false },
    { label: "Edit Info", handler: onEditInfo, disabled: false },
    { label: "Edit File", handler: onEditFile, disabled: false },
    {
      label: "Edit Rules",
      handler: onEditRules,
      disabled: option?.rules === null,
    },
    {
      label: "Edit Proxies",
      handler: onEditProxies,
      disabled: option?.proxies === null,
    },
    {
      label: "Edit Groups",
      handler: onEditGroups,
      disabled: option?.groups === null,
    },
    {
      label: "Edit Merge",
      handler: onEditMerge,
      disabled: option?.merge === null,
    },
    {
      label: "Edit Script",
      handler: onEditScript,
      disabled: option?.script === null,
    },
    { label: "Open File", handler: onOpenFile, disabled: false },
    {
      label: "Delete",
      handler: () => {
        setAnchorEl(null);
        setConfirmOpen(true);
      },
      disabled: false,
    },
  ];

  const boxStyle = {
    height: 26,
    display: "flex",
    alignItems: "center",
    justifyContent: "space-between",
  };

  return (
    <Box
      sx={{
        transform: CSS.Transform.toString(transform),
        transition,
      }}
    >
      <ProfileBox
        aria-selected={selected}
        onClick={() => onSelect(false)}
        onContextMenu={(event) => {
          const { clientX, clientY } = event;
          setPosition({ top: clientY, left: clientX });
          setAnchorEl(event.currentTarget);
          event.preventDefault();
        }}
      >
        {activating && (
          <Box
            sx={{
              position: "absolute",
              display: "flex",
              justifyContent: "center",
              alignItems: "center",
              top: 10,
              left: 10,
              right: 10,
              bottom: 2,
              zIndex: 10,
              backdropFilter: "blur(2px)",
            }}
          >
            <CircularProgress color="inherit" size={20} />
          </Box>
        )}
        <Box position="relative">
          <Box sx={{ display: "flex", justifyContent: "start" }}>
            <Box
              ref={setNodeRef}
              sx={{ display: "flex", margin: "auto 0" }}
              {...attributes}
              {...listeners}
            >
              <DragIndicator
                sx={[
                  { cursor: "move", marginLeft: "-6px" },
                  ({ palette: { text } }) => {
                    return { color: text.primary };
                  },
                ]}
              />
            </Box>

            <Typography
              width="calc(100% - 36px)"
              sx={{ fontSize: "18px", fontWeight: "600", lineHeight: "26px" }}
              variant="h6"
              component="h2"
              noWrap
              title={name}
            >
              {name}
            </Typography>
          </Box>

          {/* only if has url can it be updated */}
          {hasUrl && (
            <IconButton
              title={t("Refresh")}
              sx={{
                position: "absolute",
                p: "3px",
                top: -1,
                right: -5,
                animation: loading ? `1s linear infinite ${round}` : "none",
              }}
              size="small"
              color="inherit"
              disabled={loading}
              onClick={(e) => {
                e.stopPropagation();
                onUpdate(1);
              }}
            >
              <RefreshRounded color="inherit" />
            </IconButton>
          )}
        </Box>
        {/* the second line show url's info or description */}
        <Box sx={boxStyle}>
          {
            <>
              {description ? (
                <Typography
                  noWrap
                  title={description}
                  sx={{ fontSize: "14px" }}
                >
                  {description}
                </Typography>
              ) : (
                hasUrl && (
                  <Typography noWrap title={`${t("From")} ${from}`}>
                    {from}
                  </Typography>
                )
              )}
              {hasUrl && (
                <Typography
                  noWrap
                  flex="1 0 auto"
                  fontSize={14}
                  textAlign="right"
                  title={`${t("Update Time")}: ${parseExpire(updated)}`}
                >
                  {updated > 0 ? dayjs(updated * 1000).fromNow() : ""}
                </Typography>
              )}
            </>
          }
        </Box>
        {/* the third line show extra info or last updated time */}
        {hasExtra ? (
          <Box sx={{ ...boxStyle, fontSize: 14 }}>
            <span title={t("Used / Total")}>
              {parseTraffic(upload + download)} / {parseTraffic(total)}
            </span>
            <span title={t("Expire Time")}>{expire}</span>
          </Box>
        ) : (
          <Box sx={{ ...boxStyle, fontSize: 12, justifyContent: "flex-end" }}>
            <span title={t("Update Time")}>{parseExpire(updated)}</span>
          </Box>
        )}
        <LinearProgress
          variant="determinate"
          value={progress}
          style={{ opacity: progress > 0 ? 1 : 0 }}
        />
      </ProfileBox>

      <Menu
        open={!!anchorEl}
        anchorEl={anchorEl}
        onClose={() => setAnchorEl(null)}
        anchorPosition={position}
        anchorReference="anchorPosition"
        transitionDuration={225}
        MenuListProps={{ sx: { py: 0.5 } }}
        onContextMenu={(e) => {
          setAnchorEl(null);
          e.preventDefault();
        }}
      >
        {(hasUrl ? urlModeMenu : fileModeMenu).map((item) => (
          <MenuItem
            key={item.label}
            onClick={item.handler}
            disabled={item.disabled}
            sx={[
              {
                minWidth: 120,
              },
              (theme) => {
                return {
                  color:
                    item.label === "Delete"
                      ? theme.palette.error.main
                      : undefined,
                };
              },
            ]}
            dense
          >
            {t(item.label)}
          </MenuItem>
        ))}
      </Menu>

      <EditorViewer
        mode="profile"
        property={uid}
        open={fileOpen}
        language="yaml"
        schema="clash"
        onChange={onChange}
        onClose={() => setFileOpen(false)}
      />
      <RulesEditorViewer
        profileUid={uid}
        property={option?.rules ?? ""}
        open={rulesOpen}
        onChange={onChange}
        onClose={() => setRulesOpen(false)}
      />
      <EditorViewer
        mode="profile"
        property={option?.proxies ?? ""}
        open={proxiesOpen}
        language="yaml"
        schema={undefined}
        onChange={onChange}
        onClose={() => setProxiesOpen(false)}
      />
      <EditorViewer
        mode="profile"
        property={option?.groups ?? ""}
        open={groupsOpen}
        language="yaml"
        schema={undefined}
        onChange={onChange}
        onClose={() => setGroupsOpen(false)}
      />
      <EditorViewer
        mode="profile"
        property={option?.merge ?? ""}
        open={mergeOpen}
        language="yaml"
        schema="merge"
        onChange={onChange}
        onClose={() => setMergeOpen(false)}
      />
      <EditorViewer
        mode="profile"
        property={option?.script ?? ""}
        open={scriptOpen}
        language="javascript"
        schema={undefined}
        onChange={onChange}
        onClose={() => setScriptOpen(false)}
      />
      <ConfirmViewer
        title={t("Confirm deletion")}
        message={t("This operation is not reversible")}
        open={confirmOpen}
        onClose={() => setConfirmOpen(false)}
        onConfirm={() => {
          onDelete();
          setConfirmOpen(false);
        }}
      />
    </Box>
  );
};

function parseUrl(url?: string) {
  if (!url) return "";
  const regex = /https?:\/\/(.+?)\//;
  const result = url.match(regex);
  return result ? result[1] : "local file";
}

function parseExpire(expire?: number) {
  if (!expire) return "-";
  return dayjs(expire * 1000).format("YYYY-MM-DD");
}
