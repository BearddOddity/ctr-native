import { describe, expect, test } from "bun:test";
import type { Settings, View } from "./api";
import { change, fixRow, homeRows, newProgress, onPhase, onStep, playLabel, settingsRows, stepRow, targetPct, tick } from "./menu";

const RES = [
  { w: 1280, h: 800, aspect: "16:10" },
  { w: 1920, h: 1080, aspect: "16:9" },
  { w: 2560, h: 1440, aspect: "16:9" },
];

function settings(over: Partial<Settings> = {}): Settings {
  return {
    width: 1920,
    height: 1080,
    fullscreen: false,
    widescreen: true,
    true_widescreen: true,
    vsync: true,
    intscale: false,
    upscale: 0,
    sharpness: 1,
    aa: 0,
    crt: 0,
    brightness: 0,
    texfilter: false,
    dither: 1,
    renderscale: 1,
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
    ...over,
  };
}

function view(over: Partial<View> = {}): View {
  return {
    mode: "package",
    has_source: true,
    data_root: "C:\\CTR",
    settings: settings(),
    resolutions: RES,
    disc: { state: 1, bin: "ctr.bin", title: "found", detail: "" },
    disc_source: "",
    cover_key: "",
    status: "",
    detail: "",
    phase: "idle",
    busy: false,
    has_log: false,
    has_game: false,
    version: "1.0.0",
    ...over,
  };
}

describe("home menu", () => {
  test("Play turns into Cancel while building and is greyed out while racing", () => {
    expect(playLabel(view())).toBe("PLAY");
    expect(playLabel(view({ busy: true, phase: "build" }))).toBe("CANCEL");
    expect(playLabel(view({ busy: true, phase: "playing" }))).toBe("RACING");
    const racing = homeRows(view({ busy: true, phase: "playing" }));
    expect(racing[0].enabled).toBe(false);
    expect(racing.find((r) => r.id === "disc")!.enabled).toBe(false);
  });

  test("rebuild needs the source and an idle launcher; the log needs a log", () => {
    expect(homeRows(view({ has_source: false })).find((r) => r.id === "rebuild")!.enabled).toBe(false);
    expect(homeRows(view()).find((r) => r.id === "log")!.enabled).toBe(false);
    expect(homeRows(view({ has_log: true })).find((r) => r.id === "log")!.enabled).toBe(true);
  });
});

describe("moving through a menu", () => {
  const rows = homeRows(view()); // play, disc, settings, rebuild, log (off), quit

  test("skips a row that can't be picked and wraps at both ends", () => {
    expect(stepRow(rows, 3, 1)).toBe(5);
    expect(stepRow(rows, 5, 1)).toBe(0);
    expect(stepRow(rows, 0, -1)).toBe(5);
    expect(stepRow(rows, 5, -1)).toBe(3);
  });

  test("a selection that went out of range or onto a greyed row is moved", () => {
    expect(fixRow(rows, 99)).toBe(5);
    expect(fixRow(rows, 4)).toBe(5);
    expect(fixRow(rows, -3)).toBe(0);
    expect(fixRow([], 2)).toBe(0);
  });
});

