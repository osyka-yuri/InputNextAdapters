//! Native build report and release configuration admission.

use std::collections::BTreeMap;
use std::fs;
use std::path::Path;

use anyhow::{Context, Result, ensure};

use crate::model::{
    Architecture, BuildKey, Check, ExecutionContext, NativeOptions, NativeReport, NativeStatus,
    Product, SourceIdentity,
};
use crate::plan::create_plan;
use crate::source::project_version;

use super::MAX_REPORT_BYTES;
use crate::package::PackageMode;
pub(super) fn validate_package_build_inputs(
    product: Product,
    architecture: Architecture,
    version: &str,
    build_directory: &Path,
    context: &ExecutionContext,
    mode: PackageMode,
) -> Result<()> {
    let resolved_version = project_version(&context.repository, product)?.to_string();
    ensure!(
        version == resolved_version,
        "package version does not match the product CMake version"
    );
    ensure!(
        build_directory.is_absolute() && build_directory.is_dir(),
        "package build directory must exist and be absolute"
    );
    let native_output = build_directory
        .parent()
        .and_then(Path::parent)
        .context("package build directory is not beneath a native output root")?;
    let expected = create_plan(
        &context.repository,
        native_output,
        product,
        architecture,
        Check::Package,
        &NativeOptions::default(),
    )?;
    ensure!(
        dunce::canonicalize(&expected.build_directory)? == dunce::canonicalize(build_directory)?,
        "package build directory does not match the fixed product and architecture path"
    );
    validate_native_package_report(
        &context.output,
        BuildKey {
            product,
            architecture,
            check: Check::Package,
        },
        &context.source,
        &expected.required_tests,
    )?;
    if mode == PackageMode::Release {
        ensure!(
            !context.source.modified && context.source.commit.is_some(),
            "release package production requires a clean committed source identity"
        );
        validate_release_build_cache(product, build_directory)?;
    }
    Ok(())
}

fn validate_native_package_report(
    native_output: &Path,
    expected_key: BuildKey,
    expected_source: &SourceIdentity,
    required_tests: &[String],
) -> Result<()> {
    let path = native_output.join("reports").join(format!(
        "{}.json",
        crate::plan::build_key_slug(expected_key)
    ));
    let metadata = fs::symlink_metadata(&path)
        .with_context(|| format!("package creation requires native report {}", path.display()))?;
    ensure!(
        metadata.is_file()
            && !metadata.file_type().is_symlink()
            && metadata.len() <= MAX_REPORT_BYTES,
        "native package report must be a bounded regular file"
    );
    let report: NativeReport =
        crate::json::parse(&fs::read(&path)?).context("native package report is malformed")?;
    ensure!(
        report.schema_version == 1
            && report.key == expected_key
            && report.source == *expected_source
            && report.status == NativeStatus::Passed
            && report.required_tests == required_tests,
        "native package report identity, source, status, or required tests do not match"
    );
    let required = required_tests
        .iter()
        .collect::<std::collections::BTreeSet<_>>();
    let discovered = report
        .discovered_tests
        .iter()
        .collect::<std::collections::BTreeSet<_>>();
    let executed = report
        .executed_tests
        .iter()
        .collect::<std::collections::BTreeSet<_>>();
    ensure!(
        required.len() == required_tests.len()
            && required.is_subset(&discovered)
            && required.is_subset(&executed),
        "native package report did not discover and execute every required test"
    );
    Ok(())
}

