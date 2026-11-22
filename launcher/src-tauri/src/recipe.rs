//! Play's build in a package: source\tools\launcher\build-recipe.json run with compiler\, the llvm-mingw cut
//! down to what this port's build reads (tools/launcher/recipe.py makes both). No CMake, no Ninja: every
//! compile, the SDL archive and the link, as CMake and Ninja would run them, several at once, and a step whose
//! command and files didn't change since it last ran is skipped.

use serde::Deserialize;
use std::collections::{BTreeMap, VecDeque};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::process::{Child, Command, Stdio};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{mpsc, Mutex};
use std::time::SystemTime;

pub const FORMAT: u32 = 1;
/// Past this many characters a step's arguments go in a response file (Windows: 32,767 at most).
const RSP_OVER: usize = 8000;
const STATE: &str = ".ctr-recipe.json";

#[derive(Deserialize, Debug, Clone)]
pub struct Step {
    pub out: String,
    pub stage: u32,
    #[serde(default)]
    pub cc: Option<String>,
    #[serde(default)]
    pub flags: Option<usize>,
    #[serde(default)]
    pub src: Option<String>,
    #[serde(default)]
    pub dep: Option<String>,
    #[serde(default)]
    pub run: Vec<Vec<String>>,
}

#[derive(Deserialize, Debug)]
pub struct Recipe {
    pub format: u32,
    pub drivers: BTreeMap<String, Vec<String>>,
    #[serde(default)]
    pub generated: BTreeMap<String, String>,
    pub flags: Vec<Vec<String>>,
    pub steps: Vec<Step>,
    pub exe: String,
}

/// {src}, {build} and {build_id} for this machine.
pub struct Values {
    pub src: String,
    pub build: String,
    pub build_id: String,
}

impl Values {
    pub fn new(src: &Path, build: &Path) -> Values {
        let id = fs::read_to_string(src.join("BUILD_ID")).ok().map(|s| s.trim().to_string()).filter(|s| !s.is_empty());
        Values { src: slashes(src), build: slashes(build), build_id: id.unwrap_or_else(|| "unknown".into()) }
    }

    fn fill(&self, s: &str) -> String {
        s.replace("{src}", &self.src).replace("{build}", &self.build).replace("{build_id}", &self.build_id)
    }
}

/// A path with forward slashes, which clang reads on every host (and in a generated #include line).
pub fn slashes(p: &Path) -> String {
    let s = p.display().to_string();
    let s = s.strip_prefix(r"\\?\").unwrap_or(&s).to_string();
    s.replace('\\', "/")
}

fn program(bin: &Path, name: &str) -> String {
    let file = if cfg!(windows) { format!("{name}.exe") } else { name.to_string() };
    bin.join(file).display().to_string()
}

impl Recipe {
    pub fn load(path: &Path) -> Result<Recipe, String> {
        let text = fs::read_to_string(path).map_err(|e| format!("{}: {e}", path.display()))?;
        let r: Recipe = serde_json::from_str(&text).map_err(|e| format!("{}: {e}", path.display()))?;
        if r.format != FORMAT {
            return Err(format!("{} is format {}, this launcher reads {FORMAT}", path.display(), r.format));
        }
        for s in &r.steps {
            if s.flags.is_some_and(|f| f >= r.flags.len()) || (s.flags.is_none() && s.run.is_empty()) {
                return Err(format!("{}: a broken step for {}", path.display(), s.out));
            }
        }
        Ok(r)
    }

    fn driver(&self, kind: &str, bin: &Path) -> Vec<String> {
        let d = self.drivers.get(kind).cloned().unwrap_or_else(|| vec![kind.to_string()]);
        let mut out = vec![program(bin, &d[0])];
        out.extend(d[1..].iter().cloned());
        out
    }

    /// The commands of one step; "{rm}" first means remove the files named.
    pub fn commands(&self, step: &Step, bin: &Path, v: &Values) -> Vec<Vec<String>> {
        if let (Some(f), Some(src)) = (step.flags, &step.src) {
            let mut cmd = self.driver(step.cc.as_deref().unwrap_or("cc"), bin);
            cmd.extend(self.flags[f].iter().map(|t| v.fill(t)));
            if let Some(dep) = &step.dep {
                cmd.extend(["-MD".into(), "-MT".into(), step.out.clone(), "-MF".into(), dep.clone()]);
            }
            cmd.extend(["-o".into(), step.out.clone(), "-c".into(), v.fill(src)]);
            return vec![cmd];
        }
        step.run
            .iter()
            .map(|op| {
                let args = op[1..].iter().map(|t| v.fill(t));
                if op[0] == "{rm}" {
                    std::iter::once("{rm}".to_string()).chain(args).collect()
                } else {
                    let mut cmd = self.driver(op[0].trim_matches(|c| c == '{' || c == '}'), bin);
                    cmd.extend(args);
                    cmd
                }
            })
            .collect()
    }

    pub fn stages(&self) -> Vec<u32> {
        let mut s: Vec<u32> = self.steps.iter().map(|s| s.stage).collect();
        s.sort();
        s.dedup();
        s
    }
}

/// The files a Make-style depfile (clang -MD) names after its target.
pub fn depfile(text: &str) -> Vec<String> {
    let text = text.replace("\\\r\n", " ").replace("\\\n", " ");
    let rest = match text.find(": ") {
        Some(i) => &text[i + 2..],
        None => return Vec::new(),
    };
    let mut words = Vec::new();
    let mut cur = String::new();
    let mut chars = rest.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\\' && matches!(chars.peek(), Some(' ') | Some('#')) {
            cur.push(chars.next().unwrap());
        } else if c.is_whitespace() {
            if !cur.is_empty() {
                words.push(std::mem::take(&mut cur));
            }
        } else {
            cur.push(c);
        }
    }
    if !cur.is_empty() {
        words.push(cur);
    }
    words
}

