//! CTR Launcher: the left side shows the game's cover, the right side is a menu in the game's own style.
//! Play puts the player's own disc where the game reads it, recompiles CTR Native from source with the
//! compiler bundled beside the launcher, and starts it with the settings in ctr_native.ini.
//!
//! The window (../src, Bun and TypeScript) draws everything; this side does the work and owns the files.

#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

mod cover;
mod disc;
mod ini;
mod paths;
mod recipe;
mod settings;
mod tasks;
mod webview;

use serde::Serialize;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use tauri::{Emitter, Manager, State};

use disc::DiscInfo;
use ini::Ini;
use paths::{Mode, Paths};
use settings::{Resolution, Settings, RESOLUTIONS};
use tasks::{Job, Phase, Worker};

pub struct Inner {
    pub settings: Settings,
    pub disc: DiscInfo,
    pub disc_source: String,
    pub cover_path: String,
    pub status: String,
    pub detail: String,
    pub phase: Phase,
}

pub struct Shared {
    pub paths: Paths,
    pub inner: Mutex<Inner>,
    pub busy: AtomicBool,
    pub cancel: AtomicBool,
    pub job: Job,
}

type App = Arc<Shared>;

/// Everything the window shows, in one answer.
#[derive(Serialize)]
struct View {
    mode: Mode,
    has_source: bool,
    data_root: String,
    settings: Settings,
    resolutions: &'static [Resolution],
    disc: DiscInfo,
    disc_source: String,
    cover_key: String,
    status: String,
    detail: String,
    phase: Phase,
    busy: bool,
    has_log: bool,
    has_game: bool,
    version: &'static str,
}

fn view(app: &Shared) -> View {
    let inner = app.inner.lock().unwrap();
    View {
        mode: app.paths.mode,
        has_source: app.paths.src_root.is_some(),
        data_root: app.paths.data_root.display().to_string(),
        settings: inner.settings.clone(),
        resolutions: &RESOLUTIONS,
        disc: inner.disc.clone(),
        disc_source: inner.disc_source.clone(),
        cover_key: cover::key(&inner.cover_path),
        status: inner.status.clone(),
        detail: inner.detail.clone(),
        phase: inner.phase,
        busy: app.busy.load(Ordering::SeqCst),
        has_log: app.paths.newest_log().is_some(),
        has_game: app.paths.game_exe().is_some(),
        version: env!("CARGO_PKG_VERSION"),
    }
}

fn save_launcher_paths(app: &Shared, inner: &Inner) {
    let mut ini = Ini::load(&app.paths.launcher_ini);
    ini.set("launcher", "disc", &inner.disc_source);
    ini.set("launcher", "cover", &inner.cover_path);
    let _ = ini.save(&app.paths.launcher_ini);
}

#[tauri::command]
fn launcher_view(app: State<'_, App>) -> View {
    view(&app)
}

#[tauri::command]
fn cover_image(app: State<'_, App>) -> Option<String> {
    let path = app.inner.lock().unwrap().cover_path.clone();
    if path.is_empty() {
        return None;
    }
    cover::data_url(Path::new(&path)).ok()
}

#[tauri::command]
fn choose_disc(app: State<'_, App>, path: String) -> Result<View, String> {
    if app.busy.load(Ordering::SeqCst) {
        return Err("Wait for the game to finish building first.".into());
    }
    let path = path.trim().trim_matches('"').to_string();
    let (info, image) = disc::choose(Path::new(&path));
    {
        let mut inner = app.inner.lock().unwrap();
        if inner.cover_path.is_empty() {
            if let Some(image) = image.filter(|p| cover::data_url(p).is_ok()) {
                inner.cover_path = image.display().to_string();
            }
        }
        inner.status = if info.playable() { "Ready".into() } else { String::new() };
        inner.detail = if info.playable() { "Press Play: the game is recompiled from source, then it starts.".into() } else { String::new() };
        inner.phase = Phase::Idle;
        inner.disc = info;
        inner.disc_source = path;
        save_launcher_paths(&app, &inner);
    }
    Ok(view(&app))
}

