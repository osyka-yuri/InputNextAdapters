use serde::{Deserialize, Serialize};
use std::fmt::{Display, Formatter};
use std::str::FromStr;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum Game {
    SkyrimSe,
    SkyrimLe,
    Fallout4,
}

impl Game {
    #[must_use]
    pub const fn tag(self) -> u32 {
        match self {
            Self::SkyrimSe => 1,
            Self::SkyrimLe => 2,
            Self::Fallout4 => 3,
        }
    }

    #[must_use]
    pub const fn archive_name(self) -> &'static str {
        match self {
            Self::Fallout4 => "Fallout4 - Sounds.ba2",
            Self::SkyrimSe | Self::SkyrimLe => "Skyrim - Sounds.bsa",
        }
    }
}

impl Display for Game {
    fn fmt(&self, formatter: &mut Formatter<'_>) -> std::fmt::Result {
        formatter.write_str(match self {
            Self::SkyrimSe => "skyrim-se",
            Self::SkyrimLe => "skyrim-le",
            Self::Fallout4 => "fallout4",
        })
    }
}

impl FromStr for Game {
    type Err = String;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        match value {
            "skyrim-se" => Ok(Self::SkyrimSe),
            "skyrim-le" => Ok(Self::SkyrimLe),
            "fallout4" => Ok(Self::Fallout4),
            _ => Err(format!(
                "unknown game '{value}' (expected skyrim-se, skyrim-le, or fallout4)"
            )),
        }
    }
}
