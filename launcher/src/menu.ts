// What each menu holds and how it moves: no DOM here, so bun test can hold it to the rules.

import type { Phase, Resolution, Settings, View } from "./api";

export type RowKind = "action" | "toggle" | "choice" | "tabs";

export interface Row {
  id: string;
  kind: RowKind;
  label: string;
  /** a toggle's ON/OFF, a choice's current value */
  value?: string;
  enabled: boolean;
  hint: string;
}

export const TABS = ["DISPLAY", "GRAPHICS", "CONTROLS"];

export const UPSCALE = ["SHARP PIXELS", "SMOOTH", "FSR 1"];
export const SHARPNESS = ["LOW", "MEDIUM", "HIGH"];
export const ANTIALIASING = ["OFF", "FXAA"];
export const CRT = ["OFF", "SCANLINES", "CURVED CRT"];
export const BRIGHTNESS = ["DARKER", "DARK", "NORMAL", "BRIGHT", "BRIGHTER"];

const UPSCALE_HINT = [
  "Sharp pixels: the original PlayStation picture, every pixel kept square and crisp.",
  "Smooth: the picture is scaled up with bilinear filtering. Softer edges, no blocky pixels.",
  "AMD FSR 1: edge-aware upscaling to your resolution, then contrast-adaptive sharpening. The crispest picture at 1440p.",
];

export function playLabel(v: View): string {
  if (v.busy && v.phase === "playing") return "RACING";
  if (v.busy && v.phase === "launch") return "STARTING";
  return v.busy ? "CANCEL" : "PLAY";
}

export function homeRows(v: View): Row[] {
  const racing = v.busy && (v.phase === "playing" || v.phase === "launch");
  return [
    { id: "play", kind: "action", label: playLabel(v), enabled: !racing, hint: "" },
    { id: "disc", kind: "action", label: "CHOOSE DISC", enabled: !v.busy, hint: "" },
    { id: "settings", kind: "action", label: "SETTINGS", enabled: true, hint: "" },
    { id: "rebuild", kind: "action", label: "REBUILD FROM SCRATCH", enabled: !v.busy && v.has_source, hint: "" },
    { id: "log", kind: "action", label: "SHOW LOG", enabled: v.has_log, hint: "" },
    { id: "quit", kind: "action", label: "QUIT", enabled: true, hint: "" },
  ];
}

export function resolutionIndex(s: Settings, list: Resolution[]): number {
  return list.findIndex((r) => r.w === s.width && r.h === s.height);
}

function resolutionLabel(s: Settings, list: Resolution[]): string {
  const r = list[resolutionIndex(s, list)];
  return `${s.width} × ${s.height}` + (r ? `  ${r.aspect}` : "");
}

const onOff = (b: boolean) => (b ? "ON" : "OFF");

function toggle(id: keyof Settings, label: string, s: Settings, hint: string, enabled = true): Row {
  return { id, kind: "toggle", label, value: onOff(Boolean(s[id])), enabled, hint };
}

export function settingsRows(v: View, tab: number): Row[] {
  const s = v.settings;
  const rows: Row[] = [
    { id: "tabs", kind: "tabs", label: TABS[tab], enabled: true, hint: "Left and right switch between Display, Graphics and Controls." },
  ];
  if (tab === 0) {
    rows.push(
      {
        id: "resolution",
        kind: "choice",
        label: "RESOLUTION",
        value: resolutionLabel(s, v.resolutions),
        enabled: true,
        hint: "The game's window: 16:9, 16:10, 4:3 or 21:9. A size set by hand in ctr_native.ini stays until you pick another.",
      },
      toggle("fullscreen", "FULLSCREEN", s, "The whole screen, no window border. Alt + Enter switches while you play."),
      toggle("widescreen", "WIDESCREEN", s, "Fill a 16:9 or 16:10 screen. Off: the original 4:3 picture with bars."),
      toggle("vsync", "VSYNC", s, "No tearing: every frame waits for the screen's refresh."),
      toggle("intscale", "PIXEL-PERFECT", s, "Whole-number scaling: every scanline the same height."),
    );
  } else if (tab === 1) {
    rows.push(
      { id: "upscale", kind: "choice", label: "UPSCALING", value: UPSCALE[s.upscale], enabled: true, hint: UPSCALE_HINT[s.upscale] },
      {
        id: "sharpness",
        kind: "choice",
        label: "FSR SHARPNESS",
        value: SHARPNESS[s.sharpness],
        enabled: s.upscale === 2,
        hint: "How strongly FSR sharpens after it upscales. Pick FSR 1 under Upscaling to use it.",
      },
      { id: "aa", kind: "choice", label: "ANTI-ALIASING", value: ANTIALIASING[s.aa], enabled: true, hint: "FXAA smooths jagged polygon edges." },
      {
        id: "crt",
        kind: "choice",
        label: "CRT EFFECT",
        value: CRT[s.crt],
        enabled: true,
        hint: "The look of a 1999 TV: scanlines, and with Curved CRT an aperture mask, a curved screen and dark corners.",
      },
      { id: "brightness", kind: "choice", label: "BRIGHTNESS", value: BRIGHTNESS[s.brightness + 2], enabled: true, hint: "Lifts or lowers the whole picture." },
      toggle("texfilter", "TEXTURE FILTER", s, "Smooths the 3D textures (the PlayStation drew them unfiltered)."),
    );
  } else {
    rows.push(
      toggle("keyboard", "KEYBOARD", s, "Arrows steer · C gas · X brake · V item · Shift hop · Enter start · Space select."),
      toggle("controller", "CONTROLLERS", s, "Gamepads, up to four players. Plug one in any time."),
      toggle("xbox", "XBOX PADS", s, "Xbox 360, One and Series pads (XInput).", s.controller),
      toggle("playstation", "PLAYSTATION PADS", s, "DualShock 3 and 4, DualSense. ✕ accelerate, □ brake, ○ item, R1 hop.", s.controller),
      toggle("vibration", "VIBRATION", s, "Rumble on pads that have it.", s.controller),
    );
  }
  rows.push({ id: "back", kind: "action", label: "BACK", enabled: true, hint: "Back to Play. Settings are saved as you change them." });
  return rows;
}

