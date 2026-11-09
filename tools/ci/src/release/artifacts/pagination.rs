//! Bounded pagination for GitHub Actions list endpoints used by release selection.

use std::collections::BTreeSet;

use anyhow::{Context, Result, ensure};
use serde_json::Value;

const PAGE_SIZE: usize = 100;
// Bound service requests and the retained inventory independently of run retries.
const MAX_ITEMS: usize = 10_000;
const MAX_PAGE_BYTES: usize = 16 * 1024 * 1024;
const MAX_TOTAL_BYTES: usize = 64 * 1024 * 1024;

pub(super) fn fetch_all_pages(
    endpoint: &str,
    collection_key: &str,
    mut fetch: impl FnMut(&str) -> Result<Value>,
) -> Result<Value> {
    ensure!(
        endpoint.ends_with("?per_page=100"),
        "paginated GitHub endpoint must request 100 entries per page"
    );

    let first_page = fetch(endpoint).context("could not read first GitHub API page")?;
    let total_count = page_total(&first_page)?;
    let total = usize::try_from(total_count).context("GitHub list total exceeds this platform")?;
    ensure!(
        total <= MAX_ITEMS,
        "GitHub list exceeds the {MAX_ITEMS}-item pagination limit"
    );

    let mut aggregate_bytes = page_size_bytes(&first_page)?;
    let mut items = Vec::with_capacity(total);
    let mut ids = BTreeSet::new();
    append_page_items(&first_page, collection_key, total, &mut items, &mut ids)?;

    let mut page_number = 2_usize;
    while items.len() < total {
        // Page numbers are constructed locally. The API response is never allowed
        // to direct requests to arbitrary URLs or repositories.
        let page_endpoint = format!("{endpoint}&page={page_number}");
        let page = fetch(&page_endpoint)
            .with_context(|| format!("could not read GitHub API page {page_number}"))?;
        ensure!(
            page_total(&page)? == total_count,
            "GitHub list total_count changed while pages were being read"
        );
        aggregate_bytes = aggregate_bytes
            .checked_add(page_size_bytes(&page)?)
            .context("GitHub list response size overflow")?;
        ensure!(
            aggregate_bytes <= MAX_TOTAL_BYTES,
            "GitHub paginated response exceeds the {MAX_TOTAL_BYTES}-byte aggregate limit"
        );

        append_page_items(&page, collection_key, total, &mut items, &mut ids)?;
        page_number = page_number
            .checked_add(1)
            .context("GitHub page number overflow")?;
    }

    ensure!(
        items.len() == total,
        "GitHub list returned an incomplete inventory"
    );
    let mut complete = serde_json::Map::new();
    complete.insert("total_count".into(), Value::from(total_count));
    complete.insert(collection_key.into(), Value::Array(items));
    Ok(Value::Object(complete))
}

fn page_total(page: &Value) -> Result<u64> {
    page.get("total_count")
        .and_then(Value::as_u64)
        .context("GitHub list response has no valid total_count")
}

fn page_size_bytes(page: &Value) -> Result<usize> {
    let bytes = serde_json::to_vec(page).context("could not size GitHub list response")?;
    ensure!(
        bytes.len() <= MAX_PAGE_BYTES,
        "GitHub list page exceeds the {MAX_PAGE_BYTES}-byte page limit"
    );
    Ok(bytes.len())
}

fn append_page_items(
    page: &Value,
    collection_key: &str,
    total: usize,
    items: &mut Vec<Value>,
    ids: &mut BTreeSet<u64>,
) -> Result<()> {
    let entries = page
        .get(collection_key)
        .and_then(Value::as_array)
        .with_context(|| format!("GitHub list response has no {collection_key} array"))?;
    let remaining = total
        .checked_sub(items.len())
        .context("GitHub list page contains more entries than total_count")?;
    let expected = remaining.min(PAGE_SIZE);
    ensure!(
        entries.len() == expected,
        "GitHub list page is short or oversized: expected {expected} entries, received {}",
        entries.len()
    );

    for entry in entries {
        let id = entry
            .get("id")
            .and_then(Value::as_u64)
            .context("GitHub list item has no valid ID")?;
        ensure!(
            id > 0 && ids.insert(id),
            "GitHub list contains a duplicate or invalid item ID"
        );
    }
    items.extend(entries.iter().cloned());
    Ok(())
}
