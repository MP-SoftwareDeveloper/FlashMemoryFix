// Plain-assert unit tests for src/core (no Qt needed). Run via `ctest`.
#include <cstdio>
#include <cstdlib>

#include "diskinfo.h"
#include "diskscript.h"

using namespace ff;

static int g_failed = 0;
#define CHECK(cond)                                                                      \
    do {                                                                                 \
        if (!(cond)) {                                                                   \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                  \
            ++g_failed;                                                                  \
        }                                                                                \
    } while (0)

static DiskInfo stick(std::uint64_t bytes)
{
    DiskInfo d;
    d.number = 2;
    d.vendor = "Kingston";
    d.product = "DataTraveler 3.0";
    d.serial = "ABC";
    d.size = bytes;
    d.bus = BusType::Usb;
    d.style = PartStyle::Mbr;
    d.layoutKnown = true;  // MBR, no partitions: the state described in the README
    return d;
}

static bool has(const Script &s, const std::string &line)
{
    for (const auto &l : s.lines)
        if (l == line)
            return true;
    return false;
}

static void testEmptyDiskSkipsClean()
{
    FormatOptions o;
    o.fs = Fs::Fat32;
    o.label = "GDSFW";
    o.sizeMB = 8000;
    const Script s = buildFormatScript(stick(61920509952ull), o);
    CHECK(s.ok);
    // Exactly the script that fixed the Kingston stick (no "clean").
    const std::vector<std::string> expect = {"select disk 2", "create partition primary size=8000",
                                             "format fs=fat32 quick label=\"GDSFW\"", "assign"};
    CHECK(s.lines == expect);
}

static void testReadOnlyAndOffline()
{
    DiskInfo d = stick(67109912576ull);
    d.readOnly = true;
    d.offline = true;
    FormatOptions o;
    o.sizeMB = 8000;
    const Script s = buildFormatScript(d, o);
    CHECK(s.ok);
    CHECK(s.lines[1] == "attributes disk clear readonly");
    CHECK(s.lines[2] == "online disk noerr");
}

static void testExistingPartitionsForceClean()
{
    DiskInfo d = stick(61920509952ull);
    d.partitions.push_back({1, 1048576, 1000ull << 20, "FAT32 (0x0C)", "FAT32", "X", "F:", true, 0});
    FormatOptions o;
    o.fs = Fs::ExFat;
    const Script s = buildFormatScript(d, o);
    CHECK(s.ok);
    CHECK(has(s, "clean"));
    CHECK(!s.steps.empty());
    CHECK(has(s, "create partition primary"));  // whole disk
    CHECK(has(s, "format fs=exfat quick"));
}

static void testUnknownLayoutForcesClean()
{
    DiskInfo d = stick(61920509952ull);
    d.layoutKnown = false;
    const Script s = buildFormatScript(d, FormatOptions{});
    CHECK(s.ok);
    CHECK(has(s, "clean"));
}

static void testFat32Cap()
{
    FormatOptions o;
    o.fs = Fs::Fat32;
    Script s = buildFormatScript(stick(61920509952ull), o);  // whole 57.7 GiB disk requested
    CHECK(s.ok);
    CHECK(has(s, "create partition primary size=32000"));
    CHECK(!s.notes.empty());

    o.sizeMB = 40000;  // explicit but too large
    s = buildFormatScript(stick(61920509952ull), o);
    CHECK(has(s, "create partition primary size=32000"));

    // Small stick: no cap, no size= (use everything), no note.
    s = buildFormatScript(stick(8ull << 30), o = FormatOptions{});
    CHECK(s.ok);
    CHECK(has(s, "create partition primary"));
    CHECK(s.notes.empty());
}

