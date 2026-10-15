// Fake devices for non-Windows builds (GUI development, screenshots, tests).
// They mirror the situation in the README: an internal system disk, a stick with
// no partitions at all, and a stick that is read-only.
#include "storage.h"

namespace ff {

bool isElevated()
{
    return true;
}

bool isDemoBackend()
{
    return true;
}

std::vector<DiskInfo> enumerateDisks()
{
    constexpr std::uint64_t MiB = 1024ull * 1024ull;
    std::vector<DiskInfo> v;

    DiskInfo sys;
    sys.number = 0;
    sys.vendor = "KXG5AZNV512G";
    sys.product = "NVMe SED TOSHIBA 512GB";
    sys.size = 512110190592ull;
    sys.bus = BusType::Nvme;
    sys.style = PartStyle::Gpt;
    sys.layoutKnown = true;
    sys.isSystemDisk = true;
    sys.letters = "C:";
    sys.partitions.push_back({1, 1 * MiB, 100 * MiB, "EFI system", "", "", "", false, 0});
    PartitionInfo c{2, 101 * MiB, 475000 * MiB, "Basic data", "NTFS", "Windows", "C:", true, 120ull << 30};
    sys.partitions.push_back(c);
    v.push_back(sys);

    DiskInfo kingston;
    kingston.number = 2;
    kingston.vendor = "Kingston";
    kingston.product = "DataTraveler 3.0";
    kingston.revision = "PMAP";
    kingston.serial = "0014780E6AB0F1C0B0000028";
    kingston.size = 61920509952ull;
    kingston.bus = BusType::Usb;
    kingston.removableFlag = true;
    kingston.style = PartStyle::Mbr;
    kingston.layoutKnown = true;  // MBR, but no partitions
    kingston.letters = "F:";
    v.push_back(kingston);

    DiskInfo generic;
    generic.number = 3;
    generic.product = "Mass Storage Device";
    generic.serial = "070B1A2C3D4E5F60";
    generic.size = 67109912576ull;
    generic.bus = BusType::Usb;
    generic.removableFlag = true;
    generic.style = PartStyle::Mbr;
    generic.layoutKnown = true;
    generic.readOnly = true;
    generic.letters = "G:";
    generic.partitions.push_back({1, 1 * MiB, 8000 * MiB, "FAT32 (0x0C)", "FAT32", "GDSFW", "G:", true, 7ull << 30});
    v.push_back(generic);

    return v;
}

}  // namespace ff
