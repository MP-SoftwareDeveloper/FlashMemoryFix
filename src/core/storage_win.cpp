// Win32 device enumeration: \\.\PhysicalDriveN + IOCTLs. No PowerShell, no WMI.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <initializer_list>
#include <string>
#include <vector>

#include "storage.h"

namespace ff {

namespace {

struct Handle {
    HANDLE h;
    explicit Handle(HANDLE x) : h(x) {}
    ~Handle()
    {
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    bool ok() const { return h != INVALID_HANDLE_VALUE; }
};

std::string toUtf8(const wchar_t *w, int len = -1)
{
    if (!w)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, nullptr, nullptr);
    while (!s.empty() && s.back() == '\0')
        s.pop_back();
    return s;
}

std::string trim(std::string s)
{
    const char *ws = " \t\r\n";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string::npos)
        return {};
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::string stringAt(const BYTE *base, DWORD total, DWORD offset)
{
    if (offset == 0 || offset >= total)
        return {};
    const char *p = reinterpret_cast<const char *>(base) + offset;
    const std::size_t maxLen = total - offset;
    std::size_t len = 0;
    while (len < maxLen && p[len] != '\0')
        ++len;
    return trim(std::string(p, len));
}

BusType mapBus(int bus)
{
    switch (bus) {
    case 1: return BusType::Scsi;
    case 2:
    case 3: return BusType::Ata;
    case 7: return BusType::Usb;
    case 0xA: return BusType::Scsi;  // SAS
    case 0xB: return BusType::Sata;
    case 0xC: return BusType::Sd;
    case 0xD: return BusType::Mmc;
    case 0xE:
    case 0xF: return BusType::Virtual;
    case 0x11: return BusType::Nvme;
    case 0: return BusType::Unknown;
    default: return BusType::Other;
    }
}

bool queryDescriptor(HANDLE h, DiskInfo &d)
{
    STORAGE_PROPERTY_QUERY q;
    std::memset(&q, 0, sizeof q);
    q.PropertyId = StorageDeviceProperty;
    q.QueryType = PropertyStandardQuery;
    std::vector<BYTE> buf(4096);
    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, buf.data(), static_cast<DWORD>(buf.size()),
                         &ret, nullptr) ||
        ret < sizeof(STORAGE_DEVICE_DESCRIPTOR))
        return false;
    const auto *desc = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR *>(buf.data());
    const DWORD total = std::min<DWORD>(ret, desc->Size);
    d.vendor = stringAt(buf.data(), total, desc->VendorIdOffset);
    d.product = stringAt(buf.data(), total, desc->ProductIdOffset);
    d.revision = stringAt(buf.data(), total, desc->ProductRevisionOffset);
    d.serial = stringAt(buf.data(), total, desc->SerialNumberOffset);
    d.bus = mapBus(static_cast<int>(desc->BusType));
    d.removableFlag = desc->RemovableMedia != FALSE;
    return true;
}

bool queryGeometrySize(HANDLE h, DiskInfo &d)
{
    std::vector<BYTE> buf(1024);
    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, buf.data(), static_cast<DWORD>(buf.size()),
                         &ret, nullptr))
        return false;
    const auto *g = reinterpret_cast<const DISK_GEOMETRY_EX *>(buf.data());
    d.size = static_cast<std::uint64_t>(g->DiskSize.QuadPart);
    return true;
}

// Local copies of the documented GET_DISK_ATTRIBUTES definitions: older MinGW headers lack them.
constexpr DWORD kIoctlGetDiskAttributes = CTL_CODE(IOCTL_DISK_BASE, 0x003c, METHOD_BUFFERED, FILE_READ_ACCESS);
constexpr DWORDLONG kDiskAttrOffline = 0x1;
constexpr DWORDLONG kDiskAttrReadOnly = 0x2;
struct DiskAttributesOut {
    DWORD Version;
    DWORD Reserved1;
    DWORDLONG Attributes;
};

void queryAttributes(HANDLE h, DiskInfo &d)
{
    DiskAttributesOut a;
    std::memset(&a, 0, sizeof a);
    DWORD ret = 0;
    if (DeviceIoControl(h, kIoctlGetDiskAttributes, nullptr, 0, &a, sizeof a, &ret, nullptr)) {
        d.readOnly = (a.Attributes & kDiskAttrReadOnly) != 0;
        d.offline = (a.Attributes & kDiskAttrOffline) != 0;
        return;
    }
    // Fallback: a write-protected disk fails IOCTL_DISK_IS_WRITABLE with ERROR_WRITE_PROTECT.
    if (!DeviceIoControl(h, IOCTL_DISK_IS_WRITABLE, nullptr, 0, nullptr, 0, &ret, nullptr) &&
        GetLastError() == ERROR_WRITE_PROTECT)
        d.readOnly = true;
}

