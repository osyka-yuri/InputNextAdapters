//! Trusted native dependency metadata, source assembly, and bundle validation.

mod assembly;
mod catalog;
mod filesystem;
mod types;
mod validation;

pub(super) const MAX_RECEIPT_BYTES: u64 = 4 * 1024 * 1024;

pub(super) use assembly::{authored_source_paths, copy_source_bundle};
pub(super) use validation::validate_source_bundle_manifest;