static void testGptConversion()
{
    FormatOptions o;
    o.style = PartStyle::Gpt;
    Script s = buildFormatScript(stick(61920509952ull), o);  // MBR, empty -> convert
    CHECK(has(s, "convert gpt"));

    DiskInfo d = stick(61920509952ull);
    d.style = PartStyle::Gpt;
    o.style = PartStyle::Mbr;
    s = buildFormatScript(d, o);  // GPT, empty -> back to MBR
    CHECK(has(s, "convert mbr"));

    d = stick(61920509952ull);
    d.style = PartStyle::Gpt;
    o.style = PartStyle::Gpt;
    s = buildFormatScript(d, o);  // already GPT: nothing to convert
    CHECK(!has(s, "convert gpt") && !has(s, "convert mbr"));

    d = stick(61920509952ull);
    d.partitions.push_back({1, 1048576, 1000ull << 20, "FAT32", "FAT32", "", "F:", false, 0});
    o.style = PartStyle::Mbr;
    s = buildFormatScript(d, o);  // clean -> uninitialized -> MBR needs no convert
    CHECK(has(s, "clean") && !has(s, "convert mbr") && !has(s, "convert gpt"));
}

static void testValidation()
{
    FormatOptions o;
    o.label = "THIS LABEL IS TOO LONG";
    CHECK(!buildFormatScript(stick(61920509952ull), o).ok);
    o.fs = Fs::Ntfs;
    CHECK(buildFormatScript(stick(61920509952ull), o).ok);  // NTFS allows 32

    o = FormatOptions{};
    o.label = "a\"b";
    CHECK(!buildFormatScript(stick(61920509952ull), o).ok);  // no quote injection into diskpart
    o.label = "A\nclean";
    CHECK(!buildFormatScript(stick(61920509952ull), o).ok);  // no newline injection

    o = FormatOptions{};
    o.sizeMB = 100000;  // bigger than the disk
    CHECK(!buildFormatScript(stick(8ull << 30), o).ok);
    o.sizeMB = 10;  // below FAT32 minimum
    CHECK(!buildFormatScript(stick(8ull << 30), o).ok);
    o = FormatOptions{};
    o.unitBytes = 1234;
    CHECK(!buildFormatScript(stick(8ull << 30), o).ok);
    o = FormatOptions{};
    o.style = PartStyle::Raw;
    CHECK(!buildFormatScript(stick(8ull << 30), o).ok);

    DiskInfo none;  // number = -1
    CHECK(!buildFormatScript(none, FormatOptions{}).ok);
    CHECK(!buildClearReadOnlyScript(none).ok);
}

static void testReadOnlyScript()
{
    const Script s = buildClearReadOnlyScript(stick(61920509952ull));
    CHECK(s.ok);
    CHECK(s.lines == std::vector<std::string>({"select disk 2", "attributes disk clear readonly", "online disk noerr"}));
}

// One action does everything; "clean" is decided automatically.
static void testAutomaticClean()
{
    // Empty disk: no clean (the Kingston refused it), straight to partition + format.
    Script s = buildFormatScript(stick(61920509952ull), FormatOptions{});
    CHECK(s.ok && !has(s, "clean") && !has(s, "clean all"));

    // Disk with a partition: cleaned automatically, and the plan says so.
    DiskInfo d = stick(61920509952ull);
    d.partitions.push_back({1, 1048576, 8000ull << 20, "FAT32 (0x0C)", "FAT32", "GDSFW", "F:", true, 0});
    s = buildFormatScript(d, FormatOptions{});
    CHECK(s.ok && has(s, "clean") && !has(s, "clean all"));
    bool erases = false;
    for (const auto &step : s.steps)
        erases = erases || step.find("Erase the existing partitions") != std::string::npos;
    CHECK(erases);
    // clean comes before create and format
    std::size_t iClean = 99, iCreate = 99, iFormat = 99;
    for (std::size_t i = 0; i < s.lines.size(); ++i) {
        if (s.lines[i] == "clean") iClean = i;
        if (s.lines[i].rfind("create partition", 0) == 0) iCreate = i;
        if (s.lines[i].rfind("format ", 0) == 0) iFormat = i;
    }
    CHECK(iClean < iCreate && iCreate < iFormat);

    // Zero-all replaces plain clean, even on an empty disk, with a slow-operation note.
    FormatOptions o;
    o.zeroAll = true;
    s = buildFormatScript(stick(61920509952ull), o);
    CHECK(has(s, "clean all") && !has(s, "clean") && !s.notes.empty());
}