describe("settings", () => {
  test("each tab starts with the tabs row and ends with Back", () => {
    for (const tab of [0, 1, 2, 3, 4]) {
      const rows = settingsRows(view(), tab);
      expect(rows[0].kind).toBe("tabs");
      expect(rows[rows.length - 1].id).toBe("back");
      for (const r of rows) expect(r.hint.length).toBeGreaterThan(0);
    }
  });

  test("dithering cycles off, PS1, fine", () => {
    expect(settingsRows(view(), 1).find((r) => r.id === "dither")!.value).toBe("PS1");
    expect(change(settings(), "dither", 1, RES).dither).toBe(2);
    expect(change(settings(), "dither", -1, RES).dither).toBe(0);
  });

  test("widescreen goes 4:3, stretched, true", () => {
    expect(settingsRows(view(), 0).find((r) => r.id === "widescreen")!.value).toBe("TRUE WIDESCREEN");
    const off = change(settings(), "widescreen", 1, RES);
    expect([off.widescreen, off.true_widescreen]).toEqual([false, true]);
    const stretched = change(off, "widescreen", 1, RES);
    expect([stretched.widescreen, stretched.true_widescreen]).toEqual([true, false]);
    const back = change(stretched, "widescreen", 1, RES);
    expect([back.widescreen, back.true_widescreen]).toEqual([true, true]);
  });

  test("field of view and camera distance step and stop at their ends", () => {
    expect(settingsRows(view(), 0).find((r) => r.id === "fov")!.value).toBe("90°  PS1");
    expect(change(settings(), "fov", 1, RES).fov).toBe(95);
    expect(change(settings({ fov: 120 }), "fov", 1, RES).fov).toBe(120);
    expect(change(settings({ fov: 60 }), "fov", -1, RES).fov).toBe(60);
    expect(change(settings(), "camera_distance", -1, RES).camera_distance).toBe(90);
    expect(change(settings({ camera_distance: 200 }), "camera_distance", 1, RES).camera_distance).toBe(200);
  });

  test("every extra and cheat starts off and flips", () => {
    for (const tab of [3, 4]) {
      for (const r of settingsRows(view(), tab).filter((r) => r.kind === "toggle")) {
        expect(r.value).toBe("OFF");
        const on = change(settings(), r.id, 1, RES);
        expect((on as unknown as Record<string, boolean>)[r.id]).toBe(true);
      }
    }
  });

  test("FSR sharpness is offered only with FSR, pad types only with controllers", () => {
    expect(settingsRows(view(), 1).find((r) => r.id === "sharpness")!.enabled).toBe(false);
    expect(settingsRows(view({ settings: settings({ upscale: 2 }) }), 1).find((r) => r.id === "sharpness")!.enabled).toBe(true);
    const off = settingsRows(view({ settings: settings({ controller: false }) }), 2);
    for (const id of ["xbox", "playstation", "vibration"]) expect(off.find((r) => r.id === id)!.enabled).toBe(false);
  });

  test("the resolution shows its aspect and steps through the list, wrapping", () => {
    expect(settingsRows(view(), 0).find((r) => r.id === "resolution")!.value).toBe("1920 × 1080  16:9");
    const up = change(settings(), "resolution", 1, RES);
    expect([up.width, up.height]).toEqual([2560, 1440]);
    const wrapped = change(up, "resolution", 1, RES);
    expect([wrapped.width, wrapped.height]).toEqual([1280, 800]);
  });

  test("a size set by hand moves onto the list's first or last entry", () => {
    const odd = settings({ width: 1000, height: 700 });
    expect(settingsRows(view({ settings: odd }), 0).find((r) => r.id === "resolution")!.value).toBe("1000 × 700");
    expect(change(odd, "resolution", 1, RES).width).toBe(1280);
    expect(change(odd, "resolution", -1, RES).width).toBe(2560);
  });

  test("choices wrap, brightness stops at its ends, toggles flip", () => {
    expect(change(settings({ upscale: 2 }), "upscale", 1, RES).upscale).toBe(0);
    expect(change(settings({ crt: 0 }), "crt", -1, RES).crt).toBe(2);
    expect(change(settings({ brightness: 2 }), "brightness", 1, RES).brightness).toBe(2);
    expect(change(settings({ brightness: -2 }), "brightness", -1, RES).brightness).toBe(-2);
    expect(change(settings({ renderscale: 1 }), "renderscale", -1, RES).renderscale).toBe(1);
    expect(change(settings({ renderscale: 7 }), "renderscale", 1, RES).renderscale).toBe(8);
    expect(change(settings({ renderscale: 8 }), "renderscale", 1, RES).renderscale).toBe(0);
    expect(change(settings({ renderscale: 0 }), "renderscale", 1, RES).renderscale).toBe(0);
    expect(change(settings({ renderscale: 0 }), "renderscale", -1, RES).renderscale).toBe(8);
    expect(change(settings(), "vsync", 1, RES).vsync).toBe(false);
  });

  test("the keyboard comes back on rather than leaving nothing to drive with", () => {
    const padOnly = change(settings(), "keyboard", 1, RES);
    expect(padOnly.keyboard).toBe(false);
    const neither = change(padOnly, "controller", 1, RES);
    expect(neither.controller).toBe(false);
    expect(neither.keyboard).toBe(true);
  });

  test("a change never touches the settings it was given", () => {
    const s = settings();
    change(s, "fullscreen", 1, RES);
    expect(s.fullscreen).toBe(false);
  });
});

describe("progress bar", () => {
  test("configure fills the first 6%, the build the rest", () => {
    const p = newProgress();
    onPhase(p, "configure", 0);
    onStep(p, 1, 2, 0);
    expect(targetPct(p, 0)).toBeCloseTo(3);
    onPhase(p, "build", 0);
    onStep(p, 50, 100, 0);
    expect(targetPct(p, 0)).toBeCloseTo(53);
  });

  test("a long step creeps toward the next one but never reaches it", () => {
    const p = newProgress();
    onPhase(p, "build", 0);
    onStep(p, 1, 10, 0);
    const at = 6 + 9.4;
    const next = 6 + 18.8;
    const later = targetPct(p, 60_000);
    expect(later).toBeGreaterThan(at);
    expect(later).toBeLessThan(next);
    expect(targetPct(p, 600_000)).toBeLessThan(next);
  });

  test("the bar eases forward and never goes back", () => {
    const p = newProgress();
    onPhase(p, "build", 0);
    onStep(p, 80, 100, 0);
    let shown = 0;
    for (let i = 0; i < 200; i++) shown = tick(p, 0);
    expect(shown).toBeGreaterThan(80);
    onStep(p, 10, 100, 1); // a step count that went back (another target started)
    expect(tick(p, 1)).toBeGreaterThanOrEqual(shown);
  });

  test("launching and playing show a full bar", () => {
    const p = newProgress();
    onPhase(p, "playing", 0);
    expect(targetPct(p, 0)).toBe(100);
  });
});