bool sameGuid(const GUID &a, unsigned long d1, unsigned short d2, unsigned short d3, std::initializer_list<unsigned char> d4)
{
    if (a.Data1 != d1 || a.Data2 != d2 || a.Data3 != d3)
        return false;
    int i = 0;
    for (unsigned char b : d4)
        if (a.Data4[i++] != b)
            return false;
    return true;
}

std::string hexByte(unsigned v)
{
    static const char *digits = "0123456789ABCDEF";
    std::string s = "0x";
    s += digits[(v >> 4) & 0xF];
    s += digits[v & 0xF];
    return s;
}

std::string mbrTypeName(unsigned t)
{
    switch (t) {
    case 0x01: return "FAT12 (0x01)";
    case 0x04:
    case 0x06:
    case 0x0E: return "FAT16 (" + hexByte(t) + ")";
    case 0x05:
    case 0x0F: return "Extended (" + hexByte(t) + ")";
    case 0x07: return "NTFS / exFAT (0x07)";
    case 0x0B:
    case 0x0C: return "FAT32 (" + hexByte(t) + ")";
    case 0x82: return "Linux swap (0x82)";
    case 0x83: return "Linux (0x83)";
    case 0xEE: return "GPT protective (0xEE)";
    default: return "Type " + hexByte(t);
    }
}

std::string gptTypeName(const GUID &g)
{
    if (sameGuid(g, 0xEBD0A0A2, 0xB9E5, 0x4433, {0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7}))
        return "Basic data";
    if (sameGuid(g, 0xC12A7328, 0xF81F, 0x11D2, {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}))
        return "EFI system";
    if (sameGuid(g, 0xE3C9E316, 0x0B5C, 0x4DB8, {0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE}))
        return "Microsoft reserved";
    return "GPT partition";
}

// Returns true if the layout was read (or the disk is clearly uninitialized).
bool queryLayout(HANDLE h, DiskInfo &d)
{
    std::vector<BYTE> buf(32 * 1024);
    DWORD ret = 0;
    bool got = false;
    DWORD err = 0;
    for (int attempt = 0; attempt < 4 && !got; ++attempt) {
        got = DeviceIoControl(h, IOCTL_DISK_GET_DRIVE_LAYOUT_EX, nullptr, 0, buf.data(),
                              static_cast<DWORD>(buf.size()), &ret, nullptr) != FALSE;
        if (got)
            break;
        err = GetLastError();
        if (err != ERROR_INSUFFICIENT_BUFFER)
            break;
        buf.resize(buf.size() * 4);
    }
    if (!got) {
        if (err == ERROR_UNRECOGNIZED_VOLUME) {  // uninitialized disk
            d.style = PartStyle::Raw;
            d.layoutKnown = true;
            return true;
        }
        return false;
    }

    const auto *L = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX *>(buf.data());
    switch (L->PartitionStyle) {
    case PARTITION_STYLE_MBR: d.style = PartStyle::Mbr; break;
    case PARTITION_STYLE_GPT: d.style = PartStyle::Gpt; break;
    default: d.style = PartStyle::Raw; break;
    }

    const std::size_t headerSize = offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry);
    const std::size_t maxEntries =
        ret > headerSize ? (ret - headerSize) / sizeof(PARTITION_INFORMATION_EX) : 0;
    const std::size_t count = std::min<std::size_t>(L->PartitionCount, maxEntries);
    for (std::size_t i = 0; i < count; ++i) {
        const PARTITION_INFORMATION_EX &e = L->PartitionEntry[i];
        if (e.PartitionLength.QuadPart <= 0)
            continue;
        PartitionInfo p;
        p.number = static_cast<int>(e.PartitionNumber);
        p.offset = static_cast<std::uint64_t>(e.StartingOffset.QuadPart);
        p.length = static_cast<std::uint64_t>(e.PartitionLength.QuadPart);
        if (e.PartitionStyle == PARTITION_STYLE_MBR) {
            p.type = mbrTypeName(e.Mbr.PartitionType);
        } else if (e.PartitionStyle == PARTITION_STYLE_GPT) {
            p.type = gptTypeName(e.Gpt.PartitionType);
            const std::string name = toUtf8(e.Gpt.Name);
            if (!name.empty())
                p.type += " \"" + name + "\"";
        }
        d.partitions.push_back(p);
    }
    d.layoutKnown = true;
    return true;
}