/// One argument in a response file, read with --rsp-quoting=posix (the same on every host).
pub fn rsp_quote(arg: &str) -> String {
    format!("\"{}\"", arg.replace('\\', "\\\\").replace('"', "\\\""))
}

/// A stamp of a step's commands (FNV-1a), so a changed command runs again.
pub fn stamp(cmds: &[Vec<String>]) -> String {
    let mut h: u64 = 0xcbf2_9ce4_8422_2325;
    for cmd in cmds {
        for a in cmd {
            for b in a.bytes().chain([0u8]) {
                h ^= b as u64;
                h = h.wrapping_mul(0x0100_0000_01b3);
            }
        }
        h ^= 0xff;
    }
    format!("{h:016x}")
}

fn mtime(p: &Path) -> Option<SystemTime> {
    fs::metadata(p).and_then(|m| m.modified()).ok()
}

fn at(build: &Path, p: &str) -> PathBuf {
    let path = Path::new(p);
    if path.is_absolute() {
        path.to_path_buf()
    } else {
        build.join(path)
    }
}

/// Done already: same command as last time, and its output is newer than every file it read.
pub fn fresh(build: &Path, step: &Step, cmds: &[Vec<String>], state: &BTreeMap<String, String>) -> bool {
    let Some(t) = mtime(&build.join(&step.out)) else { return false };
    if state.get(&step.out) != Some(&stamp(cmds)) {
        return false;
    }
    let inputs: Vec<String> = match &step.dep {
        Some(dep) => match fs::read_to_string(build.join(dep)) {
            Ok(text) => depfile(&text),
            Err(_) => return false,
        },
        None => cmds
            .iter()
            .flat_map(|c| c.iter().skip(1))
            .filter(|w| !w.starts_with('-') && !w.starts_with('{') && build.join(w.as_str()).is_file())
            .cloned()
            .collect(),
    };
    inputs.iter().all(|i| mtime(&at(build, i)).is_some_and(|m| m <= t))
}

/// What the build tells its caller as it goes.
pub enum Note {
    /// A step finished: (done, total, its output, a link or archive rather than a compile).
    Step(u64, u64, String, bool),
    /// What a compile or link printed.
    Output(String),
}

pub type Spawn<'a> = dyn Fn(&mut Command) -> io::Result<Child> + Sync + 'a;