#[tauri::command]
fn set_cover(app: State<'_, App>, path: String) -> Result<View, String> {
    if !path.is_empty() {
        cover::data_url(Path::new(&path))?;
    }
    {
        let mut inner = app.inner.lock().unwrap();
        inner.cover_path = path;
        save_launcher_paths(&app, &inner);
    }
    Ok(view(&app))
}

#[tauri::command]
fn save_settings(app: State<'_, App>, settings: Settings) -> View {
    let mut s = settings;
    s.tidy();
    let mut ini = Ini::load(&app.paths.game_ini);
    s.save(&mut ini);
    let _ = ini.save(&app.paths.game_ini);
    app.inner.lock().unwrap().settings = s;
    view(&app)
}

#[tauri::command]
fn start(handle: tauri::AppHandle, app: State<'_, App>, play: bool, clean: bool) -> Result<(), String> {
    if app.busy.compare_exchange(false, true, Ordering::SeqCst, Ordering::SeqCst).is_err() {
        return Err("Already working.".into());
    }
    app.cancel.store(false, Ordering::SeqCst);
    {
        let mut inner = app.inner.lock().unwrap();
        inner.phase = Phase::Idle;
        inner.status = if play { "Getting ready".into() } else { "Rebuilding the game".into() };
        inner.detail = String::new();
    }
    let shared = Arc::clone(&app);
    let worker = Worker::new(handle, shared);
    std::thread::spawn(move || worker.run(play, clean));
    Ok(())
}

#[tauri::command]
fn cancel(app: State<'_, App>) {
    if app.busy.load(Ordering::SeqCst) {
        app.cancel.store(true, Ordering::SeqCst);
        app.job.kill();
    }
}

#[tauri::command]
fn show_log(app: State<'_, App>) -> Result<(), String> {
    let log = app.paths.newest_log().ok_or("There is no log yet.")?;
    open_with_default(&log).map_err(|e| format!("Could not open the log: {e}"))
}

fn open_with_default(path: &Path) -> std::io::Result<()> {
    if cfg!(windows) {
        std::process::Command::new("explorer").arg(path).spawn().map(|_| ())
    } else if cfg!(target_os = "macos") {
        std::process::Command::new("open").arg(path).spawn().map(|_| ())
    } else {
        std::process::Command::new("xdg-open").arg(path).spawn().map(|_| ())
    }
}

#[tauri::command]
fn quit(handle: tauri::AppHandle, app: State<'_, App>) {
    if app.busy.load(Ordering::SeqCst) {
        app.cancel.store(true, Ordering::SeqCst);
        app.job.kill();
    }
    handle.exit(0);
}

/// launcher.ini, ctr_native.ini and the disc already placed in assets\ (an earlier run, a checkout).
fn load(paths: Paths, work_area: (i32, i32)) -> Shared {
    let launcher = Ini::load(&paths.launcher_ini);
    let settings = Settings::load(&Ini::load(&paths.game_ini), settings::default_resolution(work_area.0, work_area.1));
    let disc_source = launcher.get("launcher", "disc").unwrap_or_default();
    let mut cover_path = launcher.get("launcher", "cover").unwrap_or_default();

    let placed = paths.assets().join("ctr-u.bin");
    let source = Path::new(&disc_source);
    let disc = if !disc_source.is_empty() && source.exists() {
        disc::choose(source).0
    } else if placed.is_file() {
        disc::analyze_bin(&placed)
    } else {
        DiscInfo::default()
    };

    if cover_path.is_empty() || !Path::new(&cover_path).is_file() {
        let names = ["cover.png", "cover.jpg", "cover.jpeg", "cover.webp", "cover.bmp", "cover.gif"];
        cover_path = names
            .iter()
            .flat_map(|n| [paths.data_root.join(n), paths.assets().join(n)])
            .find(|p| p.is_file())
            .map(|p| p.display().to_string())
            .unwrap_or_default();
    }

    let (status, detail) = if disc.playable() {
        ("Ready".to_string(), "Press Play: the game is recompiled from source, then it starts.".to_string())
    } else {
        ("Waiting for your disc".to_string(), String::new())
    };
    Shared {
        paths,
        inner: Mutex::new(Inner { settings, disc, disc_source, cover_path, status, detail, phase: Phase::Idle }),
        busy: AtomicBool::new(false),
        cancel: AtomicBool::new(false),
        job: Job::new(),
    }
}

