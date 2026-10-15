cmake_minimum_required(VERSION 3.21)
project(FlashMemoryFix VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(FLASHFIX_REQUIRE_ADMIN "Windows: ask for administrator rights at start (needed to format)" ON)
option(FLASHFIX_BUILD_TESTS "Build the unit tests" ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)

if(MSVC)
    add_compile_options(/utf-8 /W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ---- core: data model, diskpart script builder, device enumeration (no Qt) ----
add_library(ffcore STATIC
    src/core/diskinfo.h
    src/core/diskinfo.cpp
    src/core/diskscript.h
    src/core/diskscript.cpp
    src/core/storage.h
)
target_include_directories(ffcore PUBLIC src/core)
if(WIN32)
    target_sources(ffcore PRIVATE src/core/storage_win.cpp)
    target_link_libraries(ffcore PUBLIC advapi32)
else()
    target_sources(ffcore PRIVATE src/core/storage_demo.cpp)  # fake disks for development off Windows
endif()

# ---- GUI ----
qt_add_executable(FlashMemoryFix WIN32
    src/main.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp
    src/ui/formatdialog.h
    src/ui/formatdialog.cpp
    src/ui/confirmdialog.h
    src/ui/confirmdialog.cpp
    src/ui/diskpartrunner.h
    src/ui/diskpartrunner.cpp
    src/ui/filecopier.h
    src/ui/filecopier.cpp
    src/ui/helpdialog.h
    src/ui/helpdialog.cpp
)
target_include_directories(FlashMemoryFix PRIVATE src)
target_link_libraries(FlashMemoryFix PRIVATE Qt6::Widgets ffcore)

if(WIN32 AND FLASHFIX_REQUIRE_ADMIN)
    if(MSVC)
        target_link_options(FlashMemoryFix PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
    else()
        target_sources(FlashMemoryFix PRIVATE resources/app.rc)
        set_source_files_properties(resources/app.rc PROPERTIES OBJECT_DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest)
        target_include_directories(FlashMemoryFix PRIVATE resources)
    endif()
endif()

# ---- tests ----
if(FLASHFIX_BUILD_TESTS)
    enable_testing()
    add_executable(test_core tests/test_core.cpp)
    target_link_libraries(test_core PRIVATE ffcore)
    add_test(NAME core COMMAND test_core)
endif()
