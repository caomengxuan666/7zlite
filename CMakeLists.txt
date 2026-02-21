cmake_minimum_required(VERSION 3.10)
project(7zlite C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Platform detection
if(WIN32)
    add_definitions(-D_USE_WINDOWS_FILE)
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