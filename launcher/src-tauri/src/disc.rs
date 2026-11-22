//! The player's disc: a folder, a .cue, or the image itself (.bin, .img or .iso) -> the raw image, and whether it
//! is CTR NTSC-U. The name doesn't decide: a raw .iso (2352-byte sectors, as many rippers write them) plays like a
//! .bin; a cooked one (2048-byte sectors) is refused with the reason.
//! The image is only read (a few hundred sectors), never changed.

use serde::Serialize;
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};

pub const SECTOR_RAW: u64 = 2352;
pub const SECTOR_COOKED: u64 = 2048;
/// The file names a disc image goes by.
pub const DISC_EXTS: [&str; 3] = ["bin", "img", "iso"];
const SYNC: [u8; 12] = [0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00];
const MIN_SIZE: u64 = 16 * 1024 * 1024;

#[derive(Serialize, Clone, Debug, Default, PartialEq)]
pub struct DiscInfo {
    /// 0 none yet, 1 CTR NTSC-U, 2 a disc image whose region could not be read (playable), 3 refused.
    pub state: u8,
    pub bin: String,
    pub title: String,
    pub detail: String,
}

impl DiscInfo {
    pub fn playable(&self) -> bool {
        self.state == 1 || self.state == 2
    }
}

/// The boot executable SYSTEM.CNF names ("BOOT = cdrom:\SCUS_944.26;1") and whether the image has raw
/// MODE2/2352 sectors (the ISO 9660 volume descriptor behind a sync pattern at sector 16).
pub fn read_disc_id(path: &Path) -> (String, bool) {
    let mut buf = Vec::new();
    if let Ok(f) = fs::File::open(path) {
        let _ = f.take(SECTOR_RAW * 400).read_to_end(&mut buf);
    }
    let s16 = (SECTOR_RAW * 16) as usize;
    let raw = buf.len() > (SECTOR_RAW * 17) as usize && buf[s16..s16 + 12] == SYNC && &buf[s16 + 25..s16 + 30] == b"CD001";
    let mut i = 0;
    while i + 20 < buf.len() {
        if buf[i..i + 5].eq_ignore_ascii_case(b"cdrom") && buf[i + 5] == b':' {
            let mut j = i + 6;
            while j < buf.len() && (buf[j] == b'\\' || buf[j] == b'/') {
                j += 1;
            }
            let start = j;
            while j < buf.len() && j - start < 63 && buf[j] > b' ' && buf[j] != b';' {
                j += 1;
            }
            if j - start >= 8 {
                return (String::from_utf8_lossy(&buf[start..j]).into_owned(), raw);
            }
        }
        i += 1;
    }
    (String::new(), raw)
}

fn file_name(p: &Path) -> String {
    p.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default()
}

fn starts_ci(s: &str, prefix: &str) -> bool {
    s.len() >= prefix.len() && s.as_bytes()[..prefix.len()].eq_ignore_ascii_case(prefix.as_bytes())
}

pub fn analyze_bin(bin: &Path) -> DiscInfo {
    let size = fs::metadata(bin).map(|m| m.len()).unwrap_or(0);
    let mut info = DiscInfo { bin: bin.to_string_lossy().into_owned(), ..Default::default() };
    if size < MIN_SIZE {
        info.state = 3;
        info.title = "That file is too small to be a disc image".into();
        info.detail = info.bin.clone();
        return info;
    }
    let (id, raw) = read_disc_id(bin);
    if !raw || size % SECTOR_RAW != 0 {
        info.state = 3;
        if is_cooked_iso(bin, size) {
            info.title = "This copy has 2048-byte sectors (a cooked .iso)".into();
            info.detail = "It leaves out the disc's music, voice and video sectors, which the game plays. Copy \
                your disc again as raw sectors: BIN/CUE (MODE2/2352), or an .iso of 2352-byte sectors."
                .into();
        } else {
            info.title = "Not a raw (2352-byte sector) disc image".into();
            info.detail = "Copy your disc as BIN/CUE (MODE2/2352), or an .iso of 2352-byte sectors.".into();
        }
        return info;
    }
    let mb = size / (1024 * 1024);
    if starts_ci(&id, "SCUS_944.26") {
        info.state = 1;
        info.title = "Crash Team Racing (NTSC-U) found".into();
        info.detail = format!("SCUS-94426 \u{00B7} {mb} MB \u{00B7} {}", file_name(bin));
    } else if starts_ci(&id, "SCES_021.05") || starts_ci(&id, "SCPS_101.18") {
        info.state = 3;
        let region = if id.as_bytes().get(2).is_some_and(|c| c.eq_ignore_ascii_case(&b'E')) { "PAL" } else { "Japanese" };
        info.title = format!("This is a {region} copy of CTR");
        info.detail = "CTR Native plays the NTSC-U disc (SCUS-94426). Use that release.".into();
    } else if !id.is_empty() {
        info.state = 3;
        info.title = "This disc is not Crash Team Racing".into();
        info.detail = format!("It boots {id}. CTR Native needs SCUS-94426 (NTSC-U).");
    } else {
        info.state = 2;
        info.title = "Disc image found (region not read)".into();
        info.detail = format!("{mb} MB \u{00B7} {}. It should be the NTSC-U release.", file_name(bin));
    }
    info
}