static void testStepsMatchLines()
{
    DiskInfo d = stick(67109912576ull);
    d.readOnly = true;
    d.partitions.push_back({1, 1048576, 8000ull << 20, "FAT32", "FAT32", "", "G:", false, 0});
    FormatOptions o;
    o.fs = Fs::ExFat;
    o.label = "DATA";
    o.style = PartStyle::Gpt;
    const Script s = buildFormatScript(d, o);
    CHECK(s.ok);
    CHECK(!s.steps.empty());
    CHECK(s.steps.front() == "Clear the read-only flag");
    CHECK(s.steps.back() == "Assign a drive letter");
    CHECK(has(s, "convert gpt"));
    // the whole sequence, in order
    const std::vector<std::string> expect = {"select disk 2",
                                             "attributes disk clear readonly",
                                             "clean",
                                             "convert gpt",
                                             "create partition primary",
                                             "format fs=exfat quick label=\"DATA\"",
                                             "assign"};
    CHECK(s.lines == expect);
}

static void testSafety()
{
    DiskInfo d = stick(61920509952ull);
    CHECK(d.isTarget());
    d.isSystemDisk = true;
    CHECK(!d.isTarget());
    d = stick(61920509952ull);
    d.bus = BusType::Nvme;
    CHECK(!d.isTarget());
    d = stick(0);
    CHECK(!d.isTarget());
    d = stick(1ull << 30);
    d.bus = BusType::Sd;
    CHECK(d.isTarget());
}

static void testFindSameDisk()
{
    DiskInfo a = stick(61920509952ull);
    DiskInfo b = a;
    b.number = 3;  // Windows renumbered the stick
    std::vector<DiskInfo> now = {b};
    const DiskInfo *p = findSameDisk(a, now);
    CHECK(p && p->number == 3);

    DiskInfo other = stick(67109912576ull);  // different size: different device
    now = {other};
    CHECK(findSameDisk(a, now) == nullptr);

    DiskInfo twin = a;
    twin.number = 5;
    now = {b, twin};  // two identical sticks, number 2 gone: ambiguous -> refuse
    CHECK(findSameDisk(a, now) == nullptr);
}

static void testDiagnose()
{
    DiskInfo d = stick(61920509952ull);
    auto f = diagnose(d);
    bool noPartitions = false;
    for (const auto &s : f)
        noPartitions = noPartitions || s.find("NO partitions") != std::string::npos;
    CHECK(noPartitions);

    d.readOnly = true;
    f = diagnose(d);
    bool ro = false;
    for (const auto &s : f)
        ro = ro || s.find("READ-ONLY") != std::string::npos;
    CHECK(ro);
}

static void testFormatting()
{
    CHECK(humanSize(61920509952ull) == "57.7 GiB");
    CHECK(groupedBytes(61920509952ull) == "61,920,509,952");
    CHECK(groupedBytes(999) == "999");
}

int main()
{
    testEmptyDiskSkipsClean();
    testReadOnlyAndOffline();
    testExistingPartitionsForceClean();
    testUnknownLayoutForcesClean();
    testFat32Cap();
    testGptConversion();
    testValidation();
    testReadOnlyScript();
    testAutomaticClean();
    testStepsMatchLines();
    testSafety();
    testFindSameDisk();
    testDiagnose();
    testFormatting();
    if (g_failed) {
        std::printf("%d check(s) failed\n", g_failed);
        return 1;
    }
    std::printf("all core tests passed\n");
    return 0;
}
