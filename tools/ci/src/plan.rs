//! Pure supported-plan mapping and affected-product matrix selection.

use std::fs::{self, OpenOptions};
use std::io::Write;
use std::path::{Component, Path, PathBuf};

use anyhow::{Context, Result, bail, ensure};
use clap::Args;
use serde::Serialize;

use crate::model::{
    Architecture, BuildKey, BuildPlan, Check, NativeOptions, Product,
    required_plugin_package_tests, required_runtime_tests,
};

const MAX_CHANGED_PATHS_BYTES: u64 = 16 * 1024 * 1024;
const MAX_MATRIX_OUTPUT_BYTES: u64 = 1024 * 1024;

#[derive(Clone, Debug, Serialize)]
pub struct MatrixRow {
    pub product: Product,
    pub architecture: Architecture,
    pub check: Check,
    pub tool_profile: &'static str,
}

#[derive(Clone, Debug, Serialize)]
pub struct MatrixOutput {
    pub include: Vec<MatrixRow>,
}

#[derive(Debug, Args)]
pub struct PlanArgs {
    #[arg(long, default_value = ".")]
    root: PathBuf,
    #[arg(long, default_value = "build/ci")]
    output: PathBuf,
    #[arg(long, value_enum)]
    product: Product,
    #[arg(long, value_enum)]
    architecture: Architecture,
    #[arg(long, value_enum)]
    check: Check,
    #[arg(long)]
    runtime_directory: Option<PathBuf>,
    #[arg(long)]
    package_plugin: Option<PathBuf>,
    #[arg(long)]
    source_bundle: Option<PathBuf>,
    #[arg(long)]
    release_build: bool,
}

impl PlanArgs {
    /// Resolves and prints a single supported native build plan.
    ///
    /// # Errors
    ///
    /// Returns an error if the repository or selected product/profile is unsupported.
    pub fn run(self) -> Result<()> {
        let repository = dunce::canonicalize(&self.root).with_context(|| {
            format!("could not resolve repository root {}", self.root.display())
        })?;
        let options = NativeOptions {
            runtime_directory: self.runtime_directory,
            package_plugin: self.package_plugin,
            source_bundle: self.source_bundle,
            release_build: self.release_build,
        };
        let plan = create_plan(
            &repository,
            &self.output,
            self.product,
            self.architecture,
            self.check,
            &options,
        )?;
        println!("{}", serde_json::to_string_pretty(&plan)?);
        Ok(())
    }
}

#[derive(Debug, Args)]
pub struct MatrixArgs {
    #[arg(long, default_value = ".")]
    root: PathBuf,
    /// Explicit product to select for qualification matrix.
    #[arg(long, value_enum, conflicts_with = "changed_paths")]
    product: Option<Product>,
    /// NUL-delimited paths from `git diff --name-only --no-renames -z`.
    #[arg(long)]
    changed_paths: Option<PathBuf>,
    /// Select every supported check for the affected product closure.
    #[arg(long)]
    deep: bool,
    /// Append safe GitHub Actions outputs named `matrix` and `has_native`.
    #[arg(long)]
    github_output: Option<PathBuf>,
}

impl MatrixArgs {
    /// Emits the affected native product/check matrix and optional GitHub Actions outputs.
    ///
    /// Invalid or unreadable changed-path input selects the conservative full matrix.
    ///
    /// # Errors
    ///
    /// Returns an error if output encoding or the GitHub output file cannot be written.
    pub fn run(self) -> Result<()> {
        let repository = dunce::canonicalize(&self.root).with_context(|| {
            format!("could not resolve repository root {}", self.root.display())
        })?;
        let output = if let Some(product) = self.product {
            matrix_for_product(product, self.deep)
        } else {
            let changed = self
                .changed_paths
                .as_deref()
                .map(read_changed_paths)
                .transpose();
            match changed {
                Ok(paths) => matrix_for_changes(paths.as_deref(), self.deep),
                Err(_) => matrix_for_changes(None, true),
            }
        };
        let has_native = !output.include.is_empty();
        let encoded = serde_json::to_string(&output)?;
        println!("{encoded}");
        if let Some(path) = self.github_output {
            append_github_output(&path, &encoded, has_native)?;
        }
        let _ = repository;
        Ok(())
    }
}

