//! `CTest` discovery and `JUnit` result parsing for exact test accounting.

use std::collections::BTreeSet;
use std::io::Cursor;

use anyhow::{Context, Result, ensure};
use quick_xml::Reader;
use quick_xml::XmlVersion;
use quick_xml::events::{BytesStart, Event};

const MAX_JUNIT_BYTES: usize = 64 * 1024 * 1024;

#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) struct CTestListing {
    pub names: Vec<String>,
    pub disabled: BTreeSet<String>,
}

#[derive(Clone, Debug, Eq, PartialEq)]
pub(super) struct JUnitCase {
    pub name: String,
    pub skipped: bool,
    pub failed: bool,
}

pub(super) fn parse_ctest_listing(bytes: &[u8]) -> Result<CTestListing> {
    let value = crate::json::parse_slice(bytes).context("CTest listing is not valid JSON")?;
    let tests = value
        .get("tests")
        .and_then(serde_json::Value::as_array)
        .context("CTest listing has no test array")?;
    let mut names = Vec::with_capacity(tests.len());
    let mut disabled = BTreeSet::new();
    let mut seen = BTreeSet::new();
    for test in tests {
        let name = test
            .get("name")
            .and_then(serde_json::Value::as_str)
            .context("CTest listing contains a test without a name")?
            .to_owned();
        ensure!(
            !name.is_empty(),
            "CTest listing contains an empty test name"
        );
        ensure!(
            seen.insert(name.clone()),
            "CTest listing has duplicate test '{name}'"
        );
        let properties = test
            .get("properties")
            .and_then(serde_json::Value::as_array)
            .cloned()
            .unwrap_or_default();
        let is_disabled = properties.iter().any(|property| {
            property.get("name").and_then(serde_json::Value::as_str) == Some("DISABLED")
                && property.get("value").is_some_and(is_truthy_json_value)
        });
        if is_disabled {
            disabled.insert(name.clone());
        }
        names.push(name);
    }
    Ok(CTestListing { names, disabled })
}

fn is_truthy_json_value(value: &serde_json::Value) -> bool {
    match value {
        serde_json::Value::Bool(value) => *value,
        serde_json::Value::String(value) => matches!(value.as_str(), "ON" | "TRUE" | "1"),
        serde_json::Value::Number(value) => value.as_i64().is_some_and(|value| value != 0),
        _ => false,
    }
}

pub(super) fn parse_junit(bytes: &[u8]) -> Result<Vec<JUnitCase>> {
    ensure!(
        bytes.len() <= MAX_JUNIT_BYTES,
        "CTest JUnit report exceeds the size limit"
    );
    let mut reader = Reader::from_reader(Cursor::new(bytes));
    reader.config_mut().trim_text(true);
    let mut buffer = Vec::new();
    let mut cases = Vec::new();
    let mut current: Option<JUnitCase> = None;
    loop {
        match reader
            .read_event_into(&mut buffer)
            .context("could not parse CTest JUnit report")?
        {
            Event::Start(start) => match start.name().as_ref() {
                "testcase" => {
                    ensure!(current.is_none(), "CTest JUnit test cases are nested");
                    current = Some(junit_case(&start)?);
                }
                "skipped" => {
                    if let Some(case) = &mut current {
                        case.skipped = true;
                    }
                }
                "failure" | "error" => {
                    if let Some(case) = &mut current {
                        case.failed = true;
                    }
                }
                _ => {}
            },
            Event::Empty(start) => match start.name().as_ref() {
                "testcase" => cases.push(junit_case(&start)?),
                "skipped" => {
                    if let Some(case) = &mut current {
                        case.skipped = true;
                    }
                }
                "failure" | "error" => {
                    if let Some(case) = &mut current {
                        case.failed = true;
                    }
                }
                _ => {}
            },
            Event::End(end) if end.name().as_ref() == "testcase" => {
                cases.push(
                    current
                        .take()
                        .context("CTest JUnit testcase ended without a start")?,
                );
            }
            Event::Eof => break,
            _ => {}
        }
        buffer.clear();
    }
    ensure!(
        current.is_none(),
        "CTest JUnit report ended inside a testcase"
    );
    let mut seen = BTreeSet::new();
    for case in &cases {
        ensure!(
            !case.name.is_empty(),
            "CTest JUnit report contains an unnamed testcase"
        );
        ensure!(
            seen.insert(&case.name),
            "CTest JUnit report repeats test '{}'",
            case.name
        );
    }
    Ok(cases)
}

fn junit_case(start: &BytesStart<'_>) -> Result<JUnitCase> {
    let mut name = None;
    let mut skipped = false;
    let mut failed = false;
    for attribute in start.attributes().with_checks(true) {
        let attribute = attribute.context("CTest JUnit testcase has an invalid attribute")?;
        let key = attribute.key.as_ref();
        let value = attribute
            .normalized_value(XmlVersion::default())
            .context("CTest JUnit testcase attribute is not valid XML")?;
        match key {
            "name" => name = Some(value.into_owned()),
            "status" => skipped |= matches!(value.as_ref(), "notrun" | "skipped"),
            "result" => failed |= matches!(value.as_ref(), "failed" | "error"),
            _ => {}
        }
    }
    Ok(JUnitCase {
        name: name.context("CTest JUnit testcase has no name attribute")?,
        skipped,
        failed,
    })
}

#[cfg(test)]
mod tests {
    use super::{parse_ctest_listing, parse_junit};
    use anyhow::Result;

    #[test]
    fn listing_parser_keeps_exact_names_and_detects_disabled_cases() -> Result<()> {
        let listing = parse_ctest_listing(
            br#"{"tests":[{"name":"runtime_client.production_package","properties":[]},{"name":"output_session.package.exact","properties":[{"name":"DISABLED","value":true}]}]}"#,
        )?;
        assert_eq!(listing.names[0], "runtime_client.production_package");
        assert!(listing.disabled.contains("output_session.package.exact"));
        Ok(())
    }

    #[test]
    fn junit_parser_records_exact_executed_skipped_and_failed_cases() -> Result<()> {
        let cases = parse_junit(
            br#"<testsuites><testsuite><testcase name="case&amp;one"/><testcase name="case.two"><skipped/></testcase><testcase name="case.three"><failure message="bad"/></testcase></testsuite></testsuites>"#,
        )?;
        assert_eq!(cases[0].name, "case&one");
        assert!(!cases[0].skipped && !cases[0].failed);
        assert!(cases[1].skipped);
        assert!(cases[2].failed);
        Ok(())
    }

    #[test]
    fn junit_parser_rejects_malformed_xml_and_duplicate_cases() {
        assert!(parse_junit(b"<testsuites><testcase name=\"x\"></testsuites>").is_err());
        assert!(
            parse_junit(b"<testsuites><testcase name=\"x\"/><testcase name=\"x\"/></testsuites>")
                .is_err()
        );
    }

    #[test]
    fn listing_parser_rejects_duplicate_json_keys_and_test_names() {
        assert!(parse_ctest_listing(br#"{"tests":[],"tests":[]}"#).is_err());
        assert!(parse_ctest_listing(br#"{"tests":[{"name":"x"},{"name":"x"}]}"#).is_err());
    }
}
