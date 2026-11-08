//! Trusted native dependency catalog and source receipt contracts.

use std::collections::BTreeSet;
use std::path::{Component, Path};

use anyhow::{Context, Result, ensure};
use serde::{Deserialize, Serialize};

use crate::model::Product;

const NATIVE_DEPENDENCIES_JSON: &str = include_str!(concat!(
    env!("CARGO_MANIFEST_DIR"),
    "/../../cmake/NativeDependencies.json"
));

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub(super) struct NativeDependencyCatalog {
    schema_version: u32,
    products: Vec<NativeProduct>,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
struct NativeProduct {
    name: String,
    dependencies: Vec<NativeDependency>,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub(super) struct NativeDependency {
    pub(super) name: String,
    pub(super) repository: String,
    pub(super) revision: String,
    #[serde(rename = "path")]
    pub(super) relative_path: String,
    pub(super) fetch_content: String,
    pub(super) patches: Vec<NativePatch>,
    pub(super) submodules: Vec<NativeSubmodule>,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub(super) struct NativePatch {
    pub(super) id: String,
    pub(super) path: String,
    pub(super) input_sha256: HashPair,
    pub(super) output_sha256: HashPair,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub(super) struct HashPair {
    pub(super) lf: String,
    pub(super) crlf: String,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub(super) struct NativeSubmodule {
    pub(super) name: String,
    pub(super) path: String,
    pub(super) repository: String,
    pub(super) revision: String,
}

pub(super) fn dependencies_for(product: Product) -> Result<Option<Vec<NativeDependency>>> {
    let catalog = parse_catalog(NATIVE_DEPENDENCIES_JSON)
        .context("the compiled native dependency catalog is invalid")?;
    Ok(catalog
        .products
        .into_iter()
        .find(|entry| entry.name == product.as_str())
        .map(|entry| entry.dependencies))
}

fn parse_catalog(contents: &str) -> Result<NativeDependencyCatalog> {
    let catalog: NativeDependencyCatalog = crate::json::parse(contents.as_bytes())
        .context("native dependency catalog is not valid strict JSON")?;
    ensure!(
        catalog.schema_version == 1,
        "native dependency catalog schema is unsupported"
    );
    ensure!(
        !catalog.products.is_empty(),
        "native dependency catalog has no products"
    );

    let mut product_names = BTreeSet::new();
    for product in &catalog.products {
        ensure!(
            !product.name.is_empty() && product_names.insert(product.name.as_str()),
            "native dependency catalog has an empty or duplicate product"
        );
        let mut dependency_names = BTreeSet::new();
        let mut bundle_paths = BTreeSet::new();
        let mut fetch_content_names = BTreeSet::new();
        for dependency in &product.dependencies {
            ensure!(
                !dependency.name.is_empty() && dependency_names.insert(dependency.name.as_str()),
                "native dependency catalog has an empty or duplicate dependency"
            );
            ensure!(
                dependency.repository.starts_with("https://")
                    && dependency.repository.strip_suffix(".git").is_some(),
                "native dependency catalog repository must be an HTTPS Git URL"
            );
            validate_git_revision(&dependency.revision)?;
            validate_bundle_path(&dependency.relative_path)?;
            ensure!(
                bundle_paths.insert(dependency.relative_path.as_str()),
                "native dependency catalog repeats a bundle path"
            );
            ensure!(
                !dependency.fetch_content.is_empty()
                    && !dependency.fetch_content.contains([';', '|', '\r', '\n'])
                    && fetch_content_names.insert(dependency.fetch_content.to_ascii_lowercase()),
                "native dependency catalog has an empty or duplicate FetchContent name"
            );

            let mut submodule_names = BTreeSet::new();
            let mut submodule_paths = BTreeSet::new();
            for submodule in &dependency.submodules {
                ensure!(
                    !submodule.name.is_empty() && submodule_names.insert(submodule.name.as_str()),
                    "native dependency catalog has an empty or duplicate submodule"
                );
                ensure!(
                    submodule.repository.starts_with("https://")
                        && submodule.repository.strip_suffix(".git").is_some(),
                    "native dependency catalog submodule repository must be an HTTPS Git URL"
                );
                validate_git_revision(&submodule.revision)?;
                validate_bundle_path(&submodule.path)?;
                ensure!(
                    submodule_paths.insert(submodule.path.as_str()),
                    "native dependency catalog repeats a submodule path"
                );
            }

            let mut patch_ids = BTreeSet::new();
            let mut patch_paths = BTreeSet::new();
            for patch in &dependency.patches {
                ensure!(
                    !patch.id.is_empty() && patch_ids.insert(patch.id.as_str()),
                    "native dependency catalog has an empty or duplicate amendment"
                );
                validate_bundle_path(&patch.path)?;
                ensure!(
                    patch_paths.insert(patch.path.as_str()),
                    "native dependency catalog repeats an amendment path"
                );
                validate_sha256(&patch.input_sha256.lf)?;
                validate_sha256(&patch.input_sha256.crlf)?;
                validate_sha256(&patch.output_sha256.lf)?;
                validate_sha256(&patch.output_sha256.crlf)?;
            }
        }
    }
    Ok(catalog)
}

fn validate_git_revision(revision: &str) -> Result<()> {
    ensure!(
        revision.len() == 40 && revision.bytes().all(|byte| byte.is_ascii_hexdigit()),
        "native dependency catalog contains an invalid Git revision"
    );
    Ok(())
}

fn validate_sha256(hash: &str) -> Result<()> {
    ensure!(
        hash.len() == 64 && hash.bytes().all(|byte| byte.is_ascii_hexdigit()),
        "native dependency catalog contains an invalid SHA-256"
    );
    Ok(())
}

fn validate_bundle_path(path: &str) -> Result<()> {
    ensure!(
        !path.is_empty()
            && !path.contains(['\\', ':', '\0', ';', '|'])
            && Path::new(path)
                .components()
                .all(|component| matches!(component, Component::Normal(_))),
        "native dependency catalog contains a non-canonical relative path"
    );
    Ok(())
}

#[cfg(test)]
mod tests {
    use anyhow::{Context, Result};
    use serde_json::Value;

    use super::{
        NATIVE_DEPENDENCIES_JSON, NativeDependencyCatalog, dependencies_for, parse_catalog,
    };
    use crate::model::Product;

    fn catalog_value() -> Result<Value> {
        Ok(serde_json::from_str(NATIVE_DEPENDENCIES_JSON)?)
    }

    #[test]
    fn compiled_catalog_resolves_native_products_and_keeps_other_products_independent() -> Result<()>
    {
        let skyrim = dependencies_for(Product::SkyrimSe)?.context("missing Skyrim SE catalog")?;
        let fallout4 = dependencies_for(Product::Fallout4)?.context("missing Fallout 4 catalog")?;
        assert!(
            skyrim
                .iter()
                .any(|dependency| dependency.name == "commonlibsse")
        );
        assert!(
            fallout4
                .iter()
                .any(|dependency| dependency.name == "commonlibf4")
        );
        assert!(dependencies_for(Product::Foundation)?.is_none());
        assert!(dependencies_for(Product::SkyrimLe)?.is_none());
        Ok(())
    }

    #[test]
    fn catalog_rejects_malformed_revision_and_amendment_hash() -> Result<()> {
        let mut malformed_revision = catalog_value()?;
        malformed_revision["products"][0]["dependencies"][0]["revision"] =
            Value::String("moving-branch".to_owned());
        let text = serde_json::to_string(&malformed_revision)?;
        assert!(crate::json::parse::<NativeDependencyCatalog>(text.as_bytes()).is_ok());
        assert!(parse_catalog(&text).is_err());

        let mut malformed_hash = catalog_value()?;
        malformed_hash["products"][1]["dependencies"][0]["patches"][0]["outputSha256"]["lf"] =
            Value::String("not-a-hash".to_owned());
        assert!(parse_catalog(&serde_json::to_string(&malformed_hash)?).is_err());
        Ok(())
    }

    #[test]
    fn catalog_rejects_unknown_fields_and_duplicate_object_keys() -> Result<()> {
        let mut unknown = catalog_value()?;
        unknown["unexpected"] = Value::Bool(true);
        assert!(parse_catalog(&serde_json::to_string(&unknown)?).is_err());
        let tail = NATIVE_DEPENDENCIES_JSON
            .trim_start()
            .strip_prefix('{')
            .context("catalog fixture is not an object")?;
        let duplicate = format!("{{\"schemaVersion\":1,{tail}");
        assert!(parse_catalog(&duplicate).is_err());
        Ok(())
    }
}
