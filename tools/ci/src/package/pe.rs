//! Bounded static PE machine and export-table inspection.

use std::fs;
use std::path::Path;

use anyhow::{Context, Result, ensure};

use crate::model::Architecture;

const MAX_PE_BYTES: u64 = 1024 * 1024 * 1024;
const MAX_EXPORTS: usize = 65_536;
const MAX_EXPORT_NAME_BYTES: usize = 4096;

#[derive(Debug)]
pub(super) struct PeInspection {
    pub machine: String,
    pub exports: Vec<String>,
}

#[derive(Clone, Copy)]
struct Section {
    virtual_address: u32,
    virtual_size: u32,
    raw_size: u32,
    raw_offset: u32,
}

pub(super) fn inspect(path: &Path, expected_architecture: Architecture) -> Result<PeInspection> {
    let metadata = fs::symlink_metadata(path)
        .with_context(|| format!("could not inspect plugin PE {}", path.display()))?;
    ensure!(
        metadata.is_file() && !metadata.file_type().is_symlink() && metadata.len() <= MAX_PE_BYTES,
        "plugin PE must be a bounded regular file"
    );
    let bytes =
        fs::read(path).with_context(|| format!("could not read plugin PE {}", path.display()))?;
    ensure!(
        bytes.len() >= 64 && bytes.get(..2) == Some(b"MZ"),
        "plugin payload is not a DOS/PE image"
    );
    let pe_offset = usize::try_from(read_u32(&bytes, 0x3c)?)?;
    ensure!(
        read_slice(&bytes, pe_offset, 4)? == b"PE\0\0",
        "plugin payload has no PE signature"
    );
    let coff = pe_offset
        .checked_add(4)
        .context("PE header offset overflow")?;
    let machine_id = read_u16(&bytes, coff)?;
    let section_count = usize::from(read_u16(&bytes, coff + 2)?);
    let optional_size = usize::from(read_u16(&bytes, coff + 16)?);
    ensure!(
        section_count > 0 && section_count <= 96,
        "plugin PE has an invalid section count"
    );
    let optional = coff
        .checked_add(20)
        .context("PE optional header offset overflow")?;
    let optional_bytes = read_slice(&bytes, optional, optional_size)?;
    let magic = read_u16(&bytes, optional)?;
    let (directories_offset, directory_count_offset, expected_machine, machine) =
        match (magic, expected_architecture) {
            (0x10b, Architecture::X86) => (96_usize, 92_usize, 0x014c_u16, "x86"),
            (0x20b, Architecture::X64) => (112_usize, 108_usize, 0x8664_u16, "x64"),
            (0x10b, Architecture::X64) => anyhow::bail!("x86 PE is not valid for an x64 package"),
            (0x20b, Architecture::X86) => anyhow::bail!("x64 PE is not valid for an x86 package"),
            _ => anyhow::bail!("plugin PE has an unsupported optional-header format"),
        };
    ensure!(
        machine_id == expected_machine,
        "plugin PE machine does not match the package architecture"
    );
    ensure!(
        optional_bytes.len() >= directories_offset + 8,
        "plugin PE optional header is too short for its export directory"
    );
    ensure!(
        read_u32(&bytes, optional + directory_count_offset)? > 0,
        "plugin PE does not declare a data directory"
    );
    let export_rva = read_u32(&bytes, optional + directories_offset)?;
    let export_size = read_u32(&bytes, optional + directories_offset + 4)?;
    ensure!(
        export_rva != 0 && export_size >= 40,
        "plugin PE has no valid export directory"
    );
    let sections = read_sections(&bytes, optional, optional_size, section_count)?;
    let exports = read_exports(&bytes, export_rva, &sections)?;
    Ok(PeInspection {
        machine: machine.to_owned(),
        exports,
    })
}