fn exe_dir() -> PathBuf {
    std::env::current_exe()
        .ok()
        .and_then(|p| p.parent().map(Path::to_path_buf))
        .unwrap_or_else(|| PathBuf::from("."))
}

fn main() {
    let dir = exe_dir();
    let paths = Paths::find(&dir);
    // The window needs WebView2: Windows' own, else the one in webview2\ beside the launcher (webview.rs).
    webview::new_log(&paths.log_dir);
    let setting = Ini::load(&paths.launcher_ini).get("window", "webview2").unwrap_or_default();
    let picked = webview::prepare(&dir, &setting, &paths.log_dir);
    webview::note(&paths.log_dir, &format!("window: {}", picked.describe()));
    let log_dir = paths.log_dir.clone();
    let error = match std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| run(paths))) {
        Ok(Ok(())) => return,
        Ok(Err(e)) => e.to_string(),
        Err(panic) => panic
            .downcast_ref::<String>()
            .cloned()
            .or_else(|| panic.downcast_ref::<&str>().map(|s| s.to_string()))
            .unwrap_or_else(|| "it stopped".to_string()),
    };
    // Without a window there is nowhere else to say it: a plain Windows message box, and the log.
    webview::note(&log_dir, &format!("the window could not start: {error}"));
    webview::message_box(&webview::missing_text(&picked, &error));
    std::process::exit(1);
}

fn run(paths: Paths) -> tauri::Result<()> {
    tauri::Builder::default()
        .plugin(tauri_plugin_dialog::init())
        .setup(move |app| {
            // The game's window sizes are screen pixels, not the desktop's scaled ones: the physical work area.
            let work = app
                .primary_monitor()
                .ok()
                .flatten()
                .map(|m| (m.work_area().size.width as i32, m.work_area().size.height as i32))
                .unwrap_or((1920, 1040));
            app.manage::<App>(Arc::new(load(paths.clone(), work)));
            Ok(())
        })
        .on_window_event(|window, event| {
            if let tauri::WindowEvent::CloseRequested { api, .. } = event {
                let app = window.state::<App>();
                let building = app.busy.load(Ordering::SeqCst) && app.inner.lock().unwrap().phase != Phase::Playing;
                if building {
                    // The window asks first, in the game's style, then calls quit.
                    api.prevent_close();
                    let _ = window.emit("confirm-close", ());
                }
            }
        })
        .invoke_handler(tauri::generate_handler![
            launcher_view,
            cover_image,
            choose_disc,
            set_cover,
            save_settings,
            start,
            cancel,
            show_log,
            quit
        ])
        .run(tauri::generate_context!())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn loads_with_nothing_there() {
        let dir = std::env::temp_dir().join(format!("ctr-launcher-load-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        std::fs::create_dir_all(&dir).unwrap();
        let shared = load(Paths::find(&dir), (1920, 1040));
        let inner = shared.inner.lock().unwrap();
        assert_eq!(inner.disc.state, 0);
        assert_eq!(inner.status, "Waiting for your disc");
        assert_eq!((inner.settings.width, inner.settings.height), (1600, 900));
        drop(inner);
        let _ = std::fs::remove_dir_all(&dir);
    }
}
