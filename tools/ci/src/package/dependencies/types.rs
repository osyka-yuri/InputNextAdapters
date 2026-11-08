//! Build receipts and packaged source manifest data.

use std::path::PathBuf;

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub(super) struct DependencyReceipt {
    pub(super) schema_version: u32,
    pub(super) product: String,
    pub(super) dependencies: Vec<ReceiptDependency>,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub(super) struct ReceiptDependency {
    pub(super) name: String,
    pub(super) source_directory: PathBuf,
    pub(super) repository: String,
    pub(super) revision: String,
    pub(super) patches: Vec<ReceiptPatch>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub(super) struct ReceiptPatch {
    pub(super) id: String,
    pub(super) path: String,
    pub(super) sha256: String,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub(super) struct SourceBundleManifest {
    pub(super) schema_version: u32,
    pub(super) kind: String,
    pub(super) product: String,
    pub(super) dependencies: Vec<SourceDependency>,
}

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields, rename_all = "camelCase")]
pub(super) struct SourceDependency {
    pub(super) name: String,
    pub(super) repository: String,
    pub(super) revision: String,
    pub(super) path: String,
    pub(super) fetch_content: String,
    pub(super) patches: Vec<ReceiptPatch>,
    pub(super) submodules: Vec<SourceSubmodule>,
    pub(super) files: Vec<SourceFile>,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub(super) struct SourceSubmodule {
    pub(super) name: String,
    pub(super) path: String,
    pub(super) repository: String,
    pub(super) revision: String,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub(super) struct SourceFile {
    pub(super) path: String,
    pub(super) size: u64,
    pub(super) sha256: String,
}
