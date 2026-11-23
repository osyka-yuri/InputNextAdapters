//! Bounded Microsoft Program Database inspection for adapter symbol packages.

use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom};
use std::path::Path;

use anyhow::{Context, Result, ensure};

use crate::model::Product;

const MSF7_SIGNATURE: &[u8; 32] = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0";
const MAX_PDB_BYTES: u64 = 2 * 1024 * 1024 * 1024;
const MAX_DIRECTORY_BYTES: usize = 64 * 1024 * 1024;
const MAX_STREAMS: usize = 1_000_000;
const PDB_INFO_STREAM: usize = 1;
const PDB_DBI_STREAM: usize = 3;
const PDB_INFO_HEADER_BYTES: usize = 28;
const PDB_DBI_HEADER_BYTES: usize = 64;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub(crate) struct CodeViewIdentity {
    pub(crate) guid: [u8; 16],
    pub(crate) age: u32,
}

struct MsfStream {
    blocks: Vec<u32>,
    size: usize,
}

struct MsfLayout {
    block_size: usize,
    block_count: u32,
    directory: Vec<u8>,
}

pub(crate) const fn has_paired_symbols(product: Product) -> bool {
    matches!(product, Product::Oblivion | Product::FalloutNewVegas)
}

pub(crate) fn verify_match(
    plugin: Option<CodeViewIdentity>,
    symbols: Option<CodeViewIdentity>,
) -> Result<()> {
    let plugin = plugin.context("plugin PE has no CodeView identity")?;
    let symbols = symbols.context("PDB has no CodeView identity")?;
    ensure!(
        plugin == symbols,
        "symbol PDB GUID and age do not match the packaged plugin"
    );
    Ok(())
}

pub(crate) fn inspect_pdb(path: &Path) -> Result<CodeViewIdentity> {
    let metadata = fs::symlink_metadata(path)
        .with_context(|| format!("could not inspect PDB {}", path.display()))?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink() && metadata.len() <= MAX_PDB_BYTES,
        "PDB must be a bounded regular file"
    );

    let mut file =
        File::open(path).with_context(|| format!("could not open PDB {}", path.display()))?;
    let layout = read_msf_layout(&mut file, metadata.len())?;
    read_pdb_identity(&mut file, &layout)
}

fn read_msf_layout(file: &mut File, file_size: u64) -> Result<MsfLayout> {
    let mut signature = [0_u8; MSF7_SIGNATURE.len()];
    file.read_exact(&mut signature)
        .context("PDB is truncated before its MSF 7 signature")?;
    ensure!(
        signature == *MSF7_SIGNATURE,
        "PDB does not use the MSF 7 format"
    );

    let block_size = read_u32(file)?;
    let _free_block_map = read_u32(file)?;
    let block_count = read_u32(file)?;
    let directory_size = usize::try_from(read_u32(file)?)?;
    let _reserved = read_u32(file)?;
    let block_map_index = read_u32(file)?;

    ensure!(
        (512..=65_536).contains(&block_size) && block_size.is_power_of_two(),
        "PDB has an invalid MSF block size"
    );
    ensure!(block_count > 0, "PDB has no MSF blocks");
    let declared_file_size = u64::from(block_count)
        .checked_mul(u64::from(block_size))
        .context("PDB MSF file size overflows")?;
    ensure!(
        declared_file_size <= file_size && declared_file_size <= MAX_PDB_BYTES,
        "PDB MSF block map extends beyond the file"
    );
    ensure!(
        directory_size > 0 && directory_size <= MAX_DIRECTORY_BYTES,
        "PDB MSF stream directory has an invalid size"
    );

    let block_size = usize::try_from(block_size)?;
    let directory_block_count = directory_size.div_ceil(block_size);
    ensure!(
        directory_block_count
            .checked_mul(4)
            .is_some_and(|size| size <= block_size),
        "PDB MSF directory block map exceeds its block"
    );
    let directory_map_offset = block_offset(block_map_index, block_size, block_count)?;
    let mut directory_block_map = vec![0_u8; directory_block_count * 4];
    read_at(file, directory_map_offset, &mut directory_block_map)?;
    let directory_blocks = read_block_indices(&directory_block_map, block_count)?;
    let directory = read_stream_prefix(
        file,
        &MsfStream {
            blocks: directory_blocks,
            size: directory_size,
        },
        block_size,
        block_count,
        directory_size,
    )?;
    Ok(MsfLayout {
        block_size,
        block_count,
        directory,
    })
}

