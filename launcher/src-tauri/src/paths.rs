//! Where things are. Three ways to run the launcher:
//!   package   the release folder: source\ beside the launcher, the build in build\, data beside it;
//!   dev       a ctr-native checkout (the launcher built under launcher\src-tauri\target\...): the build in
//!             build-launcher\ of that checkout;
//!   prebuilt  neither: plays a ctr_native.exe that sits beside the launcher.

use serde::Serialize;
use std::fs;
use std::path::{Path, PathBuf};

#[derive(Serialize, Clone, Copy, Debug, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum Mode {
    Package,
    Dev,
    Prebuilt,
}

#[derive(Clone, Debug)]
pub struct Paths {
    pub mode: Mode,
    pub exe_dir: PathBuf,
    pub src_root: Option<PathBuf>,
    pub data_root: PathBuf,
    pub build_dir: PathBuf,
    pub game_ini: PathBuf,
    pub launcher_ini: PathBuf,
    pub log_dir: PathBuf,
    pub build_log: PathBuf,
    pub game_log: PathBuf,
}

/// A ctr-native source tree: its CMakeLists.txt names the project, and main.c is there.
pub fn looks_like_source(dir: &Path) -> bool {
    let Ok(head) = fs::read(dir.join("CMakeLists.txt")) else { return false };
    let head = &head[..head.len().min(4096)];
    String::from_utf8_lossy(head).contains("CTR-Native") && dir.join("main.c").is_file()
}

impl Paths {
    pub fn find(exe_dir: &Path) -> Paths {
        let package = exe_dir.join("source");
        let (mode, src_root, data_root, build_dir) = if looks_like_source(&package) {
            (Mode::Package, Some(package), exe_dir.to_path_buf(), exe_dir.join("build"))
        } else if let Some(root) = exe_dir.ancestors().take(7).find(|d| looks_like_source(d)) {
            (Mode::Dev, Some(root.to_path_buf()), root.to_path_buf(), root.join("build-launcher"))
        } else {
            (Mode::Prebuilt, None, exe_dir.to_path_buf(), exe_dir.to_path_buf())
        };
        let log_dir = data_root.join("logs");
        Paths {
            mode,
            exe_dir: exe_dir.to_path_buf(),
            game_ini: data_root.join("ctr_native.ini"),
            launcher_ini: data_root.join("launcher.ini"),
            build_log: log_dir.join("build.log"),
            game_log: log_dir.join("game.log"),
            log_dir,
            src_root,
            data_root,
            build_dir,
        }
    }

    pub fn assets(&self) -> PathBuf {
        self.data_root.join("assets")
    }

    /// The game the last build made (or one shipped beside the launcher).
    pub fn game_exe(&self) -> Option<PathBuf> {
        let name = format!("ctr_native{}", std::env::consts::EXE_SUFFIX);
        let mut candidates = vec![self.build_dir.join(&name), self.exe_dir.join(&name)];
        if let Some(src) = &self.src_root {
            candidates.push(src.join(&name));
            candidates.push(src.join("build-clangcl-x86").join(&name));
            candidates.push(src.join("build").join(&name));
            candidates.push(src.join("build-msvc-x86").join("Release").join(&name));
        }
        candidates.into_iter().find(|p| p.is_file())
    }

    /// The newer of the build log and the game's log.
    pub fn newest_log(&self) -> Option<PathBuf> {
        let modified = |p: &Path| fs::metadata(p).and_then(|m| m.modified()).ok();
        match (modified(&self.build_log), modified(&self.game_log)) {
            (Some(b), Some(g)) => Some(if g > b { self.game_log.clone() } else { self.build_log.clone() }),
            (Some(_), None) => Some(self.build_log.clone()),
            (None, Some(_)) => Some(self.game_log.clone()),
            (None, None) => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn finds_the_mode() {
        let root = std::env::temp_dir().join(format!("ctr-launcher-paths-{}", std::process::id()));
        let _ = fs::remove_dir_all(&root);
        let src = root.join("checkout");
        let deep = src.join("launcher").join("src-tauri").join("target").join("release");
        fs::create_dir_all(&deep).unwrap();
        fs::write(src.join("CMakeLists.txt"), "project(CTR-Native C)\n").unwrap();
        fs::write(src.join("main.c"), "").unwrap();
        let dev = Paths::find(&deep);
        assert_eq!(dev.mode, Mode::Dev);
        assert_eq!(dev.build_dir, src.join("build-launcher"));

        let pkg = root.join("pkg");
        fs::create_dir_all(pkg.join("source")).unwrap();
        fs::copy(src.join("CMakeLists.txt"), pkg.join("source").join("CMakeLists.txt")).unwrap();
        fs::write(pkg.join("source").join("main.c"), "").unwrap();
        let p = Paths::find(&pkg);
        assert_eq!((p.mode, p.build_dir.clone()), (Mode::Package, pkg.join("build")));
        assert_eq!(p.game_ini, pkg.join("ctr_native.ini"));

        let lone = root.join("lone");
        fs::create_dir_all(&lone).unwrap();
        assert_eq!(Paths::find(&lone).mode, Mode::Prebuilt);
        let _ = fs::remove_dir_all(&root);
    }
}