struct LetterInfo {
    char letter = 0;
    int disk = -1;
    int part = 0;
    std::string fs;
    std::string label;
    bool hasSpace = false;
    std::uint64_t freeBytes = 0;
};

std::vector<LetterInfo> scanLetters()
{
    std::vector<LetterInfo> out;
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(mask & (1u << i)))
            continue;
        wchar_t root[] = L"X:\\";
        root[0] = static_cast<wchar_t>(L'A' + i);
        const UINT type = GetDriveTypeW(root);
        if (type != DRIVE_REMOVABLE && type != DRIVE_FIXED)
            continue;

        wchar_t dev[] = L"\\\\.\\X:";
        dev[4] = static_cast<wchar_t>(L'A' + i);
        Handle h(CreateFileW(dev, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
        if (!h.ok())
            continue;
        STORAGE_DEVICE_NUMBER sdn;
        std::memset(&sdn, 0, sizeof sdn);
        DWORD ret = 0;
        if (!DeviceIoControl(h.h, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &sdn, sizeof sdn, &ret, nullptr))
            continue;
        if (sdn.DeviceType != FILE_DEVICE_DISK)
            continue;

        LetterInfo li;
        li.letter = static_cast<char>('A' + i);
        li.disk = static_cast<int>(sdn.DeviceNumber);
        li.part = static_cast<int>(sdn.PartitionNumber);

        wchar_t label[MAX_PATH + 1] = {0};
        wchar_t fsn[MAX_PATH + 1] = {0};
        if (GetVolumeInformationW(root, label, MAX_PATH + 1, nullptr, nullptr, nullptr, fsn, MAX_PATH + 1)) {
            li.label = toUtf8(label);
            li.fs = toUtf8(fsn);
        }
        ULARGE_INTEGER freeB, totalB, totalFree;
        if (GetDiskFreeSpaceExW(root, &freeB, &totalB, &totalFree)) {
            li.hasSpace = true;
            li.freeBytes = freeB.QuadPart;
        }
        out.push_back(li);
    }
    return out;
}

}  // namespace

bool isElevated()
{
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok))
        return false;
    TOKEN_ELEVATION e;
    DWORD sz = 0;
    const BOOL ok = GetTokenInformation(tok, TokenElevation, &e, sizeof e, &sz);
    CloseHandle(tok);
    return ok && e.TokenIsElevated != 0;
}

bool isDemoBackend()
{
    return false;
}

std::vector<DiskInfo> enumerateDisks()
{
    const UINT oldMode = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    std::vector<DiskInfo> result;

    const std::vector<LetterInfo> letters = scanLetters();

    int systemDisk = -1;
    wchar_t sysDir[MAX_PATH] = {0};
    if (GetSystemDirectoryW(sysDir, MAX_PATH) >= 2 && sysDir[1] == L':') {
        const char sysLetter = static_cast<char>(towupper(sysDir[0]));
        for (const auto &li : letters)
            if (li.letter == sysLetter)
                systemDisk = li.disk;
    }

    for (int n = 0; n < 64; ++n) {
        const std::wstring path = L"\\\\.\\PhysicalDrive" + std::to_wstring(n);
        bool full = true;
        Handle h(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             0, nullptr));
        if (!h.ok()) {
            const DWORD err = GetLastError();
            if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                continue;
            // Typically ERROR_ACCESS_DENIED without administrator rights: still read the basics.
            h.h = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            full = false;
            if (!h.ok())
                continue;
        }

        DiskInfo d;
        d.number = n;
        if (!queryDescriptor(h.h, d))
            continue;
        if (!queryGeometrySize(h.h, d) || d.size == 0)
            continue;  // empty card-reader slot etc.
        if (full) {
            queryAttributes(h.h, d);
            queryLayout(h.h, d);
        }
        d.isSystemDisk = (n == systemDisk);

        for (const auto &li : letters) {
            if (li.disk != n)
                continue;
            const std::string l = std::string(1, li.letter) + ":";
            if (!d.letters.empty())
                d.letters += " ";
            d.letters += l;
            for (auto &p : d.partitions) {
                if (p.number == li.part) {
                    p.letter = l;
                    p.fileSystem = li.fs;
                    p.label = li.label;
                    p.hasSpaceInfo = li.hasSpace;
                    p.freeBytes = li.freeBytes;
                }
            }
        }
        result.push_back(std::move(d));
    }

    SetErrorMode(oldMode);
    return result;
}

}  // namespace ff
