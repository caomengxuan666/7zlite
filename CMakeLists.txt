cmake_minimum_required(VERSION 3.10)
project(7zlite VERSION 1.0.1.3 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Binary size optimization
# Strip debug symbols to reduce size
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION FALSE)
set(CMAKE_SKIP_BUILD_RPATH TRUE)
set(CMAKE_BUILD_RPATH_USE_ORIGIN TRUE)

# Reduce binary size by removing unused sections
# Only add --as-needed on non-Windows platforms
if(NOT WIN32)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--as-needed")
endif()

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

# LZMA SDK source files (minimal set for 7z LZMA2 compression/decompression)
# Removed: Xz* (XZ format), Lzma86* (LZMA86 format), BraIA64 (IA64)
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
    C/CpuArch.c
    C/Delta.c
    C/LzFind.c
    C/LzFindMt.c
    C/LzFindOpt.c
    C/Lzma2Dec.c
    C/Lzma2DecMt.c
    C/Lzma2Enc.c
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

# Compiler detection
if(WIN32)
    target_link_libraries(7zlite shlwapi)
    
    # Detect compiler type on Windows
    if(CMAKE_C_COMPILER_ID MATCHES "MSVC")
        # MSVC compiler - use MSVC linker flags
        set_target_properties(7zlite PROPERTIES
            LINK_FLAGS "/OPT:REF /OPT:ICF"
        )
    elseif(CMAKE_C_COMPILER_ID MATCHES "Clang")
        # Clang compiler on Windows - use MSVC-compatible flags via -Xlinker
        # because lld-link is used on Windows
        set_target_properties(7zlite PROPERTIES
            LINK_FLAGS "-Xlinker /OPT:REF -Xlinker /OPT:ICF"
        )
    elseif(CMAKE_C_COMPILER_ID MATCHES "GNU")
        # GCC compiler on Windows - use GNU linker flags
        set_target_properties(7zlite PROPERTIES
            LINK_FLAGS "-Wl,--gc-sections"
        )
    else()
        # Unknown compiler - try generic GNU flags
        set_target_properties(7zlite PROPERTIES
            LINK_FLAGS "-Wl,--gc-sections"
        )
    endif()
else()
    # Non-Windows platforms (Linux, macOS, etc.)
    target_link_libraries(7zlite ${PLATFORM_LIBS})
    
    # Linker optimization for smaller binary size
    if(CMAKE_C_COMPILER_ID MATCHES "Clang" OR CMAKE_C_COMPILER_ID MATCHES "GNU")
        set_target_properties(7zlite PROPERTIES
            LINK_FLAGS "-Wl,--as-needed -Wl,--gc-sections"
        )
    endif()
    
    # Static linking preference for smaller dependencies
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a ${CMAKE_FIND_LIBRARY_SUFFIXES})
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
    # Strip debug symbols to reduce size
    install(TARGETS 7zlite DESTINATION bin COMPONENT binary)
    install(CODE "execute_process(COMMAND ${CMAKE_STRIP} \$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/7zlite)")
endif()

# Windows-specific CPACK settings
if(WIN32)
    set(CPACK_GENERATOR "ZIP;NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "7zLite")
    set(CPACK_NSIS_PACKAGE_NAME "7zLite")
    set(CPACK_NSIS_CONTACT "7zLite Project")
    set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/caomengxuan666/7zlite")
    # Strip debug symbols on Windows (if strip is available)
    install(TARGETS 7zlite DESTINATION . COMPONENT binary)
    install(CODE "
        if(CMAKE_STRIP)
            execute_process(COMMAND \${CMAKE_STRIP} --strip-all \${CMAKE_INSTALL_PREFIX}/7zlite.exe)
        endif()
    ")
endif()

include(CPack)