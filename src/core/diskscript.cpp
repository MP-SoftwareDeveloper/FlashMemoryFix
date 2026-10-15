#include "diskscript.h"

namespace ff {

namespace {

constexpr std::uint64_t kMiB = 1024ull * 1024ull;

Script fail(const std::string &msg)
{
    Script s;
    s.ok = false;
    s.error = msg;
    return s;
}

void addPrologue(Script &s, const DiskInfo &d)
{
    s.lines.push_back("select disk " + std::to_string(d.number));
    if (d.readOnly) {
        s.lines.push_back("attributes disk clear readonly");
        s.steps.push_back("Clear the read-only flag");
    }
    if (d.offline) {
        s.lines.push_back("online disk noerr");
        s.steps.push_back("Bring the disk online");
    }
}

}  // namespace

const char *fsName(Fs fs)
{
    switch (fs) {
    case Fs::Fat32: return "FAT32";
    case Fs::ExFat: return "exFAT";
    case Fs::Ntfs: return "NTFS";
    }
    return "?";
}

std::size_t maxLabelLength(Fs fs)
{
    return fs == Fs::Ntfs ? 32 : 11;
}

std::string checkLabel(Fs fs, const std::string &label)
{
    if (label.size() > maxLabelLength(fs))
        return std::string("The label is too long for ") + fsName(fs) + " (max " +
               std::to_string(maxLabelLength(fs)) + " characters).";
    for (unsigned char c : label) {
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' ||
                        c == '_' || c == '-';
        if (!ok)
            return "The label may only contain letters, digits, space, '_' and '-'.";
    }
    return {};
}

Script buildClearReadOnlyScript(const DiskInfo &d)
{
    if (d.number < 0)
        return fail("No disk selected.");
    Script s;
    s.lines.push_back("select disk " + std::to_string(d.number));
    s.lines.push_back("attributes disk clear readonly");
    s.lines.push_back("online disk noerr");
    s.steps.push_back("Clear the read-only flag and bring the disk online");
    s.ok = true;
    return s;
}

Script buildFormatScript(const DiskInfo &d, const FormatOptions &o)
{
    if (d.number < 0)
        return fail("No disk selected.");
    if (o.style != PartStyle::Mbr && o.style != PartStyle::Gpt)
        return fail("Partition style must be MBR or GPT.");
    if (const std::string e = checkLabel(o.fs, o.label); !e.empty())
        return fail(e);

    static const unsigned kUnits[] = {0, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536};
    bool unitOk = false;
    for (unsigned u : kUnits)
        unitOk = unitOk || (u == o.unitBytes);
    if (!unitOk)
        return fail("Unsupported allocation unit size.");

    const std::uint64_t diskMB = d.size / kMiB;
    const std::uint64_t minMB = o.fs == Fs::Fat32 ? kFat32MinMB : kOtherMinMB;
    if (diskMB < minMB + 4)
        return fail("The disk is too small for this file system.");

    Script s;
    std::uint64_t sizeMB = o.sizeMB;  // 0 = whole disk
    if (sizeMB != 0) {
        if (sizeMB < minMB)
            return fail(std::string("The partition must be at least ") + std::to_string(minMB) + " MB for " +
                        fsName(o.fs) + ".");
        if (sizeMB + 4 > diskMB)
            return fail("The partition is larger than the disk.");
    }
    if (o.fs == Fs::Fat32) {
        const bool capped = (sizeMB == 0 && diskMB > kFat32MaxMB) || sizeMB > kFat32MaxMB;
        if (capped) {
            sizeMB = kFat32MaxMB;
            s.notes.push_back("Windows can only format FAT32 up to 32 GB. A " + std::to_string(kFat32MaxMB) +
                              " MB partition is created; about " + humanSize((diskMB - sizeMB) * kMiB) +
                              " stay unallocated. Choose exFAT to use the whole disk.");
        }
    }

    // Cleaning is automatic. A disk without partitions is NOT cleaned: some sticks refuse "clean"
    // (access denied) yet accept a new partition, and there is nothing to wipe anyway.
    const bool hasPartitions = !d.partitions.empty();
    const bool needClean = o.zeroAll || !d.layoutKnown || hasPartitions;

    addPrologue(s, d);
    if (needClean) {
        if (o.zeroAll) {
            s.lines.push_back("clean all");
            s.steps.push_back("Overwrite every sector with zeros (\"clean all\")");
            s.notes.push_back("Zeroing writes every sector. On a large stick this can take hours.");
        } else {
            s.lines.push_back("clean");
            s.steps.push_back(hasPartitions ? "Erase the existing partitions and their data (clean)"
                                            : "Erase the partition table (clean), because it could not be read");
        }
    }

    // After "clean" the disk is uninitialized: "create partition" makes MBR by itself, GPT needs a convert.
    const char *styleLabel = o.style == PartStyle::Gpt ? "GPT" : "MBR";
    if (needClean) {
        if (o.style == PartStyle::Gpt) {
            s.lines.push_back("convert gpt");
            s.steps.push_back("Initialize the disk as GPT");
        } else {
            s.steps.push_back("Initialize the disk as MBR");
        }
    } else if (o.style == PartStyle::Gpt && d.style != PartStyle::Gpt) {
        s.lines.push_back("convert gpt");
        s.steps.push_back("Convert the empty disk to GPT");
    } else if (o.style == PartStyle::Mbr && d.style == PartStyle::Gpt) {
        s.lines.push_back("convert mbr");
        s.steps.push_back("Convert the empty disk to MBR");
    } else {
        s.steps.push_back(std::string("Keep the existing ") + styleLabel + " layout (no partitions to remove)");
    }

    std::string create = "create partition primary";
    if (sizeMB != 0)
        create += " size=" + std::to_string(sizeMB);
    s.lines.push_back(create);
    s.steps.push_back(sizeMB != 0 ? "Create one primary partition of " + std::to_string(sizeMB) + " MB"
                                  : "Create one primary partition using the whole disk");

    std::string fmt = std::string("format fs=") + (o.fs == Fs::Fat32 ? "fat32" : o.fs == Fs::ExFat ? "exfat" : "ntfs");
    if (o.quick)
        fmt += " quick";
    if (!o.label.empty())
        fmt += " label=\"" + o.label + "\"";
    if (o.unitBytes != 0)
        fmt += " unit=" + std::to_string(o.unitBytes);
    s.lines.push_back(fmt);
    s.steps.push_back(std::string("Format it as ") + fsName(o.fs) + (o.quick ? " (quick)" : " (full)") +
                      (o.label.empty() ? std::string() : ", label \"" + o.label + "\""));
    s.lines.push_back("assign");
    s.steps.push_back("Assign a drive letter");

    s.ok = true;
    return s;
}

std::string scriptToText(const Script &s)
{
    std::string out;
    for (const auto &l : s.lines)
        out += l + "\n";
    return out;
}

}  // namespace ff
