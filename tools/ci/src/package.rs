//! Product package creation and static archive verification.

mod archive;
mod builder;
mod consumers;
mod dependencies;
mod inspect;
mod pe;
mod types;

pub use builder::{PackageArgs, build_package};
pub use inspect::inspect_package_archive;
pub use types::{
    ConsumerEvidence, PackageExpectation, PackageKind, PackageMode, PackageReport,
    PackageReportEntry, PluginProbeEvidence, VerifiedPackage,
};