/// Creates one explicit, product-owned native plan.
///
/// # Errors
///
/// Returns an error when the product, architecture, profile, or typed options are unsupported.
pub fn create_plan(
    repository: &Path,
    output: &Path,
    product: Product,
    architecture: Architecture,
    check: Check,
    options: &NativeOptions,
) -> Result<BuildPlan> {
    ensure!(
        !(options.release_build && options.source_bundle.is_some()),
        "release builds cannot consume a source bundle"
    );
    if let Some(path) = &options.source_bundle {
        ensure!(
            path.is_absolute(),
            "source bundle manifest path must be absolute"
        );
    }
    if let Some(path) = &options.runtime_directory {
        ensure!(
            path.is_absolute(),
            "runtime package bin directory must be absolute"
        );
    }
    if let Some(path) = &options.package_plugin {
        ensure!(path.is_absolute(), "package plugin path must be absolute");
    }

    let preset = preset_for(product, architecture, check)?;
    let repository = dunce::canonicalize(repository)
        .with_context(|| format!("could not resolve repository {}", repository.display()))?;
    let output = resolve_output(&repository, output)?;
    let key = BuildKey {
        product,
        architecture,
        check,
    };
    let slug = build_key_slug(key);
    let mut required_tests = Vec::new();
    if options.runtime_directory.is_some() {
        required_tests.extend(
            required_runtime_tests(product)
                .iter()
                .map(|name| (*name).to_owned()),
        );
    }
    if requires_plugin_test(check) {
        required_tests.extend(
            plugin_test_names(product)
                .iter()
                .map(|name| (*name).to_owned()),
        );
    }
    Ok(BuildPlan {
        key,
        preset: preset.to_owned(),
        source_directory: product.source_directory(&repository),
        build_directory: output.join("builds").join(slug),
        required_tests,
    })
}

/// Returns the full supported product/profile map without reading the filesystem.
#[must_use]
pub fn supported_keys() -> Vec<BuildKey> {
    let mut keys = Vec::new();
    for product in [
        Product::Foundation,
        Product::SkyrimSe,
        Product::SkyrimLe,
        Product::Fallout4,
    ] {
        let architectures = match product {
            Product::Foundation => vec![Architecture::X64, Architecture::X86],
            Product::SkyrimSe | Product::Fallout4 => vec![Architecture::X64],
            Product::SkyrimLe => vec![Architecture::X86],
        };
        for architecture in architectures {
            for check in [
                Check::Tests,
                Check::Quality,
                Check::Analyze,
                Check::Asan,
                Check::Ubsan,
                Check::ClangTests,
                Check::NativeTests,
                Check::NativeQuality,
                Check::NativeAnalyze,
                Check::Package,
            ] {
                if preset_for(product, architecture, check).is_ok() {
                    keys.push(BuildKey {
                        product,
                        architecture,
                        check,
                    });
                }
            }
        }
    }
    keys
}

/// Returns the full affected product closure for a product release or qualification.
///
/// Foundation is the root shared library consumed by all game adapters, so qualifying Foundation
/// requires qualifying Foundation and every dependent adapter (Skyrim SE, Skyrim LE, Fallout 4).
/// Individual game adapters affect only themselves.
#[must_use]
pub fn product_closure(product: Product) -> Vec<Product> {
    match product {
        Product::Foundation => vec![
            Product::Foundation,
            Product::SkyrimSe,
            Product::SkyrimLe,
            Product::Fallout4,
        ],
        Product::SkyrimSe => vec![Product::SkyrimSe],
        Product::SkyrimLe => vec![Product::SkyrimLe],
        Product::Fallout4 => vec![Product::Fallout4],
    }
}

/// Emits the native product/check matrix for a single explicit product and its affected closure.
#[must_use]
pub fn matrix_for_product(product: Product, deep: bool) -> MatrixOutput {
    matrix_for_products(&product_closure(product), deep)
}