fn strip_ansi(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut chars = s.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\u{1b}' && chars.peek() == Some(&'[') {
            for d in chars.by_ref() {
                if d.is_ascii_alphabetic() {
                    break;
                }
            }
        } else {
            out.push(c);
        }
    }
    out
}

/// Runs one step's commands in the build folder. Ok(text it printed) or Err(text, and the failed command).
fn run_step(build: &Path, n: usize, cmds: &[Vec<String>], spawn: &Spawn) -> Result<String, String> {
    let mut text = String::new();
    for (i, cmd) in cmds.iter().enumerate() {
        if cmd[0] == "{rm}" {
            for p in &cmd[1..] {
                let _ = fs::remove_file(at(build, p));
            }
            continue;
        }
        let mut args: Vec<String> = cmd[1..].to_vec();
        let mut rsp = None;
        if cmd.iter().map(|a| a.len() + 3).sum::<usize>() > RSP_OVER {
            let file = build.join(format!(".ctr-recipe-{n}-{i}.rsp"));
            let body: Vec<String> = args.iter().map(|a| rsp_quote(a)).collect();
            fs::write(&file, body.join("\n") + "\n").map_err(|e| format!("{}: {e}", file.display()))?;
            args = vec!["--rsp-quoting=posix".into(), format!("@{}", file.display())];
            rsp = Some(file);
        }
        let mut c = Command::new(&cmd[0]);
        c.args(&args).current_dir(build).stdin(Stdio::null()).stdout(Stdio::piped()).stderr(Stdio::piped());
        let out = spawn(&mut c).and_then(|child| child.wait_with_output());
        if let Some(f) = rsp {
            let _ = fs::remove_file(f);
        }
        let out = out.map_err(|e| format!("could not start {} ({e})", cmd[0]))?;
        for b in [&out.stdout, &out.stderr] {
            let s = strip_ansi(String::from_utf8_lossy(b).trim());
            if !s.is_empty() {
                text.push_str(&s);
                text.push('\n');
            }
        }
        if !out.status.success() {
            let code = out.status.code().unwrap_or(-1);
            let line: String = cmd.join(" ").chars().take(600).collect();
            return Err(format!("{text}exit {code}: {line}"));
        }
    }
    Ok(text)
}

