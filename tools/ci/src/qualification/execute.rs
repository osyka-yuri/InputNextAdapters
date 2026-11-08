use std::collections::BTreeSet;

use anyhow::{Result, ensure};

use crate::model::{
    BuildPlan, Check, ExecutionContext, NativeOptions, NativeReport, NativeStatus, Product,
    SourceIdentity, required_runtime_tests,
};
use crate::native::run_native;
use crate::plan::create_plan;
use crate::process::Runner;

use super::{QualificationMode, QualificationReport, RuntimePairOrigin, VerifiedRuntimePair};

pub(super) fn qualify_runtime_pair(
    pair: &VerifiedRuntimePair,
    plan: &BuildPlan,
    context: &ExecutionContext,
    required_tests: &[String],
    options: &NativeOptions,
    runner: &mut dyn Runner,
) -> Result<QualificationReport> {
    ensure!(
        plan.key.check == Check::Tests,
        "runtime qualification requires an SDK-free test build plan"
    );
    ensure!(
        options.source_bundle.is_none(),
        "runtime qualification does not accept a source bundle"
    );
    let mode = if options.release_build {
        QualificationMode::Release
    } else {
        QualificationMode::Development
    };
    ensure!(
        mode != QualificationMode::Release || pair.origin == RuntimePairOrigin::Published,
        "release qualification requires a published runtime pair"
    );
    ensure!(
        mode != QualificationMode::Release || is_clean_release_source(&context.source),
        "release qualification requires a clean committed source snapshot"
    );

    let archive = match plan.key.architecture {
        crate::model::Architecture::X64 => &pair.x64,
        crate::model::Architecture::X86 => &pair.x86,
    };
    ensure!(
        options.runtime_directory.as_deref() == Some(archive.bin_directory.as_path()),
        "native runtime directory does not match the verified architecture-specific archive"
    );
    super::payload::verify_pair(pair, &context.output)?;
    super::input::verify_published_pair(pair, context, runner)?;

    let expected_plan = create_plan(
        &context.repository,
        &context.output,
        plan.key.product,
        plan.key.architecture,
        Check::Tests,
        options,
    )?;
    ensure!(
        plan.key == expected_plan.key
            && plan.preset == expected_plan.preset
            && plan.source_directory == expected_plan.source_directory
            && plan.build_directory == expected_plan.build_directory,
        "runtime qualification plan does not match the fixed test mapping"
    );
    ensure!(
        plan.required_tests == expected_plan.required_tests
            && required_tests == expected_plan.required_tests.as_slice(),
        "runtime qualification must execute the exact SDK-free test plan list"
    );
    let mut seen = BTreeSet::new();
    for test in required_tests {
        ensure!(
            seen.insert(test),
            "runtime qualification repeats required test '{test}'"
        );
    }
    for test in required_runtime_tests(plan.key.product) {
        ensure!(
            required_tests
                .iter()
                .filter(|name| name.as_str() == *test)
                .count()
                == 1,
            "runtime qualification must require each exact cold test once"
        );
    }

    let native_report = run_native(plan, context, required_tests, options, runner)?;
    qualification_report(
        plan.key.product,
        plan.key.architecture,
        mode,
        context.source.clone(),
        pair.clone(),
        required_tests.to_vec(),
        native_report,
    )
}

pub(super) fn qualification_report(
    product: Product,
    architecture: crate::model::Architecture,
    mode: QualificationMode,
    source: SourceIdentity,
    runtime_pair: VerifiedRuntimePair,
    required_tests: Vec<String>,
    native_report: NativeReport,
) -> Result<QualificationReport> {
    ensure!(
        native_report.schema_version == 1
            && native_report.key.product == product
            && native_report.key.architecture == architecture
            && native_report.key.check == Check::Tests
            && native_report.source == source,
        "native report identity does not match the requested qualification"
    );
    ensure!(
        native_report.required_tests == required_tests,
        "native report required tests differ from qualification requirements"
    );
    let required = unique_test_set(&required_tests, "required")?;
    let discovered = unique_test_set(&native_report.discovered_tests, "discovered")?;
    let executed = unique_test_set(&native_report.executed_tests, "executed")?;
    if native_report.status == NativeStatus::Passed {
        ensure!(
            required.is_subset(&discovered) && required.is_subset(&executed),
            "passed native report does not contain every required discovered and executed test"
        );
    }
    let release_eligible = mode == QualificationMode::Release
        && runtime_pair.origin == RuntimePairOrigin::Published
        && is_clean_release_source(&source)
        && native_report.status == NativeStatus::Passed
        && required == executed
        && required.is_subset(&discovered);
    Ok(QualificationReport {
        schema_version: 1,
        product,
        architecture,
        mode,
        source,
        runtime_pair,
        required_tests,
        discovered_tests: native_report.discovered_tests.clone(),
        executed_tests: native_report.executed_tests.clone(),
        native_report,
        release_eligible,
    })
}

pub(super) fn is_clean_release_source(source: &SourceIdentity) -> bool {
    let Some(commit) = source.commit.as_deref() else {
        return false;
    };
    !source.modified
        && source.snapshot_sha256.is_none()
        && matches!(commit.len(), 40 | 64)
        && commit
            .bytes()
            .all(|byte| byte.is_ascii_digit() || (b'a'..=b'f').contains(&byte))
}

fn unique_test_set<'a>(names: &'a [String], label: &str) -> Result<BTreeSet<&'a str>> {
    let set = names.iter().map(String::as_str).collect::<BTreeSet<_>>();
    ensure!(
        set.len() == names.len(),
        "native report contains duplicate {label} tests"
    );
    Ok(set)
}

#[cfg(test)]
mod tests {
    use super::is_clean_release_source;
    use crate::model::SourceIdentity;

    #[test]
    fn release_source_requires_a_clean_canonical_commit() {
        let clean = SourceIdentity {
            commit: Some("a".repeat(40)),
            modified: false,
            snapshot_sha256: None,
        };
        assert!(is_clean_release_source(&clean));

        let mut invalid = clean.clone();
        invalid.snapshot_sha256 = Some("b".repeat(64));
        assert!(!is_clean_release_source(&invalid));
        let mut invalid = clean.clone();
        invalid.commit = Some("A".repeat(40));
        assert!(!is_clean_release_source(&invalid));
        let mut invalid = clean;
        invalid.modified = true;
        assert!(!is_clean_release_source(&invalid));
    }
}
