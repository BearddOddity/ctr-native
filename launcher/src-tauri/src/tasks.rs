//! Play: put the player's disc where the game reads it, recompile the game from source with the bundled
//! compiler (llvm-mingw, CMake and Ninja in tools\, so nothing has to be installed), then start it with the
//! settings in ctr_native.ini. Runs on its own thread and tells the window how it goes.

use serde::Serialize;
use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::sync::atomic::Ordering;
use std::sync::Arc;
use std::time::Instant;
use tauri::{AppHandle, Emitter, Manager};

use crate::ini::Ini;
use crate::paths::Mode;
use crate::Shared;

#[derive(Serialize, Clone, Copy, Debug, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum Phase {
    Idle,
    Copy,
    Configure,
    Build,
    Launch,
    Playing,
    Done,
    Failed,
}

/// What the window hears, on the "launcher" event.
#[derive(Serialize, Clone, Debug)]
#[serde(tag = "kind", rename_all = "lowercase")]
pub enum Event {
    Status { status: String },
    Detail { detail: String },
    Phase { phase: Phase },
    Step { done: u64, total: u64 },
    Done { ok: bool },
}

#[cfg(windows)]
const CREATE_NO_WINDOW: u32 = 0x0800_0000;

#[cfg(windows)]
fn no_window(cmd: &mut Command) {
    use std::os::windows::process::CommandExt;
    cmd.creation_flags(CREATE_NO_WINDOW);
}

#[cfg(not(windows))]
fn no_window(_cmd: &mut Command) {}

// ---------------------------------------------------------------------------------------------------------
// The compiler's process tree lives in a job object: Cancel, or closing the launcher, stops it all.

#[cfg(windows)]
pub struct Job(windows_sys::Win32::Foundation::HANDLE);

#[cfg(windows)]
unsafe impl Send for Job {}
#[cfg(windows)]
unsafe impl Sync for Job {}

#[cfg(windows)]
impl Job {
    pub fn new() -> Job {
        use windows_sys::Win32::System::JobObjects::*;
        unsafe {
            let h = CreateJobObjectW(std::ptr::null(), std::ptr::null());
            if !h.is_null() {
                let mut limits: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = std::mem::zeroed();
                limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                SetInformationJobObject(
                    h,
                    JobObjectExtendedLimitInformation,
                    &limits as *const _ as *const core::ffi::c_void,
                    std::mem::size_of::<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>() as u32,
                );
            }
            Job(h)
        }
    }

    fn assign(&self, child: &std::process::Child) {
        use std::os::windows::io::AsRawHandle;
        if !self.0.is_null() {
            unsafe {
                windows_sys::Win32::System::JobObjects::AssignProcessToJobObject(self.0, child.as_raw_handle() as _);
            }
        }
    }

    pub fn kill(&self) {
        if !self.0.is_null() {
            unsafe {
                windows_sys::Win32::System::JobObjects::TerminateJobObject(self.0, 1);
            }
        }
    }
}

/// Elsewhere (a developer's Linux checkout) Cancel stops the command itself.
#[cfg(not(windows))]
pub struct Job(std::sync::Mutex<Option<u32>>);

#[cfg(not(windows))]
impl Job {
    pub fn new() -> Job {
        Job(std::sync::Mutex::new(None))
    }

    fn assign(&self, child: &std::process::Child) {
        *self.0.lock().unwrap() = Some(child.id());
    }

    pub fn kill(&self) {
        if let Some(pid) = self.0.lock().unwrap().take() {
            let _ = Command::new("kill").arg(pid.to_string()).status();
        }
    }
}

// ---------------------------------------------------------------------------------------------------------
// The log

struct Log(Option<fs::File>);

impl Log {
    fn create(path: &Path) -> Log {
        if let Some(dir) = path.parent() {
            let _ = fs::create_dir_all(dir);
        }
        Log(fs::File::create(path).ok())
    }

    fn bytes(&mut self, b: &[u8]) {
        if let Some(f) = &mut self.0 {
            let _ = f.write_all(b);
        }
    }