/// Builds the game into `build` from the recipe with the compiler in `bin`. Returns the first failure's text.
pub fn run(
    recipe: &Recipe,
    bin: &Path,
    v: &Values,
    build: &Path,
    jobs: usize,
    cancel: &AtomicBool,
    spawn: &Spawn,
    note: &mut dyn FnMut(Note),
) -> Result<(), String> {
    fs::create_dir_all(build).map_err(|e| format!("{}: {e}", build.display()))?;
    for (rel, text) in &recipe.generated {
        let path = build.join(rel);
        let text = v.fill(text);
        if fs::read_to_string(&path).ok().as_deref() != Some(text.as_str()) {
            if let Some(dir) = path.parent() {
                let _ = fs::create_dir_all(dir);
            }
            fs::write(&path, text).map_err(|e| format!("{}: {e}", path.display()))?;
        }
    }
    let state_file = build.join(STATE);
    let mut state: BTreeMap<String, String> =
        fs::read_to_string(&state_file).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default();
    let total = recipe.steps.len() as u64;
    let mut done = 0u64;
    let jobs = jobs.max(1);

    for stage in recipe.stages() {
        let mut todo: VecDeque<(usize, &Step, Vec<Vec<String>>)> = VecDeque::new();
        for (i, step) in recipe.steps.iter().enumerate().filter(|(_, s)| s.stage == stage) {
            let cmds = recipe.commands(step, bin, v);
            if fresh(build, step, &cmds, &state) {
                done += 1;
                note(Note::Step(done, total, step.out.clone(), step.flags.is_none()));
                continue;
            }
            for rel in [Some(&step.out), step.dep.as_ref()].into_iter().flatten() {
                if let Some(dir) = build.join(rel).parent() {
                    let _ = fs::create_dir_all(dir);
                }
            }
            todo.push_back((i, step, cmds));
        }
        if todo.is_empty() {
            continue;
        }
        let count = todo.len();
        let queue = Mutex::new(todo);
        let stop = AtomicBool::new(false);
        let (tx, rx) = mpsc::channel::<(usize, Result<String, String>, String)>();
        let mut failed: Option<String> = None;
        std::thread::scope(|scope| {
            for _ in 0..jobs.min(count) {
                let tx = tx.clone();
                let (queue, stop) = (&queue, &stop);
                scope.spawn(move || loop {
                    if stop.load(Ordering::SeqCst) || cancel.load(Ordering::SeqCst) {
                        break;
                    }
                    let Some((i, _, cmds)) = queue.lock().unwrap().pop_front() else { break };
                    let result = run_step(build, i, &cmds, spawn);
                    let key = if result.is_ok() { stamp(&cmds) } else { String::new() };
                    if tx.send((i, result, key)).is_err() {
                        break;
                    }
                });
            }
            drop(tx);
            for (i, result, key) in rx {
                let step = &recipe.steps[i];
                done += 1;
                note(Note::Step(done, total, step.out.clone(), step.flags.is_none()));
                match result {
                    Ok(text) => {
                        if !text.trim().is_empty() {
                            note(Note::Output(text));
                        }
                        state.insert(step.out.clone(), key);
                    }
                    Err(text) => {
                        note(Note::Output(text.clone()));
                        state.remove(&step.out);
                        if failed.is_none() {
                            failed = Some(text);
                        }
                        stop.store(true, Ordering::SeqCst);
                    }
                }
            }
        });
        let _ = fs::write(&state_file, serde_json::to_string_pretty(&state).unwrap_or_default());
        if let Some(text) = failed {
            return Err(text);
        }
        if cancel.load(Ordering::SeqCst) {
            return Err("cancelled".into());
        }
    }
    if !build.join(&recipe.exe).is_file() {
        return Err(format!("the build finished but {} is missing", recipe.exe));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn recipe() -> Recipe {
        serde_json::from_str(
            r##"{"format": 1, "exe": "game.exe",
                "drivers": {"cc": ["clang", "--target=i686-w64-mingw32"], "ar": ["llvm-ar"], "ranlib": ["llvm-ar", "s"]},
                "generated": {"gen/config.h": "#include \"{src}/x.h\"\n"},
                "flags": [["-I{src}/include", "-DID=\"{build_id}\"", "-O2"]],
                "steps": [
                  {"out": "a.c.obj", "stage": 0, "cc": "cc", "flags": 0, "src": "{src}/a.c", "dep": "a.c.obj.d"},
                  {"out": "lib.a", "stage": 1, "run": [["{rm}", "lib.a"], ["{ar}", "qc", "lib.a", "a.c.obj"], ["{ranlib}", "lib.a"]]},
                  {"out": "game.exe", "stage": 2, "run": [["{cc}", "a.c.obj", "-o", "game.exe", "lib.a"]]}
                ]}"##,
        )
        .unwrap()
    }

    fn values() -> Values {
        Values { src: "D:/ctr/source".into(), build: "D:/ctr/build".into(), build_id: "abc123".into() }
    }

    #[test]
    fn builds_each_steps_commands() {
        let r = recipe();
        let bin = Path::new("bin");
        let c = r.commands(&r.steps[0], bin, &values());
        assert_eq!(c.len(), 1);
        assert_eq!(c[0][0], program(bin, "clang"));
        assert_eq!(
            c[0][1..],
            ["--target=i686-w64-mingw32", "-ID:/ctr/source/include", "-DID=\"abc123\"", "-O2", "-MD", "-MT", "a.c.obj", "-MF",
                "a.c.obj.d", "-o", "a.c.obj", "-c", "D:/ctr/source/a.c"]
        );
        let ar = r.commands(&r.steps[1], bin, &values());
        assert_eq!(ar[0], ["{rm}", "lib.a"]);
        assert_eq!(ar[1][0], program(bin, "llvm-ar"));
        assert_eq!(ar[2][1..], ["s", "lib.a"]);
        assert_eq!(r.stages(), vec![0, 1, 2]);
    }

    #[test]
    fn reads_depfiles() {
        let d = "a.c.obj: /src/a.c /src/my\\ dir/b.h \\\n  C:/llvm/include/stdio.h \\\r\n D:/x.h\n";
        assert_eq!(depfile(d), ["/src/a.c", "/src/my dir/b.h", "C:/llvm/include/stdio.h", "D:/x.h"]);
        assert!(depfile("nothing").is_empty());
    }

    #[test]
    fn quotes_response_files_the_same_everywhere() {
        assert_eq!(rsp_quote("a.obj"), "\"a.obj\"");
        assert_eq!(rsp_quote(r#"C:\x y\"q""#), r#""C:\\x y\\\"q\"""#);
    }

    #[test]
    fn a_changed_command_or_newer_input_runs_again() {
        let dir = std::env::temp_dir().join(format!("ctr-recipe-test-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        let r = recipe();
        let step = &r.steps[0];
        let cmds = r.commands(step, Path::new("bin"), &values());
        let mut state = BTreeMap::new();
        assert!(!fresh(&dir, step, &cmds, &state), "no output yet");
        fs::write(dir.join("a.c"), "int x;").unwrap();
        std::thread::sleep(std::time::Duration::from_millis(20));
        fs::write(dir.join("a.c.obj"), "obj").unwrap();
        fs::write(dir.join("a.c.obj.d"), format!("a.c.obj: {}\n", dir.join("a.c").display())).unwrap();
        assert!(!fresh(&dir, step, &cmds, &state), "never stamped");
        state.insert(step.out.clone(), stamp(&cmds));
        assert!(fresh(&dir, step, &cmds, &state));
        let mut other = cmds.clone();
        other[0].push("-g".into());
        assert!(!fresh(&dir, step, &other, &state), "another command");
        std::thread::sleep(std::time::Duration::from_millis(20));
        fs::write(dir.join("a.c"), "int y;").unwrap();
        assert!(!fresh(&dir, step, &cmds, &state), "a newer source");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn refuses_a_recipe_of_another_format_or_a_broken_step() {
        let dir = std::env::temp_dir().join(format!("ctr-recipe-load-{}", std::process::id()));
        fs::create_dir_all(&dir).unwrap();
        let f = dir.join("r.json");
        fs::write(&f, r#"{"format": 2, "exe": "x", "drivers": {}, "flags": [], "steps": []}"#).unwrap();
        assert!(Recipe::load(&f).unwrap_err().contains("format 2"));
        fs::write(&f, r#"{"format": 1, "exe": "x", "drivers": {}, "flags": [], "steps": [{"out": "a", "stage": 0, "flags": 3, "src": "a.c"}]}"#)
            .unwrap();
        assert!(Recipe::load(&f).unwrap_err().contains("broken step"));
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn strips_terminal_colours() {
        assert_eq!(strip_ansi("\u{1b}[0;1;31merror: \u{1b}[0mbad"), "error: bad");
    }

    /// The whole game from the real recipe: CTR_RECIPE_COMPILER=<an llvm-mingw for this machine, cut or not>.
    #[test]
    #[ignore]
    fn builds_the_game() {
        let Ok(tc) = std::env::var("CTR_RECIPE_COMPILER") else { return };
        let src = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..").canonicalize().unwrap();
        let r = Recipe::load(&src.join("tools/launcher/build-recipe.json")).unwrap();
        let build = std::env::temp_dir().join("ctr-recipe-game");
        let _ = fs::remove_dir_all(&build);
        let v = Values::new(&src, &build);
        let cancel = AtomicBool::new(false);
        let mut steps = 0;
        let spawn = |c: &mut Command| c.spawn();
        let result = run(&r, &Path::new(&tc).join("bin"), &v, &build, 8, &cancel, &spawn, &mut |n| {
            if let Note::Step(..) = n {
                steps += 1
            }
        });
        assert_eq!(result, Ok(()));
        assert_eq!(steps, r.steps.len());
        // A second run has nothing to do.
        let exe = build.join("ctr_native.exe");
        let before = mtime(&exe).unwrap();
        let mut output = 0;
        run(&r, &Path::new(&tc).join("bin"), &v, &build, 8, &cancel, &spawn, &mut |n| {
            if let Note::Output(_) = n {
                output += 1
            }
        })
        .unwrap();
        assert_eq!(output, 0);
        assert_eq!(mtime(&exe).unwrap(), before);
    }
}