/// An ISO 9660 image of 2048-byte sectors: its volume descriptor ("\x01CD001") right at sector 16.
fn is_cooked_iso(path: &Path, size: u64) -> bool {
    let mut head = [0u8; 6];
    let at = SECTOR_COOKED * 16;
    size % SECTOR_COOKED == 0
        && fs::File::open(path)
            .and_then(|mut f| {
                use std::io::{Seek, SeekFrom};
                f.seek(SeekFrom::Start(at))?;
                f.read_exact(&mut head)
            })
            .is_ok()
        && &head == b"\x01CD001"
}

/// A .cue -> the first FILE "x.bin" it names, beside it.
pub fn bin_from_cue(cue: &Path) -> Option<PathBuf> {
    let text = fs::read(cue).ok()?;
    let text = String::from_utf8_lossy(&text);
    for line in text.lines() {
        let t = line.trim_start();
        if t.len() >= 4 && t[..4].eq_ignore_ascii_case("FILE") {
            let rest = &t[4..];
            let a = rest.find('"')?;
            let b = rest[a + 1..].find('"')? + a + 1;
            let bin = cue.parent().unwrap_or(Path::new(".")).join(&rest[a + 1..b]);
            return bin.is_file().then_some(bin);
        }
    }
    None
}

pub fn has_ext(p: &Path, exts: &[&str]) -> bool {
    p.extension().and_then(|e| e.to_str()).is_some_and(|e| exts.iter().any(|x| x.eq_ignore_ascii_case(e)))
}

pub const IMAGE_EXTS: [&str; 6] = ["png", "jpg", "jpeg", "bmp", "gif", "webp"];

#[derive(Default, Debug)]
pub struct Scan {
    pub bin: Option<PathBuf>,
    size: u64,
    named: bool,
    raw: bool,
    pub image: Option<PathBuf>,
    image_score: u8,
    seen: usize,
}

/// The folder and two levels below: the best disc image (a raw-sized one first, then a name with CTR or Crash,
/// then the biggest; a cooked one only when nothing else is there, so the player is told why) and the best cover
/// picture (a "front" or "cover" one first).
pub fn scan_folder(dir: &Path, depth: u32, best: &mut Scan) {
    let Ok(entries) = fs::read_dir(dir) else { return };
    for entry in entries.flatten() {
        best.seen += 1;
        if best.seen > 4000 {
            return;
        }
        let path = entry.path();
        let Ok(meta) = entry.metadata() else { continue };
        if meta.is_dir() {
            if depth > 0 {
                scan_folder(&path, depth - 1, best);
            }
            continue;
        }
        let name = file_name(&path);
        let lower = name.to_ascii_lowercase();
        if has_ext(&path, &DISC_EXTS) {
            let size = meta.len();
            let named = lower.contains("ctr") || lower.starts_with("crash");
            let raw = size % SECTOR_RAW == 0;
            if size >= MIN_SIZE && (raw, named, size) > (best.raw, best.named, best.size) {
                best.bin = Some(path);
                best.size = size;
                best.named = named;
                best.raw = raw;
            }
        } else if has_ext(&path, &IMAGE_EXTS) {
            let score = if lower.contains("front") || lower.contains("cover") { 2 } else { 1 };
            if score > best.image_score {
                best.image = Some(path);
                best.image_score = score;
            }
        }
    }
}