    fn line(&mut self, s: &str) {
        self.bytes(s.as_bytes());
        self.bytes(b"\r\n");
    }
}

fn describe(cmd: &Command) -> String {
    let mut out = format!("\"{}\"", cmd.get_program().to_string_lossy());
    for a in cmd.get_args() {
        out.push_str(&format!(" \"{}\"", a.to_string_lossy()));
    }
    out
}

fn forward(p: &Path) -> String {
    p.to_string_lossy().replace('\\', "/")
}

// ---------------------------------------------------------------------------------------------------------
// Tools: the bundled compiler first, then launcher.ini's [build] overrides, then PATH (a developer's own)

#[derive(Debug, Default)]
pub struct Tools {
    pub cmake: PathBuf,
    pub ninja: PathBuf,
    pub cc: PathBuf,
    pub cxx: Option<PathBuf>,
    pub rc: Option<PathBuf>,
    pub bin_dirs: Vec<PathBuf>,
    /// [build] command=: one command that builds the game (tests, custom setups).
    pub command: Option<String>,
}

pub fn which(name: &str) -> Option<PathBuf> {
    let path = std::env::var_os("PATH")?;
    let names: Vec<String> = if cfg!(windows) && Path::new(name).extension().is_none() {
        vec![format!("{name}.exe"), format!("{name}.cmd"), format!("{name}.bat")]
    } else {
        vec![name.to_string()]
    };
    std::env::split_paths(&path)
        .flat_map(|dir| names.iter().map(move |n| dir.join(n)))
        .find(|p| p.is_file())
}

fn found(p: PathBuf) -> Option<PathBuf> {
    p.is_file().then_some(p)
}

pub fn find_tools(exe_dir: &Path, launcher: &Ini) -> Option<Tools> {
    let setting = |key: &str| launcher.get("build", key).filter(|v| !v.trim().is_empty());
    if let Some(command) = setting("command") {
        return Some(Tools { command: Some(command), ..Default::default() });
    }
    let tools = exe_dir.join("tools");
    let llvm = tools.join("llvm-mingw").join("bin");
    let mut cmake = found(tools.join("cmake").join("bin").join("cmake.exe"));
    let mut ninja = found(tools.join("ninja").join("ninja.exe"));
    let mut cc = found(llvm.join("i686-w64-mingw32-clang.exe"));
    let (mut cxx, mut rc) = match cc {
        Some(_) => (found(llvm.join("i686-w64-mingw32-clang++.exe")), found(llvm.join("i686-w64-mingw32-windres.exe"))),
        None => (None, None),
    };
    if let Some(p) = setting("cmake") {
        cmake = Some(PathBuf::from(p));
    }
    if let Some(p) = setting("ninja") {
        ninja = Some(PathBuf::from(p));
    }
    if let Some(p) = setting("cc") {
        cc = Some(PathBuf::from(p));
        cxx = None;
        rc = None;
    }
    cmake = cmake.or_else(|| which("cmake"));
    ninja = ninja.or_else(|| which("ninja"));
    if cc.is_none() {
        if let Some(clang) = which("i686-w64-mingw32-clang") {
            cc = Some(clang);
            cxx = which("i686-w64-mingw32-clang++");
            rc = which("i686-w64-mingw32-windres");
        } else if let Some(gcc) = which("i686-w64-mingw32-gcc") {
            cc = Some(gcc);
            cxx = which("i686-w64-mingw32-g++");
            rc = which("i686-w64-mingw32-windres").or_else(|| which("windres"));
        }
    }
    let (cmake, ninja, cc) = (cmake?, ninja?, cc?);
    let mut bin_dirs: Vec<PathBuf> = Vec::new();
    for p in [&cc, &ninja, &cmake] {
        if let Some(dir) = p.parent() {
            if !dir.as_os_str().is_empty() && !bin_dirs.iter().any(|d| d == dir) {
                bin_dirs.push(dir.to_path_buf());
            }
        }
    }
    Some(Tools { cmake, ninja, cc, cxx, rc, bin_dirs, command: None })
}