fn read_pdb_identity(file: &mut File, layout: &MsfLayout) -> Result<CodeViewIdentity> {
    let (info_stream, dbi_stream) =
        read_required_stream_blocks(&layout.directory, layout.block_count, layout.block_size)?;
    ensure!(
        info_stream.size >= PDB_INFO_HEADER_BYTES,
        "PDB Info stream is too short"
    );
    let info = read_stream_prefix(
        file,
        &info_stream,
        layout.block_size,
        layout.block_count,
        PDB_INFO_HEADER_BYTES,
    )?;

    let version = read_u32_slice(&info, 0)?;
    ensure!(
        matches!(version, 19_990_903 | 20_000_404 | 20_091_201 | 20_140_508),
        "PDB Info stream has an unsupported version"
    );
    let age = read_u32_slice(&info, 8)?;
    ensure!(age > 0, "PDB Info stream has an invalid age");
    let guid: [u8; 16] = info[12..28]
        .try_into()
        .context("PDB Info stream GUID is truncated")?;
    ensure!(
        guid.iter().any(|byte| *byte != 0),
        "PDB Info stream GUID is empty"
    );
    ensure!(
        dbi_stream.size >= PDB_DBI_HEADER_BYTES,
        "PDB DBI stream is too short"
    );
    let dbi = read_stream_prefix(
        file,
        &dbi_stream,
        layout.block_size,
        layout.block_count,
        PDB_DBI_HEADER_BYTES,
    )?;
    let dbi_signature = read_u32_slice(&dbi, 0)?;
    let dbi_version = read_u32_slice(&dbi, 4)?;
    let dbi_age = read_u32_slice(&dbi, 8)?;
    ensure!(
        dbi_signature == u32::MAX && dbi_version == 19_990_903 && dbi_age == age,
        "PDB DBI stream does not match its Info stream identity"
    );
    Ok(CodeViewIdentity { guid, age })
}

fn read_required_stream_blocks(
    directory: &[u8],
    block_count: u32,
    block_size: usize,
) -> Result<(MsfStream, MsfStream)> {
    let stream_count = usize::try_from(read_u32_slice(directory, 0)?)?;
    ensure!(
        (PDB_DBI_STREAM + 1..=MAX_STREAMS).contains(&stream_count),
        "PDB stream count is invalid"
    );
    let sizes_end = 4_usize
        .checked_add(
            stream_count
                .checked_mul(4)
                .context("PDB stream table size overflows")?,
        )
        .context("PDB stream table size overflows")?;
    ensure!(
        sizes_end <= directory.len(),
        "PDB stream size table is truncated"
    );
    let info_size = usize::try_from(read_u32_slice(directory, 4 + PDB_INFO_STREAM * 4)?)?;
    ensure!(
        info_size != u32::MAX as usize && info_size > 0,
        "PDB Info stream is missing"
    );
    let dbi_size = usize::try_from(read_u32_slice(directory, 4 + PDB_DBI_STREAM * 4)?)?;
    ensure!(
        dbi_size != u32::MAX as usize && dbi_size > 0,
        "PDB DBI stream is missing"
    );

    let mut cursor = sizes_end;
    let mut info_blocks = Vec::new();
    let mut dbi_blocks = Vec::new();
    for index in 0..stream_count {
        let size = usize::try_from(read_u32_slice(directory, 4 + index * 4)?)?;
        if size == u32::MAX as usize {
            continue;
        }
        let count = size.div_ceil(block_size);
        let bytes = count
            .checked_mul(4)
            .context("PDB stream block list overflows")?;
        let end = cursor
            .checked_add(bytes)
            .context("PDB stream directory offset overflows")?;
        ensure!(end <= directory.len(), "PDB stream block list is truncated");
        if index == PDB_INFO_STREAM {
            info_blocks = read_block_indices(&directory[cursor..end], block_count)?;
        } else if index == PDB_DBI_STREAM {
            dbi_blocks = read_block_indices(&directory[cursor..end], block_count)?;
        } else {
            validate_block_indices(&directory[cursor..end], block_count)?;
        }
        cursor = end;
    }
    ensure!(
        cursor == directory.len(),
        "PDB stream directory contains trailing data"
    );
    ensure!(
        !info_blocks.is_empty() && !dbi_blocks.is_empty(),
        "PDB required stream has no data blocks"
    );
    Ok((
        MsfStream {
            blocks: info_blocks,
            size: info_size,
        },
        MsfStream {
            blocks: dbi_blocks,
            size: dbi_size,
        },
    ))
}

