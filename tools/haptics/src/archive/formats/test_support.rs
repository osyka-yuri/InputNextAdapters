use super::{ArchiveEntry, PayloadCodec};
use crate::archive::ResourceInfo;

pub(in crate::archive) fn raw_entry(info: ResourceInfo) -> ArchiveEntry {
    ArchiveEntry {
        info,
        offset: 0,
        stored_size: 1,
        codec: PayloadCodec::Raw,
    }
}
