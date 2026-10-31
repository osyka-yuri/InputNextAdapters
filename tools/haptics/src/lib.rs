#![forbid(unsafe_code)]

pub mod archive;
pub mod bake;
pub mod bank;
pub mod decoder;
pub mod dsp;
pub(crate) mod glob;
pub mod manifest;
pub mod model;

pub const MAX_RESOURCE_BYTES: usize = 32 * 1024 * 1024;
pub(crate) const MAX_RESOURCE_PATH_BYTES: usize = 1024;
pub const MAX_ARCHIVE_ENTRIES: usize = 100_000;