// ---------------------------------------------------------------------------------------------------------
// What the build prints

/// "[12/486] Building C object .../src/video/SDL_video.c.obj" -> (12, 486, "SDL_video.c", false)
pub fn ninja_step(line: &str) -> Option<(u64, u64, String, bool)> {
    let rest = line.strip_prefix('[')?;
    let (counts, desc) = rest.split_once(']')?;
    let (done, total) = counts.split_once('/')?;
    let (done, total): (u64, u64) = (done.trim().parse().ok()?, total.trim().parse().ok()?);
    if total == 0 {
        return None;
    }
    let last = desc.rsplit(['/', '\\', ' ']).next().unwrap_or("").trim();
    let name = last.strip_suffix(".obj").or_else(|| last.strip_suffix(".o")).unwrap_or(last).to_string();
    Some((done, total, name, desc.contains("Linking")))
}

fn is_build_error(line: &str) -> bool {
    line.contains("error:") || line.starts_with("FAILED:")
}

// ---------------------------------------------------------------------------------------------------------
// The worker

pub struct Worker {
    app: AppHandle,
    shared: Arc<Shared>,
    first_error: String,
}

impl Worker {
    pub fn new(app: AppHandle, shared: Arc<Shared>) -> Worker {
        Worker { app, shared, first_error: String::new() }
    }

    fn emit(&self, event: Event) {
        {
            let mut inner = self.shared.inner.lock().unwrap();
            match &event {
                Event::Status { status } => inner.status = status.clone(),
                Event::Detail { detail } => inner.detail = detail.clone(),
                Event::Phase { phase } => inner.phase = *phase,
                _ => {}
            }
        }
        let _ = self.app.emit("launcher", event);
    }

    fn status(&self, s: impl Into<String>) {
        self.emit(Event::Status { status: s.into() });
    }

    fn detail(&self, s: impl Into<String>) {
        self.emit(Event::Detail { detail: s.into() });
    }

    fn phase(&self, phase: Phase) {
        self.emit(Event::Phase { phase });
    }

    fn step(&self, done: u64, total: u64) {
        let _ = self.app.emit("launcher", Event::Step { done, total });
    }

    fn cancelled(&self) -> bool {
        self.shared.cancel.load(Ordering::SeqCst)
    }

    pub fn run(mut self, play: bool, clean: bool) {
        let mut ok = true;
        if play {
            ok = self.place_disc();
        }
        if ok && !self.cancelled() {
            ok = self.build_game(clean);
        }
        if ok && !self.cancelled() && play {
            ok = self.launch_game();
        } else if ok && !self.cancelled() {
            self.status("The game is rebuilt");
            self.detail("Press Play to start it.");
        }
        let ok = ok && !self.cancelled();
        self.shared.busy.store(false, Ordering::SeqCst);
        self.phase(if ok { Phase::Done } else { Phase::Failed });
        let _ = self.app.emit("launcher", Event::Done { ok });
    }

    /// Runs a command, sending each output line to `on_line` and every byte to the log.
    /// Returns its exit code, or -1 when it could not start.
    fn run_command(&mut self, mut cmd: Command, cwd: &Path, log: &mut Log, on_line: &mut dyn FnMut(&mut Self, &str)) -> i32 {
        log.line("");
        log.line(&describe(&cmd));
        let (mut reader, writer) = match std::io::pipe() {
            Ok(p) => p,
            Err(e) => {
                log.line(&format!("no pipe for its output: {e}"));
                return -1;
            }
        };
        let Ok(writer2) = writer.try_clone() else { return -1 };
        cmd.current_dir(cwd).stdin(Stdio::null()).stdout(writer).stderr(writer2);
        no_window(&mut cmd);
        let mut child = match cmd.spawn() {
            Ok(c) => c,
            Err(e) => {
                log.line(&format!("could not start it ({e})"));
                return -1;
            }
        };
        self.shared.job.assign(&child);
        drop(cmd); // its copies of the pipe's write end: the read below ends when the child's do
        let mut buf = [0u8; 4096];
        let mut line: Vec<u8> = Vec::new();
        loop {
            let n = match reader.read(&mut buf) {
                Ok(0) | Err(_) => break,
                Ok(n) => n,
            };
            log.bytes(&buf[..n]);
            for &c in &buf[..n] {
                if c == b'\n' || c == b'\r' {
                    if !line.is_empty() {
                        let text = String::from_utf8_lossy(&line).into_owned();
                        line.clear();
                        on_line(self, &text);
                    }
                } else if line.len() < 4096 {
                    line.push(c);
                }
            }
        }
        if !line.is_empty() {
            let text = String::from_utf8_lossy(&line).into_owned();
            on_line(self, &text);
        }
        let code = child.wait().ok().and_then(|s| s.code()).unwrap_or(-1);
        log.line(&format!("exit code {code}"));
        code
    }

