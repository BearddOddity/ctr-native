//! The window is drawn by Microsoft Edge WebView2. Windows 11 has it and so do most Windows 10 PCs (Windows Update
//! puts it there), but some don't: Windows 10 LTSC, PCs kept offline. For those the package carries Microsoft's
//! Fixed Version runtime in `webview2\` (tools/launcher/package_windows.py --webview2), so the player needs nothing
//! but their own disc.
//!
//! Windows' own copy comes first, because Windows Update keeps it current. The bundled one is used when Windows has
//! none, or when launcher.ini says `[window] webview2 = bundled`. A WEBVIEW2_BROWSER_EXECUTABLE_FOLDER the person
//! set themselves is left as it is.

use std::path::{Path, PathBuf};

/// The environment variable the WebView2 loader reads for a Fixed Version runtime.
pub const FOLDER_VAR: &str = "WEBVIEW2_BROWSER_EXECUTABLE_FOLDER";

#[derive(Debug, PartialEq)]
pub enum Runtime {
    /// Windows' own (Evergreen) WebView2, with its version.
    System(String),
    /// The Fixed Version runtime that came with the launcher.
    Bundled(PathBuf),
    /// A folder the person named in WEBVIEW2_BROWSER_EXECUTABLE_FOLDER.
    Given(PathBuf),
    /// None found: the window is still tried, and a failure is explained in a message box.
    Missing,
    /// Not Windows: the window is the system's WebKit.
    NotWindows,
}

impl Runtime {
    pub fn describe(&self) -> String {
        match self {
            Runtime::System(v) => format!("Windows' own WebView2 {v}"),
            Runtime::Bundled(p) => format!("the WebView2 that came with the launcher ({})", p.display()),
            Runtime::Given(p) => format!("the WebView2 in {FOLDER_VAR} ({})", p.display()),
            Runtime::Missing => "no WebView2 found".to_string(),
            Runtime::NotWindows => "the system's web view".to_string(),
        }
    }
}

/// Which runtime to use. `setting` is launcher.ini's `[window] webview2`: auto (default), bundled or system.
pub fn choose(given: Option<PathBuf>, system: Option<String>, bundled: Option<PathBuf>, setting: &str) -> Runtime {
    if let Some(dir) = given {
        return Runtime::Given(dir);
    }
    match (setting.trim().to_ascii_lowercase().as_str(), system, bundled) {
        ("bundled", _, Some(dir)) => Runtime::Bundled(dir),
        ("system", Some(v), _) => Runtime::System(v),
        ("system", None, _) => Runtime::Missing,
        (_, Some(v), _) => Runtime::System(v),
        (_, None, Some(dir)) => Runtime::Bundled(dir),
        (_, None, None) => Runtime::Missing,
    }
}

/// The bundled runtime: `webview2\` beside the launcher holding msedgewebview2.exe, or the one folder in it that
/// does (Microsoft's own `Microsoft.WebView2.FixedVersionRuntime.<version>.x64`, dropped in as it unpacks).
pub fn bundled(exe_dir: &Path) -> Option<PathBuf> {
    let root = exe_dir.join("webview2");
    let holds = |d: &Path| d.join("msedgewebview2.exe").is_file();
    if holds(&root) {
        return Some(root);
    }
    let dirs: Vec<PathBuf> = std::fs::read_dir(&root).ok()?.flatten().map(|e| e.path()).filter(|p| p.is_dir()).collect();
    match dirs.as_slice() {
        [one] if holds(one) => Some(one.clone()),
        _ => None,
    }
}

/// Picks the runtime before the window opens and points WebView2 at it. Returns what was picked, for the log.
pub fn prepare(exe_dir: &Path, setting: &str, log_dir: &Path) -> Runtime {
    if !cfg!(windows) {
        return Runtime::NotWindows;
    }
    let given = std::env::var_os(FOLDER_VAR).filter(|v| !v.is_empty()).map(PathBuf::from);
    let picked = choose(given, system_version(), bundled(exe_dir), setting);
    if let Runtime::Bundled(dir) = &picked {
        std::env::set_var(FOLDER_VAR, dir);
        allow_sandbox(dir, log_dir);
    }
    picked
}

/// Windows 10 runs WebView2's renderer in an AppContainer, which can't read a folder unpacked from a zip until
/// "ALL APPLICATION PACKAGES" and "ALL RESTRICTED APPLICATION PACKAGES" may (Microsoft's Fixed Version steps, from
/// version 120). Done once per folder with Windows' own icacls; it harms nothing on Windows 11.
#[cfg(windows)]
fn allow_sandbox(dir: &Path, log_dir: &Path) {
    use std::os::windows::process::CommandExt;
    const CREATE_NO_WINDOW: u32 = 0x0800_0000;
    let stamp = dir.join(".ctr-launcher-acl");
    let here = dir.display().to_string();
    if std::fs::read_to_string(&stamp).map(|s| s.trim() == here).unwrap_or(false) {
        return;
    }
    let icacls = std::env::var_os("SystemRoot")
        .map(|r| PathBuf::from(r).join("System32").join("icacls.exe"))
        .unwrap_or_else(|| PathBuf::from("icacls.exe"));
    let mut ok = true;
    for sid in ["*S-1-15-2-2", "*S-1-15-2-1"] {
        let status = std::process::Command::new(&icacls)
            .arg(dir)
            .arg("/grant")
            .arg(format!("{sid}:(OI)(CI)(RX)"))
            .creation_flags(CREATE_NO_WINDOW)
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status();
        ok &= matches!(status, Ok(s) if s.success());
    }
    if ok {
        let _ = std::fs::write(&stamp, here);
    } else {
        note(log_dir, "icacls could not let WebView2's sandbox read the bundled runtime; the window may stay blank");
    }
}