/// What the player dropped or picked -> the disc it holds, and a cover picture found beside it.
pub fn choose(path: &Path) -> (DiscInfo, Option<PathBuf>) {
    let mut image = None;
    let bin = if path.is_dir() {
        let mut best = Scan::default();
        scan_folder(path, 2, &mut best);
        image = best.image;
        best.bin
    } else if has_ext(path, &["cue"]) {
        bin_from_cue(path)
    } else if path.is_file() {
        Some(path.to_path_buf())
    } else {
        None
    };
    match bin {
        Some(bin) => (analyze_bin(&bin), image),
        None => (
            DiscInfo {
                state: 3,
                title: "No disc image there".into(),
                detail: "Drop the folder holding your CTR disc image (.bin/.cue or .iso), or the file itself.".into(),
                ..Default::default()
            },
            image,
        ),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn image_with(id: &str, sectors: u64) -> Vec<u8> {
        let mut data = vec![0u8; (SECTOR_RAW * sectors) as usize];
        let s16 = (SECTOR_RAW * 16) as usize;
        data[s16..s16 + 12].copy_from_slice(&SYNC);
        data[s16 + 25..s16 + 30].copy_from_slice(b"CD001");
        let cnf = format!("BOOT = cdrom:\\{id};1\r\n");
        let at = (SECTOR_RAW * 24) as usize;
        data[at..at + cnf.len()].copy_from_slice(cnf.as_bytes());
        data
    }

    #[test]
    fn reads_the_boot_name_and_region() {
        let dir = std::env::temp_dir().join(format!("ctr-launcher-disc-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(dir.join("sub")).unwrap();
        let sectors = MIN_SIZE / SECTOR_RAW + 1;
        fs::write(dir.join("sub").join("Crash Team Racing.bin"), image_with("SCUS_944.26", sectors)).unwrap();
        fs::write(dir.join("other.bin"), image_with("SCES_021.05", sectors)).unwrap();
        fs::write(dir.join("front.jpg"), b"x").unwrap();
        fs::write(dir.join("game.cue"), "FILE \"other.bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n").unwrap();

        let (info, image) = choose(&dir);
        assert_eq!(info.state, 1, "{info:?}");
        assert!(info.bin.ends_with("Crash Team Racing.bin"));
        assert_eq!(image.as_deref(), Some(dir.join("front.jpg").as_path()));

        let (pal, _) = choose(&dir.join("game.cue"));
        assert_eq!((pal.state, pal.title.as_str()), (3, "This is a PAL copy of CTR"));

        fs::write(dir.join("small.bin"), b"tiny").unwrap();
        assert_eq!(choose(&dir.join("small.bin")).0.state, 3);
        assert_eq!(choose(&dir.join("missing")).0.title, "No disc image there");
        let _ = fs::remove_dir_all(&dir);
    }

    #[test]
    fn takes_a_raw_iso_and_explains_a_cooked_one() {
        let dir = std::env::temp_dir().join(format!("ctr-launcher-iso-{}", std::process::id()));
        let _ = fs::remove_dir_all(&dir);
        // Laid out like a download: the .iso two folders down, its raw 2352-byte sectors under an .iso name.
        let inner = dir.join("CTR - Crash Team Racing (USA) (ISO) - 1").join("CTR - Crash Team Racing (USA) (ISO)");
        fs::create_dir_all(&inner).unwrap();
        let iso = inner.join("CTR - Crash Team Racing (USA) (ISO).iso");
        fs::write(&iso, image_with("SCUS_944.26", MIN_SIZE / SECTOR_RAW + 1)).unwrap();
        assert_eq!(choose(&iso).0.state, 1);
        let (info, _) = choose(&dir);
        assert_eq!(info.state, 1, "{info:?}");
        assert!(info.bin.ends_with("(ISO).iso"));

        // A cooked copy: 2048-byte sectors, the volume descriptor right at sector 16.
        let cooked_dir = dir.join("cooked");
        fs::create_dir_all(&cooked_dir).unwrap();
        let mut data = vec![0u8; (MIN_SIZE / SECTOR_COOKED * SECTOR_COOKED + SECTOR_COOKED * 4) as usize];
        if data.len() as u64 % SECTOR_RAW == 0 {
            data.extend_from_slice(&[0u8; SECTOR_COOKED as usize]);
        }
        let at = (SECTOR_COOKED * 16) as usize;
        data[at..at + 6].copy_from_slice(b"\x01CD001");
        fs::write(cooked_dir.join("ctr.iso"), &data).unwrap();
        let (cooked, _) = choose(&cooked_dir);
        assert_eq!(cooked.state, 3);
        assert!(cooked.title.contains("2048-byte sectors"), "{cooked:?}");
        // Beside a raw image, the raw one is taken.
        fs::copy(&iso, cooked_dir.join("zz.bin")).unwrap();
        assert_eq!(choose(&cooked_dir).0.state, 1);
        let _ = fs::remove_dir_all(&dir);
    }
}
