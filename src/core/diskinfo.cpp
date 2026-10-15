#include "diskinfo.h"

#include <cstdio>

namespace ff {

const char *busName(BusType b)
{
    switch (b) {
    case BusType::Usb: return "USB";
    case BusType::Sd: return "SD";
    case BusType::Mmc: return "MMC";
    case BusType::Sata: return "SATA";
    case BusType::Ata: return "ATA";
    case BusType::Scsi: return "SCSI";
    case BusType::Nvme: return "NVMe";
    case BusType::Virtual: return "Virtual";
    case BusType::Other: return "Other";
    default: return "Unknown";
    }
}

const char *styleName(PartStyle s)
{
    switch (s) {
    case PartStyle::Mbr: return "MBR";
    case PartStyle::Gpt: return "GPT";
    case PartStyle::Raw: return "RAW (not initialized)";
    default: return "unknown";
    }
}

std::string humanSize(std::uint64_t bytes)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 5) {
        v /= 1024.0;
        ++u;
    }
    char buf[64];
    if (u == 0)
        std::snprintf(buf, sizeof buf, "%llu B", static_cast<unsigned long long>(bytes));
    else
        std::snprintf(buf, sizeof buf, "%.1f %s", v, units[u]);
    return buf;
}

std::string groupedBytes(std::uint64_t bytes)
{
    std::string s = std::to_string(bytes);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
        s.insert(static_cast<std::size_t>(i), ",");
    return s;
}

std::string DiskInfo::displayName() const
{
    std::string n = vendor;
    if (!n.empty() && !product.empty())
        n += ' ';
    n += product;
    if (n.empty())
        n = "Disk " + std::to_string(number);
    return n;
}

std::string DiskInfo::refuseReason() const
{
    if (isSystemDisk)
        return "This disk contains the Windows installation.";
    if (bus != BusType::Usb && bus != BusType::Sd && bus != BusType::Mmc)
        return std::string("Only USB / SD / MMC devices can be modified (this one is ") + busName(bus) + ").";
    if (size == 0)
        return "The device reports no capacity (no media?).";
    return {};
}

std::string DiskInfo::identity() const
{
    return vendor + "|" + product + "|" + serial + "|" + std::to_string(size);
}

std::vector<std::string> diagnose(const DiskInfo &d)
{
    std::vector<std::string> out;
    if (d.isSystemDisk)
        out.push_back("System disk: shown for information only, it cannot be modified here.");
    if (d.offline)
        out.push_back("Disk is OFFLINE. Use \"Clear read-only / bring online\".");
    if (d.readOnly)
        out.push_back("Disk is marked READ-ONLY (software flag). Use \"Clear read-only / bring online\". "
                      "If that does not help, check the stick's physical write-protect switch.");
    if (!d.layoutKnown) {
        out.push_back("The partition table could not be read. Run the program as administrator.");
    } else if (d.style == PartStyle::Raw) {
        out.push_back("Disk is not initialized (RAW). Windows cannot mount it. Use \"Partition + Format\".");
    } else if (d.partitions.empty()) {
        out.push_back("The disk has NO partitions, so Windows has nothing to mount (drive shows as 0 B / "
                      "unusable). Use \"Partition + Format\".");
    }
    for (const auto &p : d.partitions) {
        if (!p.letter.empty() && p.fileSystem.empty())
            out.push_back("Partition " + std::to_string(p.number) + " (" + p.letter +
                          ") has no recognized file system (RAW). Format it.");
    }
    if (d.layoutKnown && d.style == PartStyle::Gpt && d.bus == BusType::Usb && d.partitions.empty())
        out.push_back("Disk uses GPT but has no partitions.");
    if (d.size > (32ull << 30))
        out.push_back("Windows cannot format FAT32 above 32 GB. Choose exFAT/NTFS, or accept a 32 GB FAT32 "
                      "partition.");
    if (out.empty())
        out.push_back("No problems detected in the partition table. If files are missing or the disk acts up, "
                      "re-create the partition (\"Partition + Format\").");
    return out;
}

const DiskInfo *findSameDisk(const DiskInfo &old, const std::vector<DiskInfo> &now)
{
    const std::string id = old.identity();
    for (const auto &d : now)
        if (d.number == old.number && d.identity() == id)
            return &d;
    const DiskInfo *found = nullptr;
    for (const auto &d : now) {
        if (d.identity() == id) {
            if (found)
                return nullptr;  // ambiguous
            found = &d;
        }
    }
    return found;
}

}  // namespace ff