fn read_block_indices(bytes: &[u8], block_count: u32) -> Result<Vec<u32>> {
    ensure!(
        bytes.len().is_multiple_of(4),
        "PDB block index list is malformed"
    );
    let mut blocks = Vec::with_capacity(bytes.len() / 4);
    for index in 0..(bytes.len() / 4) {
        let block = read_u32_slice(bytes, index * 4)?;
        ensure!(
            block < block_count,
            "PDB block index is outside the MSF file"
        );
        blocks.push(block);
    }
    Ok(blocks)
}

fn validate_block_indices(bytes: &[u8], block_count: u32) -> Result<()> {
    let (chunks, remainder) = bytes.as_chunks::<4>();
    ensure!(remainder.is_empty(), "PDB block index list is malformed");
    for chunk in chunks {
        let block = read_u32_slice(chunk, 0)?;
        ensure!(
            block < block_count,
            "PDB block index is outside the MSF file"
        );
    }
    Ok(())
}

fn read_stream_prefix(
    file: &mut File,
    stream: &MsfStream,
    block_size: usize,
    block_count: u32,
    prefix_size: usize,
) -> Result<Vec<u8>> {
    ensure!(
        prefix_size <= stream.size,
        "PDB stream prefix exceeds its declared size"
    );
    ensure!(
        stream.blocks.len() == stream.size.div_ceil(block_size),
        "PDB stream block count does not match its declared size"
    );
    let mut output = Vec::with_capacity(prefix_size);
    for &block in &stream.blocks {
        if output.len() == prefix_size {
            break;
        }
        let remaining = prefix_size - output.len();
        let amount = remaining.min(block_size);
        let offset = block_offset(block, block_size, block_count)?;
        let start = output.len();
        output.resize(start + amount, 0);
        read_at(file, offset, &mut output[start..])?;
    }
    ensure!(output.len() == prefix_size, "PDB stream data is truncated");
    Ok(output)
}

fn block_offset(index: u32, block_size: usize, block_count: u32) -> Result<u64> {
    ensure!(
        index < block_count,
        "PDB block index is outside the MSF file"
    );
    u64::from(index)
        .checked_mul(u64::try_from(block_size)?)
        .context("PDB block offset overflows")
}

fn read_at(file: &mut File, offset: u64, output: &mut [u8]) -> Result<()> {
    file.seek(SeekFrom::Start(offset))
        .context("could not seek to PDB stream data")?;
    file.read_exact(output)
        .context("PDB stream data is truncated")
}