/// Emits the native product/check matrix for a slice of explicit products.
#[must_use]
pub fn matrix_for_products(products: &[Product], deep: bool) -> MatrixOutput {
    if products.is_empty() {
        return MatrixOutput {
            include: Vec::new(),
        };
    }
    if deep {
        return MatrixOutput {
            include: supported_keys()
                .into_iter()
                .filter(|key| products.contains(&key.product) && key.check != Check::NativeTests)
                .map(|key| MatrixRow {
                    product: key.product,
                    architecture: key.architecture,
                    check: key.check,
                    tool_profile: tool_profile(key.check),
                })
                .collect(),
        };
    }
    let mut rows = Vec::new();
    for product in products {
        match product {
            Product::Foundation => {
                push_row(
                    &mut rows,
                    *product,
                    Architecture::X64,
                    Check::Tests,
                    "build",
                );
                push_row(
                    &mut rows,
                    *product,
                    Architecture::X86,
                    Check::Tests,
                    "build",
                );
                push_row(
                    &mut rows,
                    *product,
                    Architecture::X64,
                    Check::Quality,
                    "quality",
                );
            }
            Product::SkyrimSe | Product::SkyrimLe | Product::Fallout4 => {
                let architecture = product_architecture(*product);
                push_row(&mut rows, *product, architecture, Check::Tests, "build");
                push_row(
                    &mut rows,
                    *product,
                    architecture,
                    Check::NativeQuality,
                    "quality",
                );
            }
        }
    }
    rows.sort_by_key(|row| {
        (
            row.product.as_str(),
            row.architecture.as_str(),
            row.check.as_str(),
        )
    });
    MatrixOutput { include: rows }
}

/// Selects the minimum coherent native profile set for a changed source closure.
///
/// Missing or invalid input and any unknown path select the full supported matrix.
/// An existing empty path list is an intentional native skip.
#[must_use]
pub fn matrix_for_changes(paths: Option<&[String]>, deep: bool) -> MatrixOutput {
    let Some(paths) = paths else {
        return full_matrix();
    };
    if paths.is_empty() {
        return MatrixOutput {
            include: Vec::new(),
        };
    }

    let mut products = Vec::new();
    for path in paths {
        let Some(scope) = classify_path(path) else {
            return full_matrix();
        };
        match scope {
            PathScope::Foundation => {
                for product in product_closure(Product::Foundation) {
                    push_product(&mut products, product);
                }
            }
            PathScope::Skyrim => {
                push_product(&mut products, Product::SkyrimSe);
                push_product(&mut products, Product::SkyrimLe);
            }
            PathScope::Product(product) => push_product(&mut products, product),
            PathScope::All => {
                return full_matrix();
            }
            PathScope::None => {}
        }
    }
    matrix_for_products(&products, deep)
}

#[must_use]
pub fn build_key_slug(key: BuildKey) -> String {
    format!(
        "{}-{}-{}",
        key.product.as_str(),
        key.architecture.as_str(),
        key.check.as_str()
    )
}

fn preset_for(product: Product, architecture: Architecture, check: Check) -> Result<&'static str> {
    let preset = match (product, architecture, check) {
        (
            Product::Foundation | Product::SkyrimSe | Product::Fallout4,
            Architecture::X64,
            Check::Tests,
        ) => "msvc-x64",
        (Product::Foundation | Product::SkyrimLe, Architecture::X86, Check::Tests) => "msvc-x86",
        (Product::Foundation, Architecture::X64, Check::Quality) => "msvc-x64-quality",
        (
            Product::Foundation | Product::SkyrimSe | Product::Fallout4,
            Architecture::X64,
            Check::Analyze,
        ) => "msvc-x64-analysis",
        (
            Product::Foundation | Product::SkyrimSe | Product::Fallout4,
            Architecture::X64,
            Check::Asan,
        ) => "msvc-x64-asan",
        (
            Product::Foundation | Product::SkyrimSe | Product::Fallout4,
            Architecture::X64,
            Check::Ubsan,
        ) => "clang-cl-x64-ubsan",
        (Product::Foundation, Architecture::X64, Check::Package) => "msvc-x64-release",
        (Product::Foundation, Architecture::X86, Check::Package) => "msvc-x86-release",
        (
            Product::SkyrimSe | Product::Fallout4,
            Architecture::X64,
            Check::NativeTests | Check::Package,
        ) => "msvc-x64-native-tests",
        (Product::SkyrimSe | Product::Fallout4, Architecture::X64, Check::NativeQuality) => {
            "msvc-x64-native-quality"
        }
        (Product::SkyrimLe, Architecture::X86, Check::ClangTests) => "clang-cl-x86",
        (Product::SkyrimLe, Architecture::X86, Check::Analyze) => "msvc-x86-analysis",
        (Product::SkyrimLe, Architecture::X86, Check::NativeQuality) => "msvc-x86-native-quality",
        (Product::SkyrimLe, Architecture::X86, Check::NativeAnalyze) => "msvc-x86-native-analysis",
        (Product::SkyrimLe, Architecture::X86, Check::NativeTests | Check::Package) => {
            "msvc-x86-native-tests"
        }
        (Product::Fallout4, Architecture::X64, Check::NativeAnalyze) => "msvc-x64-native-analysis",
        _ => bail!(
            "check '{}' is not supported for product '{}' architecture '{}'",
            check.as_str(),
            product.as_str(),
            architecture.as_str()
        ),
    };
    Ok(preset)
}

