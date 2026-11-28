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

export const TABS = ["DISPLAY", "GRAPHICS", "CONTROLS", "EXTRAS", "CHEATS"];

export const UPSCALE = ["SHARP PIXELS", "SMOOTH", "FSR 1"];
export const SHARPNESS = ["LOW", "MEDIUM", "HIGH"];
export const ANTIALIASING = ["OFF", "FXAA"];
export const CRT = ["OFF", "SCANLINES", "CURVED CRT"];
export const BRIGHTNESS = ["DARKER", "DARK", "NORMAL", "BRIGHT", "BRIGHTER"];
export const MAX_RENDER_SCALE = 8;
export const FOV = { min: 60, max: 120, step: 5, ps1: 90 };
export const CAMERA_DISTANCE = { min: 50, max: 200, step: 10, ps1: 100 };

export function fovLabel(n: number): string {
  return n === FOV.ps1 ? `${n}°  PS1` : `${n}°`;
}

export function cameraDistanceLabel(n: number): string {
  return n === CAMERA_DISTANCE.ps1 ? `${n}%  PS1` : `${n}%`;
}

export function renderScaleLabel(n: number): string {
  return n <= 1 ? "1X  PS1" : `${n}X`;
}

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
    { id: "tabs", kind: "tabs", label: TABS[tab], enabled: true, hint: "Left and right switch between Display, Graphics, Controls, Extras and Cheats." },
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
      {
        id: "fov",
        kind: "choice",
        label: "FIELD OF VIEW",
        value: fovLabel(s.fov),
        enabled: true,
        hint: "How wide the race camera sees. 90° is the PlayStation's; wider shows more of the track around you.",
      },
      {
        id: "camera_distance",
        kind: "choice",
        label: "CAMERA DISTANCE",
        value: cameraDistanceLabel(s.camera_distance),
        enabled: true,
        hint: "How far behind your kart the race camera sits. 100% is the PlayStation's.",
      },
    );
  } else if (tab === 1) {
    rows.push(
      {
        id: "renderscale",
        kind: "choice",
        label: "INTERNAL RESOLUTION",
        value: renderScaleLabel(s.renderscale),
        enabled: true,
        hint: "The 3D is drawn at the PlayStation's own size times this, then scaled to your window: 2X and up are sharper and cost more. A build of the game from before this setting draws at 1X.",
      },
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
  } else if (tab === 3) {
    rows.push(
      toggle("skip_intro", "SKIP INTRO", s, "Straight to the title screen: no Sony voice, no copyright hold, and the Naughty Dog crate cut short."),
      toggle("unlock_all", "UNLOCK EVERYTHING", s, "Every hidden character, Turbo Track, the locked battle arenas and the scrapbook, as the game's own unlock codes give them. Saved with your profile, as theirs are."),
    );
  } else if (tab === 4) {
    rows.push(
      toggle("cheat_wumpa", "99 WUMPA FRUIT", s, "Every kart starts each race with 99 Wumpa fruit, so every weapon is its juiced-up version. The game turns this one off in Adventure and Time Trial."),
      toggle("cheat_masks", "INFINITE MASKS", s, "An Aku Aku or Uka Uka mask whenever you want one. The game turns this one off in Adventure and Time Trial."),
      toggle("cheat_turbos", "INFINITE TURBOS", s, "Turbo boosts in your item slot that never run out. The game turns this one off in Adventure and Time Trial."),
      toggle("cheat_bombs", "INFINITE BOMBS", s, "Bombs in your item slot that never run out. The game turns this one off in Adventure and Time Trial."),
      toggle("cheat_invisible", "INFINITE INVISIBILITY", s, "The invisibility item that never runs out."),
      toggle("cheat_engine", "SUPER ENGINE", s, "Every kart gets a faster engine. The game turns this one off in Adventure and Time Trial."),
      toggle("cheat_icy", "ICY TRACKS", s, "Every track is as slippery as ice."),
      toggle("cheat_turbo_pads", "SUPER TURBO PADS", s, "Turbo pads boost harder."),
      toggle("cheat_turbo_count", "TURBO COUNTER", s, "Your turbo boosts are counted on the HUD."),
      toggle("cheat_one_lap", "ONE LAP RACES", s, "Every race is a single lap."),
      toggle("cheat_super_hard", "SUPER HARD", s, "The toughest AI the game has."),
      toggle("cheat_hard_adventure", "HARD ADVENTURE", s, "Harder AI in Adventure mode."),
    );
  } else if (tab === 2) {
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
    case "fov":
      n.fov = Math.min(FOV.max, Math.max(FOV.min, s.fov + dir * FOV.step));
      break;
    case "camera_distance":
      n.camera_distance = Math.min(CAMERA_DISTANCE.max, Math.max(CAMERA_DISTANCE.min, s.camera_distance + dir * CAMERA_DISTANCE.step));
      break;
    case "renderscale":
      n.renderscale = Math.min(MAX_RENDER_SCALE, Math.max(1, s.renderscale + dir));
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