fn read_u32(file: &mut File) -> Result<u32> {
    let mut bytes = [0_u8; 4];
    file.read_exact(&mut bytes)
        .context("PDB MSF header is truncated")?;
    Ok(u32::from_le_bytes(bytes))
}

fn read_u32_slice(bytes: &[u8], offset: usize) -> Result<u32> {
    let end = offset.checked_add(4).context("PDB offset overflows")?;
    let value = bytes
        .get(offset..end)
        .context("PDB field extends beyond its stream")?;
    Ok(u32::from_le_bytes(value.try_into()?))
}

#[cfg(test)]
mod tests {
    use super::{CodeViewIdentity, inspect_pdb};
    use anyhow::Result;
    use std::fs;

    fn pdb_fixture(guid: [u8; 16], age: u32) -> Vec<u8> {
        let mut bytes = vec![0_u8; 7 * 512];
        bytes[..32].copy_from_slice(super::MSF7_SIGNATURE);
        bytes[32..36].copy_from_slice(&512_u32.to_le_bytes());
        bytes[36..40].copy_from_slice(&1_u32.to_le_bytes());
        bytes[40..44].copy_from_slice(&7_u32.to_le_bytes());
        bytes[44..48].copy_from_slice(&28_u32.to_le_bytes());
        bytes[52..56].copy_from_slice(&2_u32.to_le_bytes());
        bytes[2 * 512..2 * 512 + 4].copy_from_slice(&4_u32.to_le_bytes());
        bytes[4 * 512..4 * 512 + 4].copy_from_slice(&4_u32.to_le_bytes());
        bytes[4 * 512 + 4..4 * 512 + 8].copy_from_slice(&u32::MAX.to_le_bytes());
        bytes[4 * 512 + 8..4 * 512 + 12].copy_from_slice(&28_u32.to_le_bytes());
        bytes[4 * 512 + 12..4 * 512 + 16].copy_from_slice(&u32::MAX.to_le_bytes());
        bytes[4 * 512 + 16..4 * 512 + 20].copy_from_slice(&64_u32.to_le_bytes());
        bytes[4 * 512 + 20..4 * 512 + 24].copy_from_slice(&5_u32.to_le_bytes());
        bytes[4 * 512 + 24..4 * 512 + 28].copy_from_slice(&6_u32.to_le_bytes());
        let info = 5 * 512;
        bytes[info..info + 4].copy_from_slice(&20_140_508_u32.to_le_bytes());
        bytes[info + 8..info + 12].copy_from_slice(&age.to_le_bytes());
        bytes[info + 12..info + 28].copy_from_slice(&guid);
        let dbi = 6 * 512;
        bytes[dbi..dbi + 4].copy_from_slice(&u32::MAX.to_le_bytes());
        bytes[dbi + 4..dbi + 8].copy_from_slice(&19_990_903_u32.to_le_bytes());
        bytes[dbi + 8..dbi + 12].copy_from_slice(&age.to_le_bytes());
        bytes
    }

    #[test]
    fn reads_the_guid_and_age_from_a_pdb_info_stream() -> Result<()> {
        let temporary = tempfile::tempdir()?;
        let path = temporary.path().join("plugin.pdb");
        let guid = [0x45_u8; 16];
        fs::write(&path, pdb_fixture(guid, 7))?;

        assert_eq!(inspect_pdb(&path)?, CodeViewIdentity { guid, age: 7 });
        Ok(())
    }

    #[test]
    fn rejects_malformed_or_truncated_msf_streams() -> Result<()> {
        let temporary = tempfile::tempdir()?;
        let path = temporary.path().join("plugin.pdb");
        fs::write(&path, b"wrong PDB")?;
        assert!(inspect_pdb(&path).is_err());

        let mut truncated = pdb_fixture([0x45; 16], 7);
        truncated.truncate(4 * 512 + 27);
        fs::write(&path, truncated)?;
        assert!(inspect_pdb(&path).is_err());
        Ok(())
    }
}