/** The next selectable row from `sel` going `dir`, wrapping like the game's menus; `sel` when there is none. */
export function stepRow(rows: Row[], sel: number, dir: 1 | -1): number {
  for (let i = 1; i <= rows.length; i++) {
    const at = (((sel + dir * i) % rows.length) + rows.length) % rows.length;
    if (rows[at].enabled) return at;
  }
  return sel;
}

/** `sel` kept in range and on a selectable row. */
export function fixRow(rows: Row[], sel: number): number {
  if (rows.length === 0) return 0;
  const at = Math.min(Math.max(sel, 0), rows.length - 1);
  return rows[at].enabled ? at : stepRow(rows, at, 1);
}

function wrap(i: number, n: number): number {
  return ((i % n) + n) % n;
}

/** A toggle flipped or a choice moved by `dir`; something always drives the kart. */
export function change(s: Settings, id: string, dir: 1 | -1, list: Resolution[]): Settings {
  const n: Settings = { ...s };
  switch (id) {
    case "resolution": {
      const at = resolutionIndex(s, list);
      const next = at < 0 ? (dir > 0 ? 0 : list.length - 1) : wrap(at + dir, list.length);
      n.width = list[next].w;
      n.height = list[next].h;
      break;
    }
    case "upscale":
      n.upscale = wrap(s.upscale + dir, UPSCALE.length);
      break;
    case "sharpness":
      n.sharpness = wrap(s.sharpness + dir, SHARPNESS.length);
      break;
    case "aa":
      n.aa = wrap(s.aa + dir, ANTIALIASING.length);
      break;
    case "crt":
      n.crt = wrap(s.crt + dir, CRT.length);
      break;
    case "brightness":
      n.brightness = Math.min(2, Math.max(-2, s.brightness + dir));
      break;
    default: {
      const key = id as keyof Settings;
      if (typeof s[key] === "boolean") (n as unknown as Record<string, boolean>)[key] = !s[key];
    }
  }
  if (!n.keyboard && !n.controller) n.keyboard = true;
  return n;
}

// ---------------------------------------------------------------------------------------------------------
// The progress bar: copy, configure (the first 6%), then the build's steps, with a gentle creep while one
// long step runs (main.c alone takes a while) so the bar never looks stuck.

export interface Progress {
  phase: Phase;
  done: number;
  total: number;
  stepTick: number;
  avgStepMs: number;
  shown: number;
}

export function newProgress(phase: Phase = "idle"): Progress {
  return { phase, done: 0, total: 0, stepTick: 0, avgStepMs: 400, shown: 0 };
}

export function targetPct(p: Progress, now: number): number {
  switch (p.phase) {
    case "copy":
      return p.total > 0 ? (100 * p.done) / p.total : 0;
    case "configure":
      return 6 * (p.total > 0 ? p.done / p.total : 0);
    case "build": {
      if (p.total <= 0) return 6;
      const base = 6 + (94 * p.done) / p.total;
      const next = 6 + (94 * Math.min(p.done + 1, p.total)) / p.total;
      let f = (now - p.stepTick) / (p.avgStepMs * 3 + 1);
      f = f / (1 + f); // approaches 1, never reaches it
      return base + (next - base) * 0.9 * f;
    }
    case "launch":
    case "playing":
    case "done":
      return 100;
    default:
      return p.shown;
  }
}

export function onStep(p: Progress, done: number, total: number, now: number): void {
  if (p.phase === "build" && done > p.done && p.done > 0) {
    const ms = (now - p.stepTick) / (done - p.done);
    p.avgStepMs = p.avgStepMs * 0.8 + ms * 0.2;
  }
  if (done !== p.done || total !== p.total) p.stepTick = now;
  p.done = done;
  p.total = total;
}

export function onPhase(p: Progress, phase: Phase, now: number): void {
  p.phase = phase;
  p.stepTick = now;
  p.done = 0;
  p.total = 0;
  if (phase === "copy" || phase === "configure") p.shown = 0;
}

/** One animation tick: the bar eases toward where the work is, and never goes back. */
export function tick(p: Progress, now: number): number {
  const target = Math.max(targetPct(p, now), p.shown);
  p.shown += (target - p.shown) * 0.25;
  if (target - p.shown < 0.05) p.shown = target;
  return p.shown;
}