fn requires_plugin_test(check: Check) -> bool {
    matches!(
        check,
        Check::NativeTests | Check::NativeQuality | Check::NativeAnalyze | Check::Package
    )
}

fn plugin_test_names(product: Product) -> &'static [&'static str] {
    required_plugin_package_tests(product)
}

pub(crate) fn resolve_output(repository: &Path, output: &Path) -> Result<PathBuf> {
    let resolved = if output.is_absolute() {
        output.to_path_buf()
    } else {
        repository.join(output)
    };
    let mut normalized = PathBuf::new();
    for component in resolved.components() {
        match component {
            Component::Prefix(prefix) => normalized.push(prefix.as_os_str()),
            Component::RootDir => normalized.push(component.as_os_str()),
            Component::CurDir => {}
            Component::ParentDir => {
                ensure!(normalized.pop(), "output directory escaped its root");
            }
            Component::Normal(part) => normalized.push(part),
        }
    }
    ensure!(
        normalized.is_absolute(),
        "output directory must resolve to an absolute path"
    );
    Ok(normalized)
}

fn read_changed_paths(path: &Path) -> Result<Vec<String>> {
    let metadata = fs::symlink_metadata(path)
        .with_context(|| format!("could not inspect changed-path file {}", path.display()))?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "changed-path input must be a regular file"
    );
    ensure!(
        metadata.len() <= MAX_CHANGED_PATHS_BYTES,
        "changed-path input exceeds the size limit"
    );
    let bytes = fs::read(path).context("could not read changed-path input")?;
    let paths = bytes
        .split(|byte| *byte == 0)
        .filter(|path| !path.is_empty())
        .map(|path| String::from_utf8(path.to_vec()).context("changed path is not UTF-8"))
        .collect::<Result<Vec<_>>>()?;
    ensure!(
        paths.iter().all(|path| safe_relative_path(path)),
        "changed paths contain unsafe input"
    );
    Ok(paths)
}

fn safe_relative_path(path: &str) -> bool {
    let candidate = Path::new(path);
    !path.starts_with(['/', '\\'])
        && !path.contains('\\')
        && path
            .split('/')
            .all(|component| !component.is_empty() && component != "." && component != "..")
        && path.as_bytes().get(1).is_none_or(|byte| *byte != b':')
        && !candidate.is_absolute()
        && candidate
            .components()
            .all(|component| matches!(component, Component::Normal(_)))
}

#[derive(Clone, Copy)]
enum PathScope {
    All,
    Foundation,
    Skyrim,
    Product(Product),
    None,
}

