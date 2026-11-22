//! The cover on the left: a picture the player chose or one found beside their disc, sent to the window as a
//! data: URL (the window reads no files itself). None ships with the launcher.

use base64::Engine;
use std::fs;
use std::path::Path;

const MAX_BYTES: u64 = 40 << 20;

/// The picture's type from its first bytes (PNG, JPEG, GIF, BMP or WebP), whatever its name says.
pub fn mime(head: &[u8]) -> Option<&'static str> {
    if head.starts_with(b"\x89PNG\r\n\x1a\n") {
        Some("image/png")
    } else if head.starts_with(&[0xFF, 0xD8, 0xFF]) {
        Some("image/jpeg")
    } else if head.starts_with(b"GIF87a") || head.starts_with(b"GIF89a") {
        Some("image/gif")
    } else if head.starts_with(b"BM") {
        Some("image/bmp")
    } else if head.len() >= 12 && &head[..4] == b"RIFF" && &head[8..12] == b"WEBP" {
        Some("image/webp")
    } else {
        None
    }
}

pub fn data_url(path: &Path) -> Result<String, String> {
    let bad = || "That picture could not be opened. Use a PNG, JPEG, BMP, GIF or WebP file.".to_string();
    let size = fs::metadata(path).map_err(|_| bad())?.len();
    if size > MAX_BYTES {
        return Err("That picture is over 40 MB. Use a smaller one.".into());
    }
    let bytes = fs::read(path).map_err(|_| bad())?;
    let mime = mime(&bytes).ok_or_else(bad)?;
    Ok(format!("data:{mime};base64,{}", base64::engine::general_purpose::STANDARD.encode(&bytes)))
}

/// A key that changes when the picture does, so the window asks for it again only then.
pub fn key(path: &str) -> String {
    if path.is_empty() {
        return String::new();
    }
    match fs::metadata(path) {
        Ok(m) => {
            let t = m.modified().ok().and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok()).map(|d| d.as_secs());
            format!("{path}|{}|{}", m.len(), t.unwrap_or(0))
        }
        Err(_) => String::new(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn knows_pictures_by_their_bytes() {
        assert_eq!(mime(b"\x89PNG\r\n\x1a\nrest"), Some("image/png"));
        assert_eq!(mime(&[0xFF, 0xD8, 0xFF, 0xE0]), Some("image/jpeg"));
        assert_eq!(mime(b"RIFF\0\0\0\0WEBPVP8 "), Some("image/webp"));
        assert_eq!(mime(b"hello"), None);
    }
}