fn read_sections(
    bytes: &[u8],
    optional: usize,
    optional_size: usize,
    section_count: usize,
) -> Result<Vec<Section>> {
    let section_table = optional
        .checked_add(optional_size)
        .context("PE section table offset overflow")?;
    let mut sections = Vec::with_capacity(section_count);
    for index in 0..section_count {
        let offset = section_table
            .checked_add(
                index
                    .checked_mul(40)
                    .context("PE section offset overflow")?,
            )
            .context("PE section offset overflow")?;
        read_slice(bytes, offset, 40)?;
        sections.push(Section {
            virtual_size: read_u32(bytes, offset + 8)?,
            virtual_address: read_u32(bytes, offset + 12)?,
            raw_size: read_u32(bytes, offset + 16)?,
            raw_offset: read_u32(bytes, offset + 20)?,
        });
    }
    Ok(sections)
}

fn read_exports(bytes: &[u8], export_rva: u32, sections: &[Section]) -> Result<Vec<String>> {
    let export_offset = map_rva(bytes, export_rva, 40, sections)?;
    let name_count = usize::try_from(read_u32(bytes, export_offset + 24)?)?;
    ensure!(
        name_count > 0 && name_count <= MAX_EXPORTS,
        "plugin PE export count is invalid"
    );
    let names_rva = read_u32(bytes, export_offset + 32)?;
    let names_offset = map_rva(
        bytes,
        names_rva,
        name_count
            .checked_mul(4)
            .context("PE export names size overflow")?,
        sections,
    )?;
    let mut exports = Vec::with_capacity(name_count);
    for index in 0..name_count {
        let export_name_rva = read_u32(bytes, names_offset + index * 4)?;
        let name_offset = map_rva(bytes, export_name_rva, 1, sections)?;
        let export_name = read_c_string(bytes, name_offset)?;
        ensure!(
            !export_name.is_empty(),
            "plugin PE contains an empty exported name"
        );
        exports.push(export_name);
    }
    exports.sort();
    ensure!(
        exports.windows(2).all(|pair| pair[0] != pair[1]),
        "plugin PE contains duplicate exported names"
    );
    Ok(exports)
}

fn map_rva(bytes: &[u8], rva: u32, size: usize, sections: &[Section]) -> Result<usize> {
    for section in sections {
        let start = u64::from(section.virtual_address);
        let end = start + u64::from(section.virtual_size.max(section.raw_size));
        let requested_start = u64::from(rva);
        let requested_end = requested_start
            .checked_add(u64::try_from(size)?)
            .context("PE RVA range overflow")?;
        if requested_start >= start && requested_end <= end {
            let section_offset = requested_start - start;
            ensure!(
                section_offset + u64::try_from(size)? <= u64::from(section.raw_size),
                "PE RVA points into an uninitialized section range"
            );
            let raw = u64::from(section.raw_offset)
                .checked_add(section_offset)
                .context("PE raw offset overflow")?;
            let offset = usize::try_from(raw)?;
            read_slice(bytes, offset, size)?;
            return Ok(offset);
        }
    }
    anyhow::bail!("plugin PE export address is outside its sections")
}

fn read_c_string(bytes: &[u8], offset: usize) -> Result<String> {
    let limit = offset
        .checked_add(MAX_EXPORT_NAME_BYTES)
        .context("PE export name range overflow")?
        .min(bytes.len());
    let remaining = bytes
        .get(offset..limit)
        .context("PE export name offset is out of range")?;
    let end = remaining
        .iter()
        .position(|byte| *byte == 0)
        .context("PE export name is not terminated")?;
    let value = std::str::from_utf8(&remaining[..end]).context("PE export name is not UTF-8")?;
    Ok(value.to_owned())
}

fn read_u16(bytes: &[u8], offset: usize) -> Result<u16> {
    let value = read_slice(bytes, offset, 2)?;
    Ok(u16::from_le_bytes([value[0], value[1]]))
}

fn read_u32(bytes: &[u8], offset: usize) -> Result<u32> {
    let value = read_slice(bytes, offset, 4)?;
    Ok(u32::from_le_bytes([value[0], value[1], value[2], value[3]]))
}

