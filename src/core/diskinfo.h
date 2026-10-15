// Plain data model for a physical disk and its partitions. No Qt, no Win32 here,
// so everything in src/core can be unit-tested on any platform.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ff {

enum class BusType { Unknown, Usb, Sd, Mmc, Sata, Ata, Scsi, Nvme, Virtual, Other };
enum class PartStyle { Unknown, Raw, Mbr, Gpt };

const char *busName(BusType b);
const char *styleName(PartStyle s);

// "57.7 GiB"
std::string humanSize(std::uint64_t bytes);
// "61,920,509,952"
std::string groupedBytes(std::uint64_t bytes);

struct PartitionInfo {
    int number = 0;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    std::string type;        // "FAT32 (0x0C)", "Basic data", ...
    std::string fileSystem;  // from the mounted volume; empty if not mounted / unreadable
    std::string label;
    std::string letter;      // "F:" or empty
    bool hasSpaceInfo = false;
    std::uint64_t freeBytes = 0;
};

struct DiskInfo {
    int number = -1;  // PhysicalDriveN
    std::string vendor;
    std::string product;
    std::string revision;
    std::string serial;
    std::uint64_t size = 0;  // bytes
    BusType bus = BusType::Unknown;
    bool removableFlag = false;
    PartStyle style = PartStyle::Unknown;
    bool layoutKnown = false;  // false if the partition table could not be read
    bool readOnly = false;
    bool offline = false;
    bool isSystemDisk = false;  // hosts the Windows directory
    std::vector<PartitionInfo> partitions;
    std::string letters;  // every drive letter on this disk, e.g. "F: G:"

    std::string displayName() const;
    // Empty string = safe to operate on. Otherwise the reason it is refused.
    std::string refuseReason() const;
    bool isTarget() const { return refuseReason().empty(); }
    // vendor|product|serial|size, used to make sure the disk is still the same
    // device after Windows renumbers it.
    std::string identity() const;
};

// Human-readable findings explaining why a disk is unusable and what to do.
std::vector<std::string> diagnose(const DiskInfo &d);

// Finds `old` in `now`: same number and identity first, otherwise a unique identity match.
const DiskInfo *findSameDisk(const DiskInfo &old, const std::vector<DiskInfo> &now);

}  // namespace ff
