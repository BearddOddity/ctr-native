//! The game's settings, as ctr_native.ini holds them (platform/native_config.c reads the same keys and words).

use serde::{Deserialize, Serialize};

use crate::ini::Ini;

pub const UPSCALE: [&str; 3] = ["sharp", "smooth", "fsr"];
pub const SHARPNESS: [&str; 3] = ["low", "medium", "high"];
pub const ANTIALIASING: [&str; 2] = ["off", "fxaa"];
pub const CRT: [&str; 3] = ["off", "scanlines", "curved"];

#[derive(Serialize, Clone, Copy, Debug, PartialEq)]
pub struct Resolution {
    pub w: i32,
    pub h: i32,
    pub aspect: &'static str,
}

pub const RESOLUTIONS: [Resolution; 10] = [
    Resolution { w: 1280, h: 800, aspect: "16:10" },
    Resolution { w: 1280, h: 960, aspect: "4:3" },
    Resolution { w: 1366, h: 768, aspect: "16:9" },
    Resolution { w: 1440, h: 900, aspect: "16:10" },
    Resolution { w: 1600, h: 900, aspect: "16:9" },
    Resolution { w: 1680, h: 1050, aspect: "16:10" },
    Resolution { w: 1920, h: 1080, aspect: "16:9" },
    Resolution { w: 1920, h: 1200, aspect: "16:10" },
    Resolution { w: 2560, h: 1080, aspect: "21:9" },
    Resolution { w: 2560, h: 1440, aspect: "16:9" },
];

#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct Settings {
    pub width: i32,
    pub height: i32,
    pub fullscreen: bool,
    pub widescreen: bool,
    pub vsync: bool,
    pub intscale: bool,
    pub upscale: usize,
    pub sharpness: usize,
    pub aa: usize,
    pub crt: usize,
    pub brightness: i32,
    pub texfilter: bool,
    pub keyboard: bool,
    pub controller: bool,
    pub xbox: bool,
    pub playstation: bool,
    pub vibration: bool,
}

/// The biggest windowed size that fits the screen's work area with its title bar, not 21:9 or 4:3.
pub fn default_resolution(work_w: i32, work_h: i32) -> (i32, i32) {
    RESOLUTIONS
        .iter()
        .rev()
        .find(|r| r.w <= work_w && r.h + 40 <= work_h && r.aspect != "21:9" && r.aspect != "4:3")
        .map(|r| (r.w, r.h))
        .unwrap_or((RESOLUTIONS[0].w, RESOLUTIONS[0].h))
}

fn choice(ini: &Ini, section: &str, key: &str, words: &[&str], default: usize) -> usize {
    ini.get(section, key)
        .and_then(|v| words.iter().position(|w| w.eq_ignore_ascii_case(v.trim())))
        .unwrap_or(default)
}

impl Settings {
    pub fn load(ini: &Ini, default: (i32, i32)) -> Settings {
        let mut s = Settings {
            width: ini.get_int("video", "width", default.0 as i64) as i32,
            height: ini.get_int("video", "height", default.1 as i64) as i32,
            fullscreen: ini.get_bool("video", "fullscreen", false),
            widescreen: ini.get_bool("video", "widescreen", true),
            vsync: ini.get_bool("video", "vsync", false),
            intscale: ini.get_bool("graphics", "integer_scale", false),
            upscale: choice(ini, "graphics", "upscale", &UPSCALE, 0),
            sharpness: choice(ini, "graphics", "sharpness", &SHARPNESS, 1),
            aa: choice(ini, "graphics", "antialiasing", &ANTIALIASING, 0),
            crt: choice(ini, "graphics", "crt", &CRT, 0),
            brightness: ini.get_int("graphics", "brightness", 0) as i32,
            texfilter: ini.get_bool("graphics", "texture_filter", false),
            keyboard: ini.get_bool("input", "keyboard", true),
            controller: ini.get_bool("input", "controller", true),
            xbox: ini.get_bool("input", "xbox", true),
            playstation: ini.get_bool("input", "playstation", true),
            vibration: ini.get_bool("input", "vibration", true),
        };
        if s.width < 320 || s.height < 240 {
            s.width = default.0;
            s.height = default.1;
        }
        s.tidy();
        s
    }

    /// Values a hand edit or an old file could hold, put back in range; something always drives the kart.
    pub fn tidy(&mut self) {
        if self.upscale >= UPSCALE.len() {
            self.upscale = 0;
        }
        if self.sharpness >= SHARPNESS.len() {
            self.sharpness = 1;
        }
        if self.aa >= ANTIALIASING.len() {
            self.aa = 0;
        }
        if self.crt >= CRT.len() {
            self.crt = 0;
        }
        if !(-2..=2).contains(&self.brightness) {
            self.brightness = 0;
        }
        if self.width < 320 || self.height < 240 {
            self.width = RESOLUTIONS[0].w;
            self.height = RESOLUTIONS[0].h;
        }
        if !self.keyboard && !self.controller {
            self.keyboard = true;
        }
    }

    pub fn save(&self, ini: &mut Ini) {
        let b = |v: bool| if v { "1" } else { "0" };
        ini.set("video", "width", &self.width.to_string());
        ini.set("video", "height", &self.height.to_string());
        ini.set("video", "fullscreen", b(self.fullscreen));
        ini.set("video", "widescreen", b(self.widescreen));
        ini.set("input", "keyboard", b(self.keyboard));
        ini.set("input", "controller", b(self.controller));
        ini.set("input", "xbox", b(self.xbox));
        ini.set("input", "playstation", b(self.playstation));
        ini.set("input", "vibration", b(self.vibration));
        ini.set("video", "vsync", b(self.vsync));
        ini.set("graphics", "upscale", UPSCALE[self.upscale]);
        ini.set("graphics", "sharpness", SHARPNESS[self.sharpness]);
        ini.set("graphics", "antialiasing", ANTIALIASING[self.aa]);
        ini.set("graphics", "crt", CRT[self.crt]);
        ini.set("graphics", "brightness", &self.brightness.to_string());
        ini.set("graphics", "texture_filter", b(self.texfilter));
        ini.set("graphics", "integer_scale", b(self.intscale));
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_and_defaults() {
        let ini = Ini::parse("");
        let mut s = Settings::load(&ini, (1920, 1080));
        assert_eq!((s.width, s.height, s.widescreen, s.sharpness), (1920, 1080, true, 1));
        s.upscale = 2;
        s.crt = 2;
        s.brightness = -1;
        s.keyboard = false;
        s.controller = false;
        s.tidy();
        assert!(s.keyboard);
        let mut out = Ini::parse("[graphics]\n; kept\n");
        s.save(&mut out);
        assert_eq!(out.get("graphics", "upscale").as_deref(), Some("fsr"));
        assert_eq!(out.get("graphics", "crt").as_deref(), Some("curved"));
        assert!(out.text().contains("; kept"));
        assert_eq!(Settings::load(&out, (1, 1)), s);
    }

    #[test]
    fn default_fits_the_screen() {
        assert_eq!(default_resolution(1920, 1040), (1600, 900));
        assert_eq!(default_resolution(2560, 1400), (1920, 1200));
        assert_eq!(default_resolution(1024, 700), (1280, 800));
    }
}