fn validate_release_build_cache(product: Product, build_directory: &Path) -> Result<()> {
    let cache_path = build_directory.join("CMakeCache.txt");
    let metadata = fs::symlink_metadata(&cache_path)
        .with_context(|| format!("release package build is missing {}", cache_path.display()))?;
    ensure!(
        metadata.is_file()
            && !metadata.file_type().is_symlink()
            && metadata.len() <= MAX_REPORT_BYTES,
        "release CMake cache must be a bounded regular file"
    );
    let cache = fs::read_to_string(&cache_path).context("could not read release CMake cache")?;
    let values = cache
        .lines()
        .filter_map(|line| {
            let (key, typed) = line.split_once(':')?;
            let (kind, value) = typed.split_once('=')?;
            Some((key, (kind, value)))
        })
        .collect::<BTreeMap<_, _>>();
    ensure!(
        values.get("CMAKE_BUILD_TYPE").map(|(_, value)| *value) == Some("Release"),
        "release package must use a Release build configuration"
    );
    ensure!(
        values
            .get("INPUTNEXTADAPTERS_RELEASE_BUILD")
            .map(|(_, value)| *value)
            == Some("ON"),
        "release package build was not configured with INPUTNEXTADAPTERS_RELEASE_BUILD=ON"
    );
    ensure!(
        values
            .get("INPUTNEXTADAPTERS_PRODUCTION")
            .map(|(_, value)| *value)
            == Some("ON"),
        "release package build must enable production optimization"
    );
    match product {
        Product::Foundation => {}
        Product::SkyrimSe => ensure!(
            values
                .get("INPUTNEXT_SKYRIM_BUILD_PLUGIN")
                .map(|(_, value)| *value)
                == Some("ON"),
            "Skyrim SE release build must enable the native plugin"
        ),
        Product::SkyrimLe => ensure!(
            values
                .get("INPUTNEXT_SKYRIM_LE_BUILD_PLUGIN")
                .map(|(_, value)| *value)
                == Some("ON"),
            "Skyrim LE release build must enable the native plugin"
        ),
        Product::Fallout4 => ensure!(
            values
                .get("INPUTNEXT_FALLOUT4_BUILD_PLUGIN")
                .map(|(_, value)| *value)
                == Some("ON"),
            "Fallout 4 release build must enable the native plugin"
        ),
    }
    for (key, (kind, value)) in &values {
        if key.starts_with("FETCHCONTENT_SOURCE_DIR_") && !value.is_empty() {
            anyhow::bail!("release package CMake cache contains a source override: {key}:{kind}");
        }
    }
    ensure!(
        values
            .get("INPUTNEXTADAPTERS_SOURCE_BUNDLE")
            .is_none_or(|(_, value)| value.is_empty()),
        "release package build cannot consume a source bundle"
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::{validate_native_package_report, validate_release_build_cache};
    use crate::model::{
        Architecture, BuildKey, Check, NativeReport, NativeStatus, Product, SourceIdentity,
    };
    use anyhow::Result;
    use std::fs;

    #[test]
    fn package_requires_matching_successful_native_report_and_exact_probe_inventory() -> Result<()>
    {
        let temporary = tempfile::tempdir()?;
        let output = temporary.path();
        fs::create_dir(output.join("reports"))?;
        let key = BuildKey {
            product: Product::SkyrimSe,
            architecture: Architecture::X64,
            check: Check::Package,
        };
        let source = SourceIdentity {
            commit: Some("a".repeat(40)),
            modified: false,
            snapshot_sha256: None,
        };
        let required = vec!["skyrim_plugin_package".to_owned()];
        let report = NativeReport {
            schema_version: 1,
            key,
            source: source.clone(),
            status: NativeStatus::Passed,
            required_tests: required.clone(),
            discovered_tests: required.clone(),
            executed_tests: required.clone(),
            steps: Vec::new(),
            junit_path: None,
            sarif_paths: Vec::new(),
            diagnostic_paths: Vec::new(),
            failure: None,
        };
        fs::write(
            output.join("reports/skyrim-se-x64-package.json"),
            serde_json::to_vec(&report)?,
        )?;
        validate_native_package_report(output, key, &source, &required)?;
        let incomplete = NativeReport {
            executed_tests: Vec::new(),
            ..report
        };
        fs::write(
            output.join("reports/skyrim-se-x64-package.json"),
            serde_json::to_vec(&incomplete)?,
        )?;
        assert!(validate_native_package_report(output, key, &source, &required).is_err());
        Ok(())
    }

    #[test]
    fn release_packaging_rechecks_marker_and_source_override_cache() -> Result<()> {
        let temporary = tempfile::tempdir()?;
        let cache = temporary.path().join("CMakeCache.txt");
        let valid = "CMAKE_BUILD_TYPE:STRING=Release\nINPUTNEXTADAPTERS_RELEASE_BUILD:BOOL=ON\nINPUTNEXTADAPTERS_PRODUCTION:BOOL=ON\nINPUTNEXT_FALLOUT4_BUILD_PLUGIN:BOOL=ON\nINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH=\n";
        fs::write(&cache, valid)?;
        validate_release_build_cache(Product::Fallout4, temporary.path())?;

        // Source override must be rejected:
        fs::write(
            &cache,
            format!("{valid}FETCHCONTENT_SOURCE_DIR_SPDLOG:PATH=C:/external/spdlog\n"),
        )?;
        assert!(validate_release_build_cache(Product::Fallout4, temporary.path()).is_err());

        // Missing or disabled production optimization must be rejected:
        let no_production = "CMAKE_BUILD_TYPE:STRING=Release\nINPUTNEXTADAPTERS_RELEASE_BUILD:BOOL=ON\nINPUTNEXT_FALLOUT4_BUILD_PLUGIN:BOOL=ON\nINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH=\n";
        fs::write(&cache, no_production)?;
        assert!(validate_release_build_cache(Product::Fallout4, temporary.path()).is_err());

        let production_off = "CMAKE_BUILD_TYPE:STRING=Release\nINPUTNEXTADAPTERS_RELEASE_BUILD:BOOL=ON\nINPUTNEXTADAPTERS_PRODUCTION:BOOL=OFF\nINPUTNEXT_FALLOUT4_BUILD_PLUGIN:BOOL=ON\nINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH=\n";
        fs::write(&cache, production_off)?;
        assert!(validate_release_build_cache(Product::Fallout4, temporary.path()).is_err());

        // Disabled release build marker must be rejected:
        let release_off = "CMAKE_BUILD_TYPE:STRING=Release\nINPUTNEXTADAPTERS_RELEASE_BUILD:BOOL=OFF\nINPUTNEXTADAPTERS_PRODUCTION:BOOL=ON\nINPUTNEXT_FALLOUT4_BUILD_PLUGIN:BOOL=ON\nINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH=\n";
        fs::write(&cache, release_off)?;
        assert!(validate_release_build_cache(Product::Fallout4, temporary.path()).is_err());

        // Debug build type must be rejected:
        let debug_type = "CMAKE_BUILD_TYPE:STRING=Debug\nINPUTNEXTADAPTERS_RELEASE_BUILD:BOOL=ON\nINPUTNEXTADAPTERS_PRODUCTION:BOOL=ON\nINPUTNEXT_FALLOUT4_BUILD_PLUGIN:BOOL=ON\nINPUTNEXTADAPTERS_SOURCE_BUNDLE:FILEPATH=\n";
        fs::write(&cache, debug_type)?;
        assert!(validate_release_build_cache(Product::Fallout4, temporary.path()).is_err());
        Ok(())
    }
}