#[cfg(not(windows))]
fn allow_sandbox(_dir: &Path, _log_dir: &Path) {}

/// The version of Windows' own WebView2, from where Microsoft says to look: `pv` under the runtime's EdgeUpdate
/// client key, machine-wide (WOW6432Node on 64-bit Windows) or for this user. Empty or 0.0.0.0 means none.
#[cfg(windows)]
pub fn system_version() -> Option<String> {
    use windows_sys::Win32::System::Registry::{RegGetValueW, HKEY, HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, RRF_RT_REG_SZ};
    const CLIENT: &str = r"Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
    let wide = |s: &str| s.encode_utf16().chain(Some(0)).collect::<Vec<u16>>();
    let places: [(HKEY, String); 3] = [
        (HKEY_LOCAL_MACHINE, format!(r"SOFTWARE\WOW6432Node\{CLIENT}")),
        (HKEY_LOCAL_MACHINE, format!(r"SOFTWARE\{CLIENT}")),
        (HKEY_CURRENT_USER, format!(r"Software\{CLIENT}")),
    ];
    let value = wide("pv");
    for (root, key) in places {
        let key = wide(&key);
        let mut buf = [0u16; 64];
        let mut size = std::mem::size_of_val(&buf) as u32;
        // SAFETY: the key and value names are NUL-terminated and buf holds `size` bytes.
        let err = unsafe {
            RegGetValueW(root, key.as_ptr(), value.as_ptr(), RRF_RT_REG_SZ, std::ptr::null_mut(), buf.as_mut_ptr().cast(), &mut size)
        };
        if err == 0 {
            let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
            let v = String::from_utf16_lossy(&buf[..len]);
            if !v.is_empty() && v != "0.0.0.0" {
                return Some(v);
            }
        }
    }
    None
}

#[cfg(not(windows))]
pub fn system_version() -> Option<String> {
    None
}

/// A line in logs/launcher.log (what the window was drawn with, and anything that went wrong before it opened).
pub fn note(log_dir: &Path, line: &str) {
    use std::io::Write;
    let _ = std::fs::create_dir_all(log_dir);
    if let Ok(mut f) = std::fs::OpenOptions::new().create(true).append(true).open(log_dir.join("launcher.log")) {
        let _ = writeln!(f, "{line}");
    }
}

/// Starts logs/launcher.log afresh for this run.
pub fn new_log(log_dir: &Path) {
    let _ = std::fs::create_dir_all(log_dir);
    let _ = std::fs::write(log_dir.join("launcher.log"), "");
}

/// What to tell the player when the window can't open.
pub fn missing_text(picked: &Runtime, error: &str) -> String {
    let why = match picked {
        Runtime::Missing => "This PC has no Microsoft Edge WebView2, which draws the launcher's window, and this copy \
            of the launcher came without its own (the webview2 folder beside CTR Launcher.exe). Use the full CTR \
            Launcher package, which has it."
            .to_string(),
        other => format!("It was drawing with {}.", other.describe()),
    };
    format!("The CTR Launcher window could not open.\n\n{why}\n\nDetails: {error}")
}

/// A plain Windows message box: it needs no window of ours and nothing beyond Windows.
#[cfg(windows)]
pub fn message_box(text: &str) {
    use windows_sys::Win32::UI::WindowsAndMessaging::{MessageBoxW, MB_ICONERROR, MB_OK};
    let wide = |s: &str| s.encode_utf16().chain(Some(0)).collect::<Vec<u16>>();
    let (text, title) = (wide(text), wide("CTR Launcher"));
    // SAFETY: both strings are NUL-terminated UTF-16.
    unsafe { MessageBoxW(std::ptr::null_mut(), text.as_ptr(), title.as_ptr(), MB_OK | MB_ICONERROR) };
}

#[cfg(not(windows))]
pub fn message_box(text: &str) {
    eprintln!("{text}");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn windows_own_comes_first() {
        let b = Some(PathBuf::from("webview2"));
        assert_eq!(choose(None, Some("142.0".into()), b.clone(), ""), Runtime::System("142.0".into()));
        assert_eq!(choose(None, None, b.clone(), "auto"), Runtime::Bundled("webview2".into()));
        assert_eq!(choose(None, Some("142.0".into()), b.clone(), "Bundled"), Runtime::Bundled("webview2".into()));
        assert_eq!(choose(None, None, b.clone(), "system"), Runtime::Missing);
        assert_eq!(choose(None, Some("142.0".into()), None, "bundled"), Runtime::System("142.0".into()));
        assert_eq!(choose(None, None, None, ""), Runtime::Missing);
        assert_eq!(choose(Some("x".into()), None, b, "bundled"), Runtime::Given("x".into()));
    }

    #[test]
    fn finds_the_bundled_folder() {
        let dir = std::env::temp_dir().join(format!("ctr-webview-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        assert_eq!(bundled(&dir), None);
        let inner = dir.join("webview2").join("Microsoft.WebView2.FixedVersionRuntime.142.0.1.x64");
        std::fs::create_dir_all(&inner).unwrap();
        assert_eq!(bundled(&dir), None);
        std::fs::write(inner.join("msedgewebview2.exe"), b"MZ").unwrap();
        assert_eq!(bundled(&dir), Some(inner.clone()));
        std::fs::write(dir.join("webview2").join("msedgewebview2.exe"), b"MZ").unwrap();
        assert_eq!(bundled(&dir), Some(dir.join("webview2")));
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn says_what_is_missing() {
        assert!(missing_text(&Runtime::Missing, "x").contains("webview2 folder"));
        assert!(missing_text(&Runtime::System("1".into()), "boom").contains("Details: boom"));
    }
}
