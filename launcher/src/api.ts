// The launcher's Rust side (src-tauri/src/main.rs), or a stand-in when the page runs in a plain browser
// (`bun run dev`, the screenshots): demo.ts plays a disc and a build so every screen can be seen.

import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { getCurrentWebview } from "@tauri-apps/api/webview";
import { open } from "@tauri-apps/plugin-dialog";

export type Phase = "idle" | "copy" | "configure" | "build" | "launch" | "playing" | "done" | "failed";

export interface Settings {
  width: number;
  height: number;
  fullscreen: boolean;
  widescreen: boolean;
  vsync: boolean;
  intscale: boolean;
  upscale: number;
  sharpness: number;
  aa: number;
  crt: number;
  brightness: number;
  texfilter: boolean;
  keyboard: boolean;
  controller: boolean;
  xbox: boolean;
  playstation: boolean;
  vibration: boolean;
}

export interface Resolution {
  w: number;
  h: number;
  aspect: string;
}

export interface DiscInfo {
  /** 0 none yet, 1 CTR NTSC-U, 2 a disc image whose region could not be read, 3 refused */
  state: number;
  bin: string;
  title: string;
  detail: string;
}

export interface View {
  mode: "package" | "dev" | "prebuilt";
  has_source: boolean;
  data_root: string;
  settings: Settings;
  resolutions: Resolution[];
  disc: DiscInfo;
  disc_source: string;
  cover_key: string;
  status: string;
  detail: string;
  phase: Phase;
  busy: boolean;
  has_log: boolean;
  has_game: boolean;
  version: string;
}

export type LauncherEvent =
  | { kind: "status"; status: string }
  | { kind: "detail"; detail: string }
  | { kind: "phase"; phase: Phase }
  | { kind: "step"; done: number; total: number }
  | { kind: "done"; ok: boolean };

export type DropKind = "over" | "drop" | "leave";

export interface Backend {
  view(): Promise<View>;
  coverImage(): Promise<string | null>;
  chooseDisc(path: string): Promise<View>;
  setCover(path: string): Promise<View>;
  saveSettings(settings: Settings): Promise<View>;
  start(play: boolean, clean: boolean): Promise<void>;
  cancel(): Promise<void>;
  showLog(): Promise<void>;
  quit(): Promise<void>;
  pickDisc(): Promise<string | null>;
  pickCover(): Promise<string | null>;
  onEvent(cb: (e: LauncherEvent) => void): void;
  onConfirmClose(cb: () => void): void;
  onDrop(cb: (kind: DropKind, paths: string[]) => void): void;
}

function picked(v: unknown): string | null {
  return typeof v === "string" && v ? v : null;
}

function tauriBackend(): Backend {
  return {
    view: () => invoke<View>("launcher_view"),
    coverImage: () => invoke<string | null>("cover_image"),
    chooseDisc: (path) => invoke<View>("choose_disc", { path }),
    setCover: (path) => invoke<View>("set_cover", { path }),
    saveSettings: (settings) => invoke<View>("save_settings", { settings }),
    start: (play, clean) => invoke<void>("start", { play, clean }),
    cancel: () => invoke<void>("cancel"),
    showLog: () => invoke<void>("show_log"),
    quit: () => invoke<void>("quit"),
    pickDisc: async () =>
      picked(
        await open({
          title: "Your Crash Team Racing disc image (.cue or .bin)",
          multiple: false,
          directory: false,
          filters: [{ name: "PS1 disc image", extensions: ["cue", "bin", "img"] }],
        }),
      ),
    pickCover: async () =>
      picked(
        await open({
          title: "A cover picture",
          multiple: false,
          directory: false,
          filters: [{ name: "Pictures", extensions: ["png", "jpg", "jpeg", "webp", "bmp", "gif"] }],
        }),
      ),
    onEvent: (cb) => void listen<LauncherEvent>("launcher", (e) => cb(e.payload)),
    onConfirmClose: (cb) => void listen("confirm-close", () => cb()),
    onDrop: (cb) =>
      void getCurrentWebview().onDragDropEvent((e) => {
        const p = e.payload;
        if (p.type === "drop") cb("drop", p.paths);
        else if (p.type === "leave") cb("leave", []);
        else cb("over", []);
      }),
  };
}

export async function backend(): Promise<Backend> {
  if ("__TAURI_INTERNALS__" in window) return tauriBackend();
  const { demoBackend } = await import("./demo");
  return demoBackend(new URLSearchParams(location.search));
}
