//! The two INI files: ctr_native.ini (the game reads it, platform/native_config.c) and launcher.ini (the
//! launcher's own: the disc, the cover, build overrides). A change keeps every other line, comment and
//! section as it was, the way Windows' profile functions did for the launcher before this one.

use std::fs;
use std::io;
use std::path::Path;

#[derive(Debug, Default, Clone, PartialEq)]
pub struct Ini {
    lines: Vec<String>,
}

/// UTF-8 (with or without a BOM), or UTF-16 LE with its BOM (the old launcher wrote launcher.ini that way).
fn decode(bytes: &[u8]) -> String {
    if bytes.len() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE {
        let units: Vec<u16> = bytes[2..].chunks_exact(2).map(|c| u16::from_le_bytes([c[0], c[1]])).collect();
        return String::from_utf16_lossy(&units);
    }
    let body = bytes.strip_prefix(&[0xEF, 0xBB, 0xBF][..]).unwrap_or(bytes);
    String::from_utf8_lossy(body).into_owned()
}

fn section_name(line: &str) -> Option<&str> {
    let t = line.trim();
    let rest = t.strip_prefix('[')?;
    Some(rest.split(']').next().unwrap_or("").trim())
}

fn key_of(line: &str) -> Option<(&str, &str)> {
    let t = line.trim();
    if t.is_empty() || t.starts_with(';') || t.starts_with('#') || t.starts_with('[') {
        return None;
    }
    let (k, v) = t.split_once('=')?;
    Some((k.trim(), v.trim()))
}

fn unquote(v: &str) -> String {
    let b = v.as_bytes();
    if b.len() >= 2 && ((b[0] == b'"' && b[b.len() - 1] == b'"') || (b[0] == b'\'' && b[b.len() - 1] == b'\'')) {
        return v[1..v.len() - 1].to_string();
    }
    v.to_string()
}

impl Ini {
    pub fn parse(text: &str) -> Ini {
        Ini { lines: text.lines().map(|l| l.to_string()).collect() }
    }

    /// The file, or an empty one when it is missing or unreadable.
    pub fn load(path: &Path) -> Ini {
        Ini::parse(&decode(&fs::read(path).unwrap_or_default()))
    }

    pub fn get(&self, section: &str, key: &str) -> Option<String> {
        let mut inside = false;
        for line in &self.lines {
            if let Some(name) = section_name(line) {
                inside = name.eq_ignore_ascii_case(section);
                continue;
            }
            if inside {
                if let Some((k, v)) = key_of(line) {
                    if k.eq_ignore_ascii_case(key) {
                        return Some(unquote(v));
                    }
                }
            }
        }
        None
    }

    /// A whole number, or `default` when the key is missing or holds no number.
    pub fn get_int(&self, section: &str, key: &str, default: i64) -> i64 {
        let Some(v) = self.get(section, key) else { return default };
        let v = v.trim();
        let digits: String = v
            .char_indices()
            .take_while(|(i, c)| c.is_ascii_digit() || (*i == 0 && (*c == '-' || *c == '+')))
            .map(|(_, c)| c)
            .collect();
        digits.parse().unwrap_or(default)
    }

    pub fn get_bool(&self, section: &str, key: &str, default: bool) -> bool {
        self.get_int(section, key, default as i64) != 0
    }

    /// Sets a key: in place when it is there, else at the end of its section, else in a new section.
    pub fn set(&mut self, section: &str, key: &str, value: &str) {
        let mut start = None;
        let mut end = self.lines.len();
        for (i, line) in self.lines.iter().enumerate() {
            if let Some(name) = section_name(line) {
                if start.is_some() {
                    end = i;
                    break;
                }
                if name.eq_ignore_ascii_case(section) {
                    start = Some(i);
                }
            }
        }
        let entry = format!("{key}={value}");
        let Some(start) = start else {
            if self.lines.last().is_some_and(|l| !l.trim().is_empty()) {
                self.lines.push(String::new());
            }
            self.lines.push(format!("[{section}]"));
            self.lines.push(entry);
            return;
        };
        let mut last_key = start;
        for i in start + 1..end {
            if let Some((k, _)) = key_of(&self.lines[i]) {
                if k.eq_ignore_ascii_case(key) {
                    self.lines[i] = entry;
                    return;
                }
                last_key = i;
            }
        }
        self.lines.insert(last_key + 1, entry);
    }

    pub fn text(&self) -> String {
        let mut out = self.lines.join("\r\n");
        out.push_str("\r\n");
        out
    }

    pub fn save(&self, path: &Path) -> io::Result<()> {
        if let Some(dir) = path.parent() {
            fs::create_dir_all(dir)?;
        }
        fs::write(path, self.text())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn set_keeps_other_lines() {
        let mut ini = Ini::parse("; mine\n[video]\nwidth=800\n\n[other]\nx = 1\n");
        ini.set("video", "width", "1920");
        ini.set("video", "height", "1080");
        ini.set("graphics", "crt", "off");
        assert_eq!(
            ini.text(),
            "; mine\r\n[video]\r\nwidth=1920\r\nheight=1080\r\n\r\n[other]\r\nx = 1\r\n\r\n[graphics]\r\ncrt=off\r\n"
        );
        assert_eq!(ini.get("OTHER", "X").as_deref(), Some("1"));
        assert_eq!(ini.get_int("video", "width", 0), 1920);
        assert_eq!(ini.get_int("video", "missing", 7), 7);
    }

    #[test]
    fn reads_utf16_and_quotes() {
        let text = "[launcher]\r\ndisc=\"D:\\Games\\CTR é\"\r\n";
        let mut bytes = vec![0xFF, 0xFE];
        for u in text.encode_utf16() {
            bytes.extend_from_slice(&u.to_le_bytes());
        }
        let ini = Ini::parse(&decode(&bytes));
        assert_eq!(ini.get("launcher", "disc").as_deref(), Some("D:\\Games\\CTR é"));
        assert_eq!(Ini::parse("[a]\nn=12abc\nm=-3\nz=x\n").get_int("a", "n", 0), 12);
        assert_eq!(Ini::parse("[a]\nm=-3\n").get_int("a", "m", 0), -3);
        assert_eq!(Ini::parse("[a]\nz=x\n").get_int("a", "z", 5), 5);
    }
}
