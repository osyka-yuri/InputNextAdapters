//! Locked tool search paths combined with the active compiler environment.

use std::collections::{BTreeMap, BTreeSet};
use std::env;
use std::ffi::{OsStr, OsString};

use anyhow::{Context, Result};

use super::Toolset;

pub(crate) fn command_environment(toolset: &Toolset) -> Result<BTreeMap<OsString, OsString>> {
    merge_tool_environment(toolset.environment(), &env::vars_os().collect())
}

pub(crate) fn merge_tool_environment(
    locked: &BTreeMap<String, String>,
    inherited: &BTreeMap<OsString, OsString>,
) -> Result<BTreeMap<OsString, OsString>> {
    let mut environment = locked
        .iter()
        .map(|(key, value)| (OsString::from(key), OsString::from(value)))
        .collect::<BTreeMap<_, _>>();
    for variable in ["PATH", "PSModulePath"] {
        let Some(value) = locked.get(variable) else {
            continue;
        };
        let current = inherited.iter().find_map(|(key, value)| {
            let matches = if cfg!(windows) {
                key.as_encoded_bytes()
                    .eq_ignore_ascii_case(variable.as_bytes())
            } else {
                key == OsStr::new(variable)
            };
            matches.then_some(value.as_os_str())
        });
        environment.insert(
            variable.into(),
            merge_search_path(OsStr::new(value), current)
                .with_context(|| format!("could not construct toolchain {variable}"))?,
        );
    }
    Ok(environment)
}

fn merge_search_path(locked: &OsStr, inherited: Option<&OsStr>) -> Result<OsString> {
    let mut directories = env::split_paths(locked).collect::<Vec<_>>();
    if let Some(inherited) = inherited {
        directories.extend(env::split_paths(inherited));
    }
    let mut seen = BTreeSet::new();
    directories.retain(|directory| {
        let identity = if cfg!(windows) {
            directory.to_string_lossy().to_ascii_lowercase()
        } else {
            directory.to_string_lossy().into_owned()
        };
        seen.insert(identity)
    });
    env::join_paths(directories).map_err(Into::into)
}

#[cfg(test)]
mod tests {
    use super::{merge_search_path, merge_tool_environment};
    use anyhow::Result;
    use std::collections::BTreeMap;
    use std::env;
    use std::ffi::OsString;

    #[test]
    fn locked_tools_precede_the_active_compiler_without_duplicate_paths() -> Result<()> {
        let temporary = tempfile::tempdir()?;
        let first = temporary.path().join("locked tools");
        let second = temporary.path().join("locked compiler");
        let active = temporary.path().join("active compiler");
        let locked = env::join_paths([&first, &second])?;
        let inherited = env::join_paths([&second, &active])?;
        let merged = merge_search_path(&locked, Some(&inherited))?;
        assert_eq!(
            env::split_paths(&merged).collect::<Vec<_>>(),
            [first, second, active]
        );
        Ok(())
    }

    #[test]
    fn inherited_variable_names_follow_host_case_rules() -> Result<()> {
        let temporary = tempfile::tempdir()?;
        let locked_path = temporary.path().join("locked");
        let compiler_path = temporary.path().join("compiler");
        let locked = BTreeMap::from([(
            "PATH".to_owned(),
            env::join_paths([&locked_path])?
                .into_string()
                .map_err(|_| anyhow::anyhow!("test path is not Unicode"))?,
        )]);
        for inherited_name in ["PATH", "Path"] {
            let inherited = BTreeMap::from([(
                OsString::from(inherited_name),
                env::join_paths([&compiler_path])?,
            )]);
            let merged = merge_tool_environment(&locked, &inherited)?;
            let actual = env::split_paths(&merged[&OsString::from("PATH")]).collect::<Vec<_>>();
            let mut expected = vec![locked_path.clone()];
            if inherited_name == "PATH" || cfg!(windows) {
                expected.push(compiler_path.clone());
            }
            assert_eq!(actual, expected);
        }
        Ok(())
    }
}