    /// The player's disc where the game reads it, assets\ctr-u.bin: a hard link on the same drive (instant,
    /// no extra space), else a copy. Their own file is never changed or moved.
    fn place_disc(&mut self) -> bool {
        let disc = self.shared.inner.lock().unwrap().disc.clone();
        if !disc.playable() {
            self.status("Add your Crash Team Racing disc first");
            self.detail("Drop the folder with your .bin/.cue on the window, or choose it in the menu.");
            return false;
        }
        let bin = PathBuf::from(&disc.bin);
        let assets = self.shared.paths.assets();
        let dest = assets.join("ctr-u.bin");
        let _ = fs::create_dir_all(&assets);
        let size = |p: &Path| fs::metadata(p).map(|m| m.len()).ok();
        if dest.exists() {
            if size(&dest).is_some() && size(&dest) == size(&bin) {
                return true; // the same disc, linked or copied before
            }
            // Someone else's file: keep it, under another name.
            if let Some(keep) = (1..100).map(|i| assets.join(format!("ctr-u.bin.old{i}"))).find(|p| !p.exists()) {
                let _ = fs::rename(&dest, keep);
            }
        }
        if fs::hard_link(&bin, &dest).is_ok() {
            return true;
        }
        self.phase(Phase::Copy);
        self.status("Copying your disc into the game folder");
        self.detail(bin.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default());
        match self.copy_with_progress(&bin, &dest) {
            Ok(()) => true,
            Err(_) => {
                let _ = fs::remove_file(&dest);
                self.status(if self.cancelled() { "Stopped" } else { "Could not copy the disc" });
                self.detail("Check there is about 700 MB free next to the launcher.");
                false
            }
        }
    }

    fn copy_with_progress(&self, from: &Path, to: &Path) -> std::io::Result<()> {
        let mut src = fs::File::open(from)?;
        let mut dst = fs::File::create(to)?;
        let total = src.metadata()?.len();
        let mut buf = vec![0u8; 4 << 20];
        let mut done = 0u64;
        loop {
            if self.cancelled() {
                return Err(std::io::Error::other("cancelled"));
            }
            let n = src.read(&mut buf)?;
            if n == 0 {
                break;
            }
            dst.write_all(&buf[..n])?;
            done += n as u64;
            self.step(done >> 20, (total >> 20) + 1);
        }
        dst.sync_all()
    }

