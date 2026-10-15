// Device enumeration. Implemented with the Win32 API in storage_win.cpp; on other
// platforms storage_demo.cpp returns fake disks so the GUI can be developed and tested anywhere.
#pragma once

#include <vector>

#include "diskinfo.h"

namespace ff {

// All physical disks the system reports, ordered by disk number. Never throws.
std::vector<DiskInfo> enumerateDisks();

// True if the process runs with administrator rights (needed to clean / partition / format).
bool isElevated();

// True when enumerateDisks() returns fake data (non-Windows build).
bool isDemoBackend();

}  // namespace ff
