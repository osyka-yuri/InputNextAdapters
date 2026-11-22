use serde::{Deserialize, Serialize};
use std::fmt::{Display, Formatter};
use std::str::FromStr;

#[derive(Clone, Copy, Debug, Eq, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "kebab-case")]
pub enum Game {
    SkyrimSe,
    SkyrimLe,
    Fallout4,
    Oblivion,
    FalloutNewVegas,
}

impl Game {
    #[must_use]
    pub const fn tag(self) -> u32 {
        match self {
            Self::SkyrimSe => 1,
            Self::SkyrimLe => 2,
            Self::Fallout4 => 3,
            Self::Oblivion => 4,
            Self::FalloutNewVegas => 5,
        }
    }

    #[must_use]
    pub const fn archive_name(self) -> &'static str {
        match self {
            Self::Fallout4 => "Fallout4 - Sounds.ba2",
            Self::Oblivion => "Oblivion - Sounds.bsa",
            Self::FalloutNewVegas => "Fallout - Sound.bsa",
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
            Self::Oblivion => "oblivion",
            Self::FalloutNewVegas => "fallout-new-vegas",
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
            "oblivion" => Ok(Self::Oblivion),
            "fallout-new-vegas" => Ok(Self::FalloutNewVegas),
            _ => Err(format!(
                "unknown game '{value}' (expected skyrim-se, skyrim-le, fallout4, oblivion, or fallout-new-vegas)"
            )),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::Game;

    #[test]
    fn oblivion_has_stable_tag_and_default_archive_name() {
        assert_eq!("oblivion".parse::<Game>(), Ok(Game::Oblivion));
        assert_eq!(Game::Oblivion.to_string(), "oblivion");
        assert_eq!(Game::Oblivion.tag(), 4);
        assert_eq!(Game::Oblivion.archive_name(), "Oblivion - Sounds.bsa");
    }

    #[test]
    fn fallout_new_vegas_has_stable_tag_and_default_archive_name() {
        assert_eq!(
            "fallout-new-vegas".parse::<Game>(),
            Ok(Game::FalloutNewVegas)
        );
        assert_eq!(Game::FalloutNewVegas.to_string(), "fallout-new-vegas");
        assert_eq!(Game::FalloutNewVegas.tag(), 5);
        assert_eq!(Game::FalloutNewVegas.archive_name(), "Fallout - Sound.bsa");
    }
}
