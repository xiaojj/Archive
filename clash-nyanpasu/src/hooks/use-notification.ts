import { Notice } from "@/components/base";
import { isPortable } from "@/services/cmds";
import {
  Options,
  isPermissionGranted,
  requestPermission,
  sendNotification,
} from "@tauri-apps/api/notification";
let permissionGranted: boolean | null = null;
let portable: boolean | null = null;

const checkPermission = async () => {
  if (permissionGranted == null) {
    permissionGranted = await isPermissionGranted();
  }
  if (!permissionGranted) {
    const permission = await requestPermission();
    permissionGranted = permission === "granted";
  }
  return permissionGranted;
};

export type NotificationOptions = {
  title: string;
  body?: string;
  type?: NotificationType;
};

export enum NotificationType {
  Success = "success",
  Info = "info",
  // Warn = "warn",
  Error = "error",
}

export const useNotification = async ({
  title,
  body,
  type = NotificationType.Info,
}: NotificationOptions) => {
  if (!title) {
    throw new Error("missing message argument!");
  }
  if (portable === null) {
    portable = await isPortable();
  }
  const permissionGranted = portable || (await checkPermission());
  if (portable || !permissionGranted) {
    // fallback to mui notification
    Notice[type](`${title} ${body ? `: ${body}` : ""}}`);
    // throw new Error("notification permission not granted!");
    return;
  }
  const options: Options = {
    title: title,
  };
  if (body) options.body = body;
  sendNotification(options);
};