    fn build_game(&mut self, clean: bool) -> bool {
        let paths = self.shared.paths.clone();
        let launcher = Ini::load(&paths.launcher_ini);
        let tools = paths.src_root.as_ref().and_then(|_| find_tools(&paths.exe_dir, &launcher));
        let (Some(src), Some(t)) = (paths.src_root.clone(), tools) else {
            if !clean && paths.game_exe().is_some() {
                self.detail("No compiler bundle here; using the game that is already built.");
                return true;
            }
            self.status("The compiler bundle is missing");
            self.detail(if paths.src_root.is_some() {
                "tools\\ (llvm-mingw, CMake, Ninja) should sit next to the launcher. Download the full CTR Launcher package again."
            } else {
                "No game source next to the launcher. Download the full CTR Launcher package again."
            });
            return false;
        };

        let mut log = Log::create(&paths.build_log);
        log.line("CTR Launcher build log");
        self.first_error.clear();

        // Absolute paths go into the build folder: if the source or the compiler moved, start it afresh.
        let stamp = format!("{}|{}", src.display(), t.command.clone().unwrap_or_else(|| t.cc.display().to_string()));
        let cache = paths.build_dir.join("CMakeCache.txt");
        if (clean || launcher.get("launcher", "build_stamp").as_deref() != Some(stamp.as_str()))
            && paths.mode != Mode::Prebuilt
            && cache.exists()
        {
            self.status("Clearing the old build");
            let ours = paths.build_dir.file_name().is_some_and(|n| n.to_string_lossy().starts_with("build"));
            if ours {
                let _ = fs::remove_dir_all(&paths.build_dir);
            }
        }
        let save_stamp = || {
            let mut ini = Ini::load(&paths.launcher_ini);
            ini.set("launcher", "build_stamp", &stamp);
            let _ = ini.save(&paths.launcher_ini);
        };

        let mut rc;
        if let Some(command) = &t.command {
            self.phase(Phase::Build);
            self.status("Recompiling the game");
            let mut cmd = if cfg!(windows) { Command::new("cmd") } else { Command::new("sh") };
            cmd.arg(if cfg!(windows) { "/C" } else { "-c" }).arg(command);
            rc = self.run_command(cmd, &src, &mut log, &mut Self::on_build_line);
            if rc == 0 {
                save_stamp();
            }
        } else {
            let mut path = t.bin_dirs.clone();
            if let Some(old) = std::env::var_os("PATH") {
                path.extend(std::env::split_paths(&old));
            }
            let path = std::env::join_paths(path).unwrap_or_default();
            let with_env = |mut c: Command| {
                c.env("PATH", &path).env("NINJA_STATUS", "[%f/%t] ");
                c
            };
            rc = 0;
            if !cache.exists() {
                self.phase(Phase::Configure);
                self.status("Preparing the build");
                self.detail("Checking the bundled compiler");
                let mut cmd = with_env(Command::new(&t.cmake));
                cmd.arg("-S").arg(&src).arg("-B").arg(&paths.build_dir).args(["-G", "Ninja"]);
                cmd.arg(format!("-DCMAKE_MAKE_PROGRAM={}", forward(&t.ninja)));
                cmd.arg(format!("-DCMAKE_C_COMPILER={}", forward(&t.cc)));
                cmd.arg(format!("-DCMAKE_CXX_COMPILER={}", forward(t.cxx.as_ref().unwrap_or(&t.cc))));
                if let Some(rc) = &t.rc {
                    cmd.arg(format!("-DCMAKE_RC_COMPILER={}", forward(rc)));
                }
                cmd.args(["-DCMAKE_BUILD_TYPE=Release", "-DBUILD_TESTING=OFF"]);
                let mut lines = 0u64;
                rc = self.run_command(cmd, &src, &mut log, &mut |w: &mut Self, line: &str| {
                    lines += 1;
                    w.step(lines.min(59), 60);
                    if w.first_error.is_empty() && line.contains("CMake Error") {
                        w.first_error = line.to_string();
                    }
                });
                if rc != 0 {
                    let _ = fs::remove_file(&cache);
                }
            }
            if rc == 0 {
                save_stamp(); // the build folder now belongs to this source and compiler
            }
            if rc == 0 && !self.cancelled() {
                self.phase(Phase::Build);
                self.status("Recompiling the game from source");
                self.detail("Starting the compiler");
                let mut cmd = with_env(Command::new(&t.cmake));
                cmd.arg("--build").arg(&paths.build_dir);
                rc = self.run_command(cmd, &src, &mut log, &mut Self::on_build_line);
            }
        }
        drop(log);

        if self.cancelled() {
            self.status("Stopped");
            self.detail("The build was cancelled.");
            return false;
        }
        if rc != 0 {
            self.status("The build failed");
            let why = if self.first_error.is_empty() { "See the log.".to_string() } else { self.first_error.clone() };
            self.detail(why);
            return false;
        }
        if paths.game_exe().is_none() {
            self.status("The build finished but ctr_native.exe is missing");
            self.detail("See the log.");
            return false;
        }
        true
    }

