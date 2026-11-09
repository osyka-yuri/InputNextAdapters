use super::validate_package_consumers;
use crate::model::Product;
use crate::package::{PackageKind, PackageReportEntry};
use serde_json::{Value, json};

fn package(kind: PackageKind, archive_sha256: &str) -> PackageReportEntry {
    PackageReportEntry {
        kind,
        name: format!("{}.zip", kind.as_str()),
        size_bytes: 1024,
        archive_sha256: archive_sha256.to_owned(),
        manifest_sha256: "c".repeat(64),
        entry_count: 1,
        plugin_probe: None,
    }
}

fn consumer_checks(product: Product, source_target: &str, source_sha: &str) -> Value {
    let mut checks = vec![json!({
        "kind": "extracted-source-consumer",
        "archiveSha256": source_sha,
        "configured": true,
        "builtTargets": [source_target],
        "diagnostic": null
    })];
    if product == Product::Foundation {
        checks.push(json!({
            "kind": "installed-relocated-consumer",
            "archiveSha256": "a".repeat(64),
            "configured": true,
            "builtTargets": ["inputnext_consumer"],
            "diagnostic": null
        }));
    }
    json!({ "consumerChecks": checks })
}

#[test]
fn source_consumer_targets_match_each_product() {
    for product in [
        Product::Foundation,
        Product::SkyrimSe,
        Product::SkyrimLe,
        Product::Fallout4,
    ] {
        let binary = package(PackageKind::Binary, &"a".repeat(64));
        let source = package(PackageKind::Source, &"b".repeat(64));
        let evidence = consumer_checks(
            product,
            product.source_consumer_target(),
            &source.archive_sha256,
        );

        let result = validate_package_consumers(&evidence, product, &binary, &source);
        assert!(
            result.is_ok(),
            "{product:?} source consumer should pass: {result:?}"
        );
    }
}

#[test]
fn source_consumer_rejects_wrong_target_or_archive_hash_for_each_product() {
    for product in [
        Product::Foundation,
        Product::SkyrimSe,
        Product::SkyrimLe,
        Product::Fallout4,
    ] {
        let binary = package(PackageKind::Binary, &"a".repeat(64));
        let source = package(PackageKind::Source, &"b".repeat(64));
        let wrong_target = consumer_checks(product, "wrong-target", &source.archive_sha256);
        assert!(validate_package_consumers(&wrong_target, product, &binary, &source).is_err());

        let wrong_hash =
            consumer_checks(product, product.source_consumer_target(), &"f".repeat(64));
        assert!(validate_package_consumers(&wrong_hash, product, &binary, &source).is_err());
    }
}
