import { useEffect, useRef, useState } from "react";
import { useTheme } from "@mui/material";
import { listen } from "@tauri-apps/api/event";
import { appWindow } from "@tauri-apps/api/window";

const maxPoint = 30;

const refLineAlpha = 1;
const refLineWidth = 2;

const upLineAlpha = 0.6;
const upLineWidth = 4;

const downLineAlpha = 1;
const downLineWidth = 4;

const defaultList = Array(maxPoint + 2).fill({ up: 0, down: 0 });

type TrafficData = { up: number; down: number };

interface Props {
  instance: React.MutableRefObject<{
    appendData: (data: TrafficData) => void;
    toggleStyle: () => void;
  }>;
}

/**
 * draw the traffic graph
 */
const TrafficGraph = (props: Props) => {
  const { instance } = props;

  const countRef = useRef(0);
  const styleRef = useRef(true);
  const listRef = useRef<TrafficData[]>(defaultList);
  const canvasRef = useRef<HTMLCanvasElement>(null!);

  const { palette } = useTheme();

  useEffect(() => {
    let timer: any;
    let cache: TrafficData | null = null;
    const zero = { up: 0, down: 0 };

    const handleData = () => {
      const data = cache ? cache : zero;
      cache = null;

      const list = listRef.current;
      if (list.length > maxPoint + 2) list.shift();
      list.push(data);
      countRef.current = 0;

      timer = setTimeout(handleData, 1000);
    };

    instance.current = {
      appendData: (data: TrafficData) => {
        cache = data;
      },
      toggleStyle: () => {
        styleRef.current = !styleRef.current;
      },
    };

    handleData();

    return () => {
      instance.current = null!;
      if (timer) clearTimeout(timer);
    };
  }, []);

  // reduce the GPU usage when hidden
  const [enablePaint, setEnablePaint] = useState(true);
  useEffect(() => {
    appWindow.isVisible().then(setEnablePaint);

    const unlistenBlur = listen("tauri://blur", async () => {
      setEnablePaint(await appWindow.isVisible());
    });

    const unlistenFocus = listen("tauri://focus", async () => {
      setEnablePaint(await appWindow.isVisible());
    });

    return () => {
      unlistenBlur.then((fn) => fn());
      unlistenFocus.then((fn) => fn());
    };
  }, []);

  useEffect(() => {
    if (!enablePaint) return;

    let raf = 0;
    const canvas = canvasRef.current!;

    if (!canvas) return;

    const context = canvas.getContext("2d")!;

    if (!context) return;

    const { primary, secondary, divider } = palette;
    const refLineColor = divider || "rgba(0, 0, 0, 0.12)";
    const upLineColor = secondary.main || "#9c27b0";
    const downLineColor = primary.main || "#5b5c9d";

    const width = canvas.width;
    const height = canvas.height;
    const dx = width / maxPoint;
    const dy = height / 7;
    const l1 = dy;
    const l2 = dy * 4;

    const countY = (v: number) => {
      const h = height;

      if (v == 0) return h - 1;
      if (v <= 10) return h - (v / 10) * dy;
      if (v <= 100) return h - (v / 100 + 1) * dy;
      if (v <= 1024) return h - (v / 1024 + 2) * dy;
      if (v <= 10240) return h - (v / 10240 + 3) * dy;
      if (v <= 102400) return h - (v / 102400 + 4) * dy;
      if (v <= 1048576) return h - (v / 1048576 + 5) * dy;
      if (v <= 10485760) return h - (v / 10485760 + 6) * dy;
      return 1;
    };

    const drawBezier = (list: number[], offset: number) => {
      const points = list.map((y, i) => [
        (dx * (i - 1) - offset + 3) | 0,
        countY(y),
      ]);

      let x = points[0][0];
      let y = points[0][1];

      context.moveTo(x, y);

      for (let i = 1; i < points.length; i++) {
        const p1 = points[i];
        const p2 = points[i + 1] || p1;

        const x1 = (p1[0] + p2[0]) / 2;
        const y1 = (p1[1] + p2[1]) / 2;

        context.quadraticCurveTo(p1[0], p1[1], x1, y1);
        x = x1;
        y = y1;
      }
    };

    const drawLine = (list: number[], offset: number) => {
      const points = list.map((y, i) => [
        (dx * (i - 1) - offset) | 0,
        countY(y),
      ]);

      context.moveTo(points[0][0], points[0][1]);

      for (let i = 1; i < points.length; i++) {
        const p = points[i];
        context.lineTo(p[0], p[1]);
      }
    };

    const drawGraph = (lastTime: number) => {
      const listUp = listRef.current.map((v) => v.up);
      const listDown = listRef.current.map((v) => v.down);
      const lineStyle = styleRef.current;

      const now = Date.now();
      const diff = now - lastTime;
      const temp = Math.min((diff / 1000) * dx + countRef.current, dx);
      const offset = countRef.current === 0 ? 0 : temp;
      countRef.current = temp;

      context.clearRect(0, 0, width, height);

      // Reference lines
      context.beginPath();
      context.globalAlpha = refLineAlpha;
      context.lineWidth = refLineWidth;
      context.strokeStyle = refLineColor;
      context.moveTo(0, l1);
      context.lineTo(width, l1);
      context.moveTo(0, l2);
      context.lineTo(width, l2);
      context.stroke();
      context.closePath();

      context.beginPath();
      context.globalAlpha = upLineAlpha;
      context.lineWidth = upLineWidth;
      context.strokeStyle = upLineColor;
      lineStyle ? drawBezier(listUp, offset) : drawLine(listUp, offset);
      context.stroke();
      context.closePath();

      context.beginPath();
      context.globalAlpha = downLineAlpha;
      context.lineWidth = downLineWidth;
      context.strokeStyle = downLineColor;
      lineStyle ? drawBezier(listDown, offset) : drawLine(listDown, offset);
      context.stroke();
      context.closePath();

      raf = requestAnimationFrame(() => drawGraph(now));
    };

    drawGraph(Date.now());

    return () => {
      cancelAnimationFrame(raf);
    };
  }, [enablePaint, palette]);

  return <canvas ref={canvasRef} style={{ width: "100%", height: "100%" }} />;
};

export default TrafficGraph;