fn classify_path(path: &str) -> Option<PathScope> {
    if !safe_relative_path(path) {
        return None;
    }
    if path.starts_with("adapters/skyrim_se/") {
        return Some(PathScope::Product(Product::SkyrimSe));
    }
    if path.starts_with("adapters/skyrim_le/") {
        return Some(PathScope::Product(Product::SkyrimLe));
    }
    if path.starts_with("adapters/fallout4/") {
        return Some(PathScope::Product(Product::Fallout4));
    }
    if path.starts_with("adapters/skyrim/") {
        return Some(PathScope::Skyrim);
    }
    if path.starts_with("include/")
        || path.starts_with("src/")
        || path.starts_with("tests/")
        || path.starts_with("examples/")
        || path.starts_with("cmake/")
        || path.starts_with("third_party/inputnext/")
    {
        return Some(PathScope::Foundation);
    }
    if matches!(path, "CMakeLists.txt" | "CMakePresets.json") {
        return Some(PathScope::All);
    }
    if path.starts_with(".github/")
        || path.starts_with("tools/ci/")
        || matches!(
            path,
            "Cargo.toml"
                | "Cargo.lock"
                | "rust-toolchain.toml"
                | "clippy.toml"
                | "tools/check_quality.ps1"
                | "tools/verify.ps1"
                | "tools/Invoke-Native.ps1"
        )
    {
        return Some(PathScope::All);
    }
    if path.starts_with("docs/") || matches!(path, "README.md" | "THIRD_PARTY_NOTICES") {
        return Some(PathScope::All);
    }
    if path.starts_with("adapters/") {
        return Some(PathScope::All);
    }
    if matches!(
        path,
        ".clang-format" | ".clang-tidy" | ".gitattributes" | ".gitignore"
    ) {
        return Some(PathScope::All);
    }
    if path.starts_with(".agents/") {
        return Some(PathScope::None);
    }
    Some(PathScope::All)
}

fn push_product(products: &mut Vec<Product>, product: Product) {
    if !products.contains(&product) {
        products.push(product);
    }
}

fn product_architecture(product: Product) -> Architecture {
    match product {
        Product::Foundation | Product::SkyrimSe | Product::Fallout4 => Architecture::X64,
        Product::SkyrimLe => Architecture::X86,
    }
}

fn push_row(
    rows: &mut Vec<MatrixRow>,
    product: Product,
    architecture: Architecture,
    check: Check,
    tool_profile: &'static str,
) {
    if preset_for(product, architecture, check).is_ok() {
        rows.push(MatrixRow {
            product,
            architecture,
            check,
            tool_profile,
        });
    }
}

fn full_matrix() -> MatrixOutput {
    let rows = supported_keys()
        .into_iter()
        .filter(|key| key.check != Check::NativeTests)
        .map(|key| MatrixRow {
            product: key.product,
            architecture: key.architecture,
            check: key.check,
            tool_profile: tool_profile(key.check),
        })
        .collect();
    MatrixOutput { include: rows }
}

fn tool_profile(check: Check) -> &'static str {
    match check {
        Check::Quality | Check::NativeQuality | Check::ClangTests | Check::Ubsan => "quality",
        Check::Package => "release",
        Check::Tests | Check::Analyze | Check::Asan | Check::NativeTests | Check::NativeAnalyze => {
            "build"
        }
    }
}

fn append_github_output(path: &Path, encoded: &str, has_native: bool) -> Result<()> {
    let metadata = fs::symlink_metadata(path)
        .with_context(|| format!("could not inspect GitHub output {}", path.display()))?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink(),
        "GitHub output must be an existing regular file"
    );
    ensure!(
        metadata.len() <= MAX_MATRIX_OUTPUT_BYTES,
        "GitHub output exceeds the size limit"
    );
    let appended_bytes = u64::try_from(encoded.len())?.saturating_add(u64::try_from(
        "matrix=".len() + "\nhas_native=true\n".len(),
    )?);
    ensure!(
        metadata.len().saturating_add(appended_bytes) <= MAX_MATRIX_OUTPUT_BYTES,
        "GitHub output would exceed the size limit"
    );
    let mut output = OpenOptions::new()
        .append(true)
        .open(path)
        .context("could not open GitHub output")?;
    writeln!(output, "matrix={encoded}").context("could not write matrix output")?;
    writeln!(output, "has_native={has_native}").context("could not write native skip output")?;
    output.flush().context("could not flush GitHub output")?;
    Ok(())
}

#[cfg(test)]
#[path = "plan/tests.rs"]
mod tests;
