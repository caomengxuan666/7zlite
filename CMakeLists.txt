cmake_minimum_required(VERSION 3.10)
project(7zlite VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# CPACK configuration
set(CPACK_PACKAGE_NAME "7zlite")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION "A lightweight 7z archive tool with hard link and symlink support")
set(CPACK_PACKAGE_CONTACT "7zLite Project")

# Platform detection
if(WIN32)
    add_definitions(-D_USE_WINDOWS_FILE -D_CRT_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN)
    set(PLATFORM_SRCS 
        src/platform/windows.c
    )
    set(PLATFORM_LIBS)
else()
    add_definitions(-D_POSIX_SOURCE)
    set(PLATFORM_SRCS 
        src/platform/posix.c
    )
    set(PLATFORM_LIBS pthread)
endif()

# LZMA SDK source files
set(LZMA_SRCS
    C/7zAlloc.c
    C/7zArcIn.c
    C/7zBuf.c
    C/7zBuf2.c
    C/7zCrc.c
    C/7zCrcOpt.c
    C/7zDec.c
    C/7zFile.c
    C/7zStream.c
    C/Aes.c
    C/AesOpt.c
    C/Alloc.c
    C/Bcj2.c
    C/Bcj2Enc.c
    C/Bra.c
    C/Bra86.c
    C/BraIA64.c
    C/CpuArch.c
    C/Delta.c
    C/LzFind.c
    C/LzFindMt.c
    C/LzFindOpt.c
    C/Lzma2Dec.c
    C/Lzma2DecMt.c
    C/Lzma2Enc.c
    C/Lzma86Dec.c
    C/Lzma86Enc.c
    C/LzmaDec.c
    C/LzmaEnc.c
    C/MtCoder.c
    C/MtDec.c
    C/Ppmd7.c
    C/Ppmd7Dec.c
    C/Ppmd7Enc.c
    C/Sha256.c
    C/Sha256Opt.c
    C/Sort.c
    C/SwapBytes.c
    C/Threads.c
    C/Xz.c
    C/XzCrc64.c
    C/XzCrc64Opt.c
    C/XzDec.c
    C/XzEnc.c
    C/XzIn.c
)

# 7zLite source files
set(ZLITE_SRCS
    src/main.c
    src/compress.c
    src/decompress.c
    src/archive.c
    src/filelist.c
    src/link.c
    src/cli.c
    ${PLATFORM_SRCS}
)

# Include directories
include_directories(
    C
    include
)

# Create executable
add_executable(7zlite ${ZLITE_SRCS} ${LZMA_SRCS})

if(NOT WIN32)
    target_link_libraries(7zlite ${PLATFORM_LIBS})
else()
    target_link_libraries(7zlite shlwapi)
endif()

# Set compiler warnings
if(MSVC)
    target_compile_options(7zlite PRIVATE /W4)
else()
    target_compile_options(7zlite PRIVATE -Wall -Wextra -pedantic)
endif()

# Optimization
if(CMAKE_BUILD_TYPE MATCHES Release)
    if(MSVC)
        target_compile_options(7zlite PRIVATE /O2)
    else()
        target_compile_options(7zlite PRIVATE -O3)
    endif()
endif()

# CPACK configuration
set(CPACK_PACKAGE_VENDOR "7zLite")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/caomengxuan666/7zlite")

# Linux-specific CPACK settings
if(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "TGZ;DEB;RPM")
    set(CPACK_SOURCE_GENERATOR "TGZ")
    set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_RPM_PACKAGE_LICENSE "Public Domain")
    set(CPACK_RPM_PACKAGE_GROUP "Applications/Archiving")
    set(CPACK_PACKAGE_EXECUTABLES ${CMAKE_INSTALL_PREFIX}/bin)
    install(TARGETS 7zlite DESTINATION bin)
endif()

# Windows-specific CPACK settings
if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "7zLite")
    set(CPACK_NSIS_PACKAGE_NAME "7zLite")
    set(CPACK_NSIS_CONTACT "7zLite Project")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/caomengxuan666/7zlite")
    install(TARGETS 7zlite DESTINATION .)
endif()

include(CPack)