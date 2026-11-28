// The launcher's window: the cover on the left, and on the right a menu in the game's own style. Arrows,
// Enter and Esc move through it as in the game, so does a pad (D-pad, Cross/A, Circle/B, L1/R1), and the
// mouse picks a row by pointing at it.

import "./style.css";
import { backend, type Backend, type LauncherEvent, type View } from "./api";
import { Flag } from "./flag";
import { change, fixRow, homeRows, newProgress, onPhase, onStep, settingsRows, stepRow, TABS, tick, type Row } from "./menu";

const $ = (id: string) => document.getElementById(id)!;
const stage = $("stage");
const right = $("right");
const flag = new Flag($("flag") as HTMLCanvasElement);
const progress = newProgress();

let api: Backend;
let v: View;
let page: "home" | "settings" = "home";
let tab = 0;
let homeSel = 0;
const tabSel = TABS.map(() => 1);
let rows: Row[] = [];
let coverKey = "-";
let lastMouse = "";

interface Dialog {
  title: string;
  text: string;
  rows: Row[];
  sel: number;
  pick: (id: string) => void;
}
let dialog: Dialog | null = null;

const ARROW_L = `<svg viewBox="0 0 20 20"><polygon points="15,2 3,10 15,18" fill="url(#og)" stroke="#000" stroke-width="3" stroke-linejoin="round"/></svg>`;
const ARROW_R = `<svg viewBox="0 0 20 20"><polygon points="5,2 17,10 5,18" fill="url(#og)" stroke="#000" stroke-width="3" stroke-linejoin="round"/></svg>`;

function esc(s: string): string {
  return s.replace(/[&<>"']/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[c]!);
}

/** Lettering the game's way: an outlined line whose fill is a gradient (the ::after holds the fill). */
function gt(text: string, cls = ""): string {
  return `<span class="gt ${cls}" data-text="${esc(text)}">${esc(text)}</span>`;
}

/** Shrinks a line of lettering until it fits `max` pixels, as the game squeezes long names. */
function fit(el: HTMLElement | null, max: number, min = 12) {
  if (!el) return;
  el.style.fontSize = "";
  let size = parseFloat(getComputedStyle(el).fontSize);
  while (el.scrollWidth > max && size > min) {
    size -= 1;
    el.style.fontSize = `${size}px`;
  }
}

function layout() {
  const z = Math.min(innerWidth / 1120, innerHeight / 700);
  stage.style.zoom = String(z);
  document.documentElement.style.setProperty("--z", String(z));
  stage.style.left = `${(innerWidth - 1120 * z) / 2 / z}px`;
  stage.style.top = `${(innerHeight - 700 * z) / 2 / z}px`;
}

// ------------------------------------------------------------------------------------------------- the cover

async function showCover() {
  if (v.cover_key === coverKey) return;
  coverKey = v.cover_key;
  const wanted = Boolean(v.cover_key || new URLSearchParams(location.search).get("cover"));
  const url = wanted ? await api.coverImage() : null;
  const box = $("cover");
  if (url) {
    box.innerHTML = `<img alt="Cover" src="${esc(url)}">`;
    setGT($("coverLink").firstElementChild as HTMLElement, "CHANGE COVER");
  } else {
    box.innerHTML = `<div class="placeholder"><span class="big">${gt("CTR")}</span>${gt("YOUR COVER HERE", "peri")}<span class="small">Drop a picture of your game's cover on the window, or click to pick one.</span></div>`;
    setGT($("coverLink").firstElementChild as HTMLElement, "ADD COVER");
  }
}

function setGT(el: HTMLElement, text: string) {
  el.textContent = text;
  el.dataset.text = text;
}

async function pickCover() {
  const path = await api.pickCover();
  if (path) await useCover(path);
}

async function useCover(path: string) {
  try {
    v = await api.setCover(path);
    await showCover();
  } catch (e) {
    say(String(e));
  }
}

// ------------------------------------------------------------------------------------------------- the pages

function renderPage() {
  if (page === "home") {
    right.innerHTML = `
      <section class="box" id="discBox">
        <div class="boxTitle">${gt("YOUR DISC")}</div><div class="rule"></div>
        <div class="discBody" id="discBody"></div>
      </section>
      <section class="box" id="menuBox"><div class="menu"><div class="hilite" id="hilite"></div><div id="rows"></div></div></section>
      <section class="box" id="statusBox">
        <div class="statusLine"><span id="status"></span><span class="pct" id="pct"></span></div>
        <div class="track"><div class="fill" id="fill"></div><div class="finish"></div></div>
        <div class="small" id="detail"></div>
      </section>`;
    $("discBox").addEventListener("click", () => void chooseDisc());
  } else {
    right.innerHTML = `
      <section class="box settings" id="settingsBox">
        <div class="boxTitle">${gt("SETTINGS")}</div><div class="rule"></div>
        <div class="menu"><div class="hilite" id="hilite"></div><div id="rows"></div></div>
      </section>
      <section class="box" id="hintBox">
        <div class="small" id="hint"></div>
        <div class="small faint keys">↑ ↓ choose &middot; ← → change &middot; Enter select &middot; Esc back &middot; PgUp PgDn tabs</div>
      </section>`;
  }
  const list = $("rows");
  list.addEventListener("mousemove", (e) => pointAt(e as MouseEvent));
  list.addEventListener("click", (e) => void clickRow(e as MouseEvent));
  update();
}

function rowHtml(r: Row, i: number, sel: number): string {
  const cls = ["row", r.enabled ? "" : "off", i === sel ? "sel" : ""].join(" ");
  if (r.kind === "tabs") {
    const names = TABS.map((t, k) => `<span data-tab="${k}">${gt(t, k === tab ? "" : "gray")}</span>`).join("");
    return `<div class="${cls}" data-i="${i}"><span class="arrow l">${ARROW_L}</span><span class="tabs">${names}</span><span class="arrow r">${ARROW_R}</span></div>`;
  }
  let value = "";
  if (r.value !== undefined) {
    const vcls = !r.enabled ? "gray" : r.kind === "toggle" ? (r.value === "ON" ? "green" : "peri") : "peri";
    const arrows = r.enabled; // left and right flip a toggle too, so it gets the same arrows as a choice
    value = `<span class="value">${arrows ? `<span class="arrow l">${ARROW_L}</span>` : ""}${gt(r.value, vcls)}${arrows ? `<span class="arrow r">${ARROW_R}</span>` : ""}</span>`;
  }
  return `<div class="${cls}" data-i="${i}">${gt(r.label, r.enabled ? "" : "gray")}${value}</div>`;
}

function currentSel(): number {
  return page === "home" ? homeSel : tabSel[tab];
}

function setSel(i: number) {
  if (page === "home") homeSel = i;
  else tabSel[tab] = i;
}

function update() {
  if (!v) return;
  document.body.classList.toggle("busy", v.busy);
  flag.fast = v.busy && (v.phase === "copy" || v.phase === "configure" || v.phase === "build");
  rows = page === "home" ? homeRows(v) : settingsRows(v, tab);
  const sel = fixRow(rows, currentSel());
  setSel(sel);
  $("rows").innerHTML = rows.map((r, i) => rowHtml(r, i, sel)).join("");
  placeHilite();

  if (page === "home") {
    const d = v.disc;
    const body = $("discBody");
    $("discBox").classList.toggle("off", v.busy);
    if (d.state === 0) {
      body.innerHTML = `<div class="line">${gt("DROP YOUR CTR DISC HERE")}</div>
        <div class="small">The folder with your .bin/.cue or .iso, or the file itself. Or click here to pick it.</div>
        <div class="small faint">CTR Native plays the NTSC-U disc, SCUS-94426, copied with raw 2352-byte sectors.</div>`;
    } else {
      const cls = d.state === 1 ? "green" : d.state === 3 ? "red" : "";
      body.innerHTML = `<div class="line">${gt(d.title.toUpperCase(), cls)}</div>
        <div class="small">${esc(d.detail)}</div>
        <div class="small faint ellipsis">${esc(v.disc_source || d.bin)}</div>`;
    }
    fit(body.querySelector(".line .gt"), 480);
    const failed = v.phase === "failed";
    const done = v.phase === "done" || v.phase === "playing" || v.phase === "launch";
    $("status").innerHTML = gt((v.status || " ").toUpperCase(), failed ? "red" : v.phase === "done" ? "green" : "");
    fit($("status").firstElementChild as HTMLElement, 400);
    $("detail").textContent = v.detail;
    $("fill").className = `fill ${failed ? "gray" : done ? "green" : ""}`;
    drawProgress();
  } else {
    $("hint").textContent = rows[sel]?.hint ?? "";
  }
  if (dialog) drawDialog();
}

/** The selected row's highlight: one bar per menu that moves, so its pulse never restarts. */
function placeHilite() {
  const bar = $("hilite");
  const h = page === "home" ? 42 : 38;
  bar.style.top = `${10 + currentSel() * h}px`;
  bar.style.height = `${h}px`;
}

function drawProgress() {
  if (page !== "home") return;
  const pct = progress.shown;
  const fill = $("fill");
  fill.style.width = `${pct.toFixed(2)}%`;
  fill.classList.toggle("none", pct <= 0);
  const showPct = v.phase !== "idle" && v.phase !== "failed" && (v.busy || v.phase === "done");
  $("pct").innerHTML = showPct ? gt(`${Math.round(pct)}%`) : "";
}

function animate() {
  const now = performance.now();
  const before = progress.shown;
  tick(progress, now);
  if (progress.shown !== before) drawProgress();
  requestAnimationFrame(animate);
}

function say(text: string) {
  v.status = text;
  v.detail = "";
  if (page === "home") update();
}

// ------------------------------------------------------------------------------------------------- actions

async function chooseDisc() {
  if (v.busy) return;
  const path = await api.pickDisc();
  if (path) await useDisc(path);
}

async function useDisc(path: string) {
  try {
    v = await api.chooseDisc(path);
    progress.shown = 0;
    if (page !== "home") {
      page = "home";
      renderPage();
    } else update();
    await showCover();
  } catch (e) {
    say(String(e));
  }
}

async function startTask(play: boolean, clean: boolean) {
  try {
    await api.start(play, clean);
    v.busy = true;
    v.phase = "idle";
    v.status = play ? "Getting ready" : "Rebuilding the game";
    v.detail = "";
    progress.shown = 0;
    onPhase(progress, "idle", performance.now());
    update();
  } catch (e) {
    say(String(e));
  }
}

async function activate(r: Row, dir: 1 | -1) {
  if (!r.enabled) return;
  switch (r.id) {
    case "play":
      if (v.busy) return void api.cancel();
      if (v.disc.state !== 1 && v.disc.state !== 2) return void chooseDisc();
      return void startTask(true, false);
    case "disc":
      return void chooseDisc();
    case "settings":
      page = "settings";
      return renderPage();
    case "rebuild":
      return void startTask(false, true);
    case "log":
      return void api.showLog().catch((e) => say(String(e)));
    case "quit":
      return askQuit();
    case "back":
      page = "home";
      return renderPage();
    case "tabs":
      tab = (tab + dir + TABS.length) % TABS.length;
      return update();
    default:
      if (r.kind === "toggle" || r.kind === "choice") {
        v.settings = change(v.settings, r.id, dir, v.resolutions);
        update();
        v = await api.saveSettings(v.settings);
        update();
      }
  }
}

function askQuit() {
  if (!v.busy || v.phase === "playing") return void api.quit();
  openDialog("STOP THE BUILD?", "The game is still being rebuilt. Quitting stops the compiler; the next Play carries on.", [
    { id: "stay", kind: "action", label: "KEEP BUILDING", enabled: true, hint: "" },
    { id: "quit", kind: "action", label: "STOP AND QUIT", enabled: true, hint: "" },
  ], (id) => {
    if (id === "quit") void api.quit();
  });
}

// ------------------------------------------------------------------------------------------------- dialog

function openDialog(title: string, text: string, list: Row[], pick: (id: string) => void) {
  dialog = { title, text, rows: list, sel: 0, pick };
  drawDialog();
}

function drawDialog() {
  const m = $("modal");
  if (!dialog) {
    m.hidden = true;
    m.innerHTML = "";
    return;
  }
  const d = dialog;
  m.hidden = false;
  m.innerHTML = `<section class="box">
      <div class="boxTitle">${gt(d.title)}</div><div class="rule"></div>
      <div class="small" style="padding:12px 26px 0">${esc(d.text)}</div>
      <div class="menu"><div class="hilite" style="top:${10 + d.sel * 42}px;height:42px"></div>
      ${d.rows.map((r, i) => `<div class="row${i === d.sel ? " sel" : ""}" data-i="${i}">${gt(r.label)}</div>`).join("")}</div>
    </section>`;
  m.querySelectorAll<HTMLElement>(".row").forEach((el) => {
    el.addEventListener("mouseenter", () => {
      if (dialog && dialog.sel !== Number(el.dataset.i)) {
        dialog.sel = Number(el.dataset.i);
        drawDialog();
      }
    });
    el.addEventListener("click", () => closeDialog(Number(el.dataset.i)));
  });
}

function closeDialog(pick: number | null) {
  const d = dialog;
  dialog = null;
  drawDialog();
  if (d && pick !== null) d.pick(d.rows[pick].id);
}

// ------------------------------------------------------------------------------------------------- input

type Control = "up" | "down" | "left" | "right" | "ok" | "back" | "prev" | "next";

function control(c: Control) {
  if (dialog) {
    if (c === "up" || c === "down") {
      dialog.sel = stepRow(dialog.rows, dialog.sel, c === "up" ? -1 : 1);
      drawDialog();
    } else if (c === "ok") closeDialog(dialog.sel);
    else if (c === "back") closeDialog(null);
    return;
  }
  const sel = currentSel();
  const r = rows[sel];
  switch (c) {
    case "up":
    case "down":
      setSel(stepRow(rows, sel, c === "up" ? -1 : 1));
      return update();
    case "left":
    case "right":
      if (r && (r.kind === "choice" || r.kind === "toggle" || r.kind === "tabs")) void activate(r, c === "left" ? -1 : 1);
      return;
    case "ok":
      if (r) void activate(r, 1);
      return;
    case "back":
      if (page === "settings") {
        page = "home";
        renderPage();
      }
      return;
    case "prev":
    case "next":
      if (page === "settings") {
        tab = (tab + (c === "prev" ? -1 : 1) + TABS.length) % TABS.length;
        update();
      }
  }
}

const KEYS: Record<string, Control> = {
  ArrowUp: "up",
  ArrowDown: "down",
  ArrowLeft: "left",
  ArrowRight: "right",
  Enter: "ok",
  " ": "ok",
  Escape: "back",
  Backspace: "back",
  PageUp: "prev",
  PageDown: "next",
  q: "prev",
  e: "next",
};

window.addEventListener("keydown", (e) => {
  const c = KEYS[e.key];
  if (!c || e.altKey || e.ctrlKey || e.metaKey) return;
  e.preventDefault();
  control(c);
});

function pointAt(e: MouseEvent) {
  const key = `${e.clientX},${e.clientY}`;
  if (key === lastMouse) return;
  lastMouse = key;
  const el = (e.target as HTMLElement).closest<HTMLElement>(".row");
  if (!el) return;
  const i = Number(el.dataset.i);
  if (rows[i]?.enabled && i !== currentSel()) {
    setSel(i);
    $("rows")
      .querySelectorAll(".row")
      .forEach((n, k) => n.classList.toggle("sel", k === i));
    placeHilite();
    if (page === "settings") $("hint").textContent = rows[i].hint;
  }
}

async function clickRow(e: MouseEvent) {
  const target = e.target as HTMLElement;
  const el = target.closest<HTMLElement>(".row");
  if (!el) return;
  const r = rows[Number(el.dataset.i)];
  if (!r) return;
  const tabEl = target.closest<HTMLElement>("[data-tab]");
  if (r.kind === "tabs" && tabEl) {
    tab = Number(tabEl.dataset.tab);
    return update();
  }
  await activate(r, target.closest(".arrow.l") ? -1 : 1);
}

// A pad: the D-pad or left stick, Cross/A, Circle/B, L1/R1; held directions repeat like the game's menus.
const held = new Map<Control, number>();
let polling = false;

function pollPads(t: number) {
  const now: Partial<Record<Control, boolean>> = {};
  for (const p of navigator.getGamepads?.() ?? []) {
    if (!p) continue;
    const b = (i: number) => Boolean(p.buttons[i]?.pressed);
    const x = p.axes[0] ?? 0;
    const y = p.axes[1] ?? 0;
    now.up ||= b(12) || y < -0.55;
    now.down ||= b(13) || y > 0.55;
    now.left ||= b(14) || x < -0.55;
    now.right ||= b(15) || x > 0.55;
    now.ok ||= b(0) || b(9);
    now.back ||= b(1);
    now.prev ||= b(4);
    now.next ||= b(5);
  }
  for (const c of ["up", "down", "left", "right", "ok", "back", "prev", "next"] as Control[]) {
    if (!now[c]) {
      held.delete(c);
      continue;
    }
    const due = held.get(c);
    const repeats = c === "up" || c === "down" || c === "left" || c === "right";
    if (due === undefined) {
      held.set(c, t + 350);
      control(c);
    } else if (repeats && t >= due) {
      held.set(c, t + 110);
      control(c);
    }
  }
  requestAnimationFrame(pollPads);
}

window.addEventListener("gamepadconnected", () => {
  if (!polling) {
    polling = true;
    requestAnimationFrame(pollPads);
  }
});

// ------------------------------------------------------------------------------------------------- events

function onEvent(e: LauncherEvent) {
  const now = performance.now();
  switch (e.kind) {
    case "status":
      v.status = e.status;
      break;
    case "detail":
      v.detail = e.detail;
      break;
    case "phase":
      v.phase = e.phase;
      // a run the launcher didn't start from this window (a build on its way when it opened) is still a run
      if (e.phase !== "idle" && e.phase !== "done" && e.phase !== "failed") v.busy = true;
      onPhase(progress, e.phase, now);
      break;
    case "step":
      onStep(progress, e.done, e.total, now);
      return;
    case "done":
      v.busy = false;
      if (e.ok) progress.shown = 100;
      void api.view().then((nv) => {
        v = nv;
        update();
      });
      break;
  }
  if (page === "home") update();
  else rows = settingsRows(v, tab);
}

async function main() {
  layout();
  window.addEventListener("resize", () => {
    layout();
    if (dialog) drawDialog();
  });
  api = await backend();
  v = await api.view();
  const q = new URLSearchParams(location.search);
  if (q.get("page") === "settings") {
    page = "settings";
    tab = Number(q.get("tab") ?? 0) % TABS.length;
  }
  homeSel = v.disc.state === 1 || v.disc.state === 2 ? 0 : 1;
  api.onEvent(onEvent);
  api.onConfirmClose(askQuit);
  api.onDrop((kind, paths) => {
    document.body.classList.toggle("dropping", kind === "over");
    if (kind !== "drop") return;
    for (const p of paths) {
      if (/\.(png|jpe?g|webp|bmp|gif)$/i.test(p)) void useCover(p);
      else if (!v.busy) void useDisc(p);
    }
  });
  $("cover").addEventListener("click", () => void pickCover());
  $("coverLink").addEventListener("click", () => void pickCover());
  renderPage();
  await showCover();
  requestAnimationFrame(animate);
}

void main();
