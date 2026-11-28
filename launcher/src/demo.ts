// A stand-in for the Rust side when the page runs in a plain browser: a disc that is found, a build that
// counts its steps, a game that "runs" for a moment. For `bun run dev` and the screenshots only.
//   ?disc=0      no disc yet          ?phase=build&pct=42   mid-build        ?page=settings&tab=1

import type { Backend, LauncherEvent, Phase, Settings, View } from "./api";

const RESOLUTIONS = [
  { w: 1280, h: 800, aspect: "16:10" },
  { w: 1280, h: 960, aspect: "4:3" },
  { w: 1366, h: 768, aspect: "16:9" },
  { w: 1440, h: 900, aspect: "16:10" },
  { w: 1600, h: 900, aspect: "16:9" },
  { w: 1680, h: 1050, aspect: "16:10" },
  { w: 1920, h: 1080, aspect: "16:9" },
  { w: 1920, h: 1200, aspect: "16:10" },
  { w: 2560, h: 1080, aspect: "21:9" },
  { w: 2560, h: 1440, aspect: "16:9" },
];

const FOUND = {
  state: 1,
  bin: "D:\\Games\\PS1\\Crash Team Racing (USA)\\Crash Team Racing (USA).bin",
  title: "Crash Team Racing (NTSC-U) found",
  detail: "SCUS-94426 \u00B7 487 MB \u00B7 Crash Team Racing (USA).bin",
};

export function demoBackend(q: URLSearchParams): Backend {
  const listeners: ((e: LauncherEvent) => void)[] = [];
  const settings: Settings = {
    width: 1920,
    height: 1080,
    fullscreen: false,
    widescreen: true,
    true_widescreen: true,
    vsync: true,
    intscale: false,
    upscale: 2,
    sharpness: 1,
    aa: 1,
    crt: 0,
    brightness: 0,
    texfilter: false,
    dither: 1,
    renderscale: 2,
    keyboard: true,
    controller: true,
    xbox: true,
    playstation: true,
    vibration: true,
    fov: 90,
    camera_distance: 100,
    skip_intro: false,
    unlock_all: false,
    cheat_wumpa: false,
    cheat_masks: false,
    cheat_turbos: false,
    cheat_bombs: false,
    cheat_invisible: false,
    cheat_engine: false,
    cheat_icy: false,
    cheat_turbo_pads: false,
    cheat_turbo_count: false,
    cheat_one_lap: false,
    cheat_super_hard: false,
    cheat_hard_adventure: false,
  };
  const hasDisc = q.get("disc") !== "0";
  const v: View = {
    mode: "package",
    has_source: true,
    data_root: "C:\\Games\\CTR Launcher",
    settings,
    resolutions: RESOLUTIONS,
    disc: hasDisc ? { ...FOUND } : { state: 0, bin: "", title: "", detail: "" },
    disc_source: hasDisc ? "D:\\Games\\PS1\\Crash Team Racing (USA)" : "",
    cover_key: "",
    status: hasDisc ? "Ready" : "Waiting for your disc",
    detail: hasDisc ? "Press Play: the game is recompiled from source, then it starts." : "",
    phase: "idle",
    busy: false,
    has_log: true,
    has_game: true,
    version: "1.0.0",
  };
  let timer = 0;
  const send = (e: LauncherEvent) => {
    if (e.kind === "status") v.status = e.status;
    if (e.kind === "detail") v.detail = e.detail;
    if (e.kind === "phase") v.phase = e.phase;
    listeners.forEach((l) => l(e));
  };
  const phase = (p: Phase) => send({ kind: "phase", phase: p });

  function build(play: boolean, from = 0) {
    v.busy = true;
    const total = 486;
    let done = from;
    phase("build");
    send({ kind: "status", status: "Recompiling the game from source" });
    timer = window.setInterval(() => {
      done = Math.min(total, done + 3);
      send({ kind: "step", done, total });
      send({ kind: "detail", detail: `Step ${done} of ${total} \u00B7 ${done < total ? "MM_TrackSelect.c" : "linking ctr_native.exe"}` });
      if (done >= total) {
        clearInterval(timer);
        if (play) {
          phase("playing");
          send({ kind: "status", status: "Playing" });
          send({ kind: "detail", detail: "The launcher comes back when you quit the game." });
          timer = window.setTimeout(() => finish(true, "Thanks for playing", "Press Play to race again."), 2500);
        } else finish(true, "The game is rebuilt", "Press Play to start it.");
      }
    }, 40);
  }

  function finish(ok: boolean, status: string, detail: string) {
    v.busy = false;
    send({ kind: "status", status });
    send({ kind: "detail", detail });
    phase(ok ? "done" : "failed");
    send({ kind: "done", ok });
  }

  if (q.get("phase") === "build") {
    window.setTimeout(() => build(true, Math.round((486 * Number(q.get("pct") ?? 40)) / 100)), 50);
  }

  return {
    view: async () => structuredClone(v),
    coverImage: async () => q.get("cover"),
    chooseDisc: async () => {
      v.disc = { ...FOUND };
      v.status = "Ready";
      return structuredClone(v);
    },
    setCover: async () => structuredClone(v),
    saveSettings: async (s) => {
      v.settings = { ...s };
      return structuredClone(v);
    },
    start: async (play) => build(play),
    cancel: async () => {
      clearInterval(timer);
      finish(false, "Stopped", "The build was cancelled.");
    },
    showLog: async () => {},
    quit: async () => {},
    pickDisc: async () => "D:\\Games\\PS1\\Crash Team Racing (USA)\\Crash Team Racing (USA).cue",
    pickCover: async () => null,
    onEvent: (cb) => void listeners.push(cb),
    onConfirmClose: () => {},
    onDrop: () => {},
  };
}
