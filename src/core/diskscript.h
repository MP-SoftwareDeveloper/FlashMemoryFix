// Builds diskpart scripts for the three operations the GUI offers. Pure string
// logic (no Qt / Win32), so it is unit-tested on any platform.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "diskinfo.h"

namespace ff {

// Windows refuses to format FAT32 volumes above 32 GB, so larger FAT32 partitions are capped.
constexpr std::uint64_t kFat32MaxMB = 32000;
constexpr std::uint64_t kFat32MinMB = 64;
constexpr std::uint64_t kOtherMinMB = 16;

enum class Fs { Fat32, ExFat, Ntfs };

const char *fsName(Fs fs);  // "FAT32", "exFAT", "NTFS"

struct FormatOptions {
    Fs fs = Fs::Fat32;
    PartStyle style = PartStyle::Mbr;  // Mbr or Gpt
    std::string label;
    std::uint64_t sizeMB = 0;  // 0 = whole disk (FAT32: capped)
    unsigned unitBytes = 0;    // allocation unit; 0 = default
    bool quick = true;
    // Overwrite every sector with zeros ("clean all"). Slow; for sticks with suspect data.
    // Normal cleaning is automatic: the partition table is wiped whenever the disk has partitions.
    bool zeroAll = false;
};

struct Script {
    bool ok = false;
    std::string error;
    std::vector<std::string> lines;
    std::vector<std::string> steps;  // the same work in plain language, for the confirmation dialog
    std::vector<std::string> notes;  // things the user should know (e.g. FAT32 size cap)
};

std::size_t maxLabelLength(Fs fs);
// Empty = valid, otherwise a message. Allowed: letters, digits, space, '_' and '-'.
std::string checkLabel(Fs fs, const std::string &label);

Script buildClearReadOnlyScript(const DiskInfo &d);
// Everything in one go: clear flags, clean (only when needed), convert, partition, format, assign.
Script buildFormatScript(const DiskInfo &d, const FormatOptions &o);

std::string scriptToText(const Script &s);

}  // namespace ff