fn read_slice(bytes: &[u8], offset: usize, length: usize) -> Result<&[u8]> {
    let end = offset.checked_add(length).context("PE offset overflow")?;
    bytes
        .get(offset..end)
        .context("PE field extends beyond the file")
}

#[cfg(test)]
mod tests {
    use super::inspect;
    use crate::model::Architecture;
    use anyhow::Result;
    use std::fs;

    fn fixture(machine: u16, magic: u16, export: &str) -> Vec<u8> {
        let mut bytes = vec![0_u8; 0x500];
        bytes[0..2].copy_from_slice(b"MZ");
        bytes[0x3c..0x40].copy_from_slice(&0x80_u32.to_le_bytes());
        bytes[0x80..0x84].copy_from_slice(b"PE\0\0");
        bytes[0x84..0x86].copy_from_slice(&machine.to_le_bytes());
        bytes[0x86..0x88].copy_from_slice(&1_u16.to_le_bytes());
        bytes[0x94..0x96].copy_from_slice(&0xf0_u16.to_le_bytes());
        let optional = 0x98;
        bytes[optional..optional + 2].copy_from_slice(&magic.to_le_bytes());
        let directory = if magic == 0x10b { 96 } else { 112 };
        let directory_count = if magic == 0x10b { 92 } else { 108 };
        bytes[optional + directory_count..optional + directory_count + 4]
            .copy_from_slice(&16_u32.to_le_bytes());
        bytes[optional + 60..optional + 64].copy_from_slice(&0x200_u32.to_le_bytes());
        bytes[optional + directory..optional + directory + 4]
            .copy_from_slice(&0x1000_u32.to_le_bytes());
        bytes[optional + directory + 4..optional + directory + 8]
            .copy_from_slice(&0x100_u32.to_le_bytes());
        let section = optional + 0xf0;
        bytes[section + 8..section + 12].copy_from_slice(&0x200_u32.to_le_bytes());
        bytes[section + 12..section + 16].copy_from_slice(&0x1000_u32.to_le_bytes());
        bytes[section + 16..section + 20].copy_from_slice(&0x200_u32.to_le_bytes());
        bytes[section + 20..section + 24].copy_from_slice(&0x200_u32.to_le_bytes());
        let export_directory = 0x200;
        bytes[export_directory + 24..export_directory + 28].copy_from_slice(&1_u32.to_le_bytes());
        bytes[export_directory + 32..export_directory + 36]
            .copy_from_slice(&0x1040_u32.to_le_bytes());
        bytes[0x240..0x244].copy_from_slice(&0x1050_u32.to_le_bytes());
        bytes[0x250..0x250 + export.len()].copy_from_slice(export.as_bytes());
        bytes[0x250 + export.len()] = 0;
        bytes
    }

    #[test]
    fn reads_plugin_machine_and_exact_named_exports() -> Result<()> {
        let temporary = tempfile::tempdir()?;
        let path = temporary.path().join("plugin.dll");
        fs::write(&path, fixture(0x8664, 0x20b, "SKSEPlugin_Load"))?;
        let inspection = inspect(&path, Architecture::X64)?;
        assert_eq!(inspection.machine, "x64");
        assert_eq!(inspection.exports, ["SKSEPlugin_Load"]);
        Ok(())
    }

    #[test]
    fn rejects_wrong_architecture_and_truncated_rvas() -> Result<()> {
        let temporary = tempfile::tempdir()?;
        let path = temporary.path().join("plugin.dll");
        fs::write(&path, fixture(0x014c, 0x10b, "SKSEPlugin_Query"))?;
        assert!(inspect(&path, Architecture::X64).is_err());
        let mut malformed = fixture(0x8664, 0x20b, "SKSEPlugin_Load");
        malformed.truncate(0x245);
        fs::write(&path, malformed)?;
        assert!(inspect(&path, Architecture::X64).is_err());
        Ok(())
    }
}