    fn on_build_line(&mut self, line: &str) {
        if let Some((done, total, name, linking)) = ninja_step(line) {
            self.step(done, total);
            self.detail(if linking {
                format!("Step {done} of {total} \u{00B7} linking {name}")
            } else {
                format!("Step {done} of {total} \u{00B7} {name}")
            });
        } else if line.contains("no work to do") {
            self.step(1, 1);
            self.detail("Already up to date");
        } else if self.first_error.is_empty() && is_build_error(line) {
            self.first_error = line.to_string();
        }
    }

    fn window(&self, show: bool) {
        if let Some(w) = self.app.get_webview_window("main") {
            if show {
                let _ = w.unminimize();
                let _ = w.set_focus();
            } else {
                let _ = w.minimize();
            }
        }
    }

    fn launch_game(&mut self) -> bool {
        let paths = self.shared.paths.clone();
        let Some(exe) = paths.game_exe() else {
            self.status("ctr_native.exe was not found");
            return false;
        };
        let s = self.shared.inner.lock().unwrap().settings.clone();
        self.phase(Phase::Launch);
        self.status("Starting Crash Team Racing");
        self.detail(format!(
            "{}x{}{}{}",
            s.width,
            s.height,
            if s.widescreen { " \u{00B7} widescreen" } else { " \u{00B7} 4:3" },
            if s.fullscreen { " \u{00B7} fullscreen" } else { "" }
        ));
        let _ = fs::create_dir_all(&paths.log_dir);
        let log = fs::File::create(&paths.game_log).ok();
        let mut cmd = Command::new(&exe);
        cmd.arg("--config").arg(&paths.game_ini).current_dir(exe.parent().unwrap_or(Path::new(".")));
        cmd.stdin(Stdio::null());
        match log.as_ref().and_then(|f| Some((f.try_clone().ok()?, f.try_clone().ok()?))) {
            Some((out, err)) => {
                cmd.stdout(out).stderr(err);
            }
            None => {
                cmd.stdout(Stdio::null()).stderr(Stdio::null());
            }
        }
        no_window(&mut cmd);
        let mut child = match cmd.spawn() {
            Ok(c) => c,
            Err(e) => {
                self.status(format!("Could not start the game ({e})"));
                return false;
            }
        };
        drop(cmd);
        drop(log);
        let start = Instant::now();
        self.phase(Phase::Playing);
        self.status("Playing");
        self.detail("The launcher comes back when you quit the game.");
        self.window(false);
        let code = child.wait().ok().and_then(|s| s.code()).unwrap_or(-1);
        self.window(true);
        if code != 0 {
            self.status(if code < 0 {
                format!("The game stopped (exit code {:#010X})", code as u32)
            } else {
                format!("The game stopped (exit code {code})")
            });
            self.detail(if start.elapsed().as_secs() < 15 {
                "It closed right away. Show log has what it said."
            } else {
                "Show log has what it said."
            });
            return false;
        }
        self.status("Thanks for playing");
        self.detail("Press Play to race again.");
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_ninja_steps() {
        assert_eq!(
            ninja_step("[12/486] Building C object CMakeFiles/x.dir/src/video/SDL_video.c.obj"),
            Some((12, 486, "SDL_video.c".to_string(), false))
        );
        assert_eq!(ninja_step("[486/486] Linking C executable ctr_native.exe"), Some((486, 486, "ctr_native.exe".into(), true)));
        assert_eq!(ninja_step("[a/b] x"), None);
        assert_eq!(ninja_step("-- Configuring done"), None);
        assert!(is_build_error("main.c:1:2: error: boom"));
        assert!(is_build_error("FAILED: x.obj"));
    }

    #[test]
    fn launcher_ini_command_wins() {
        let ini = Ini::parse("[build]\ncommand=make -j8\n");
        let t = find_tools(Path::new("/nowhere"), &ini).unwrap();
        assert_eq!(t.command.as_deref(), Some("make -j8"));
    }
}
