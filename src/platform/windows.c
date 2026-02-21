#include "../../include/compat.h"
#include "../../include/7zlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <shlwapi.h>

#ifndef _S_IFDIR
#define _S_IFDIR 0040000
#endif

#ifndef _S_IFREG
#define _S_IFREG 0100000
#endif

#pragma comment(lib, "shlwapi.lib")

int zlite_stat_file(const char *path, ZliteFileStat *stat) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    wchar_t wpath[MAX_PATH];
    
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);
    
    if (!GetFileAttributesExW(wpath, GetFileExInfoStandard, &data)) {
        return -1;
    }
    
    stat->size = ((uint64_t)data.nFileSizeHigh << 32) | data.nFileSizeLow;
    stat->mtime = ((uint64_t)data.ftLastWriteTime.dwHighDateTime << 32) | 
                  data.ftLastWriteTime.dwLowDateTime;
    stat->atime = ((uint64_t)data.ftLastAccessTime.dwHighDateTime << 32) | 
                  data.ftLastAccessTime.dwLowDateTime;
    stat->ctime = ((uint64_t)data.ftCreationTime.dwHighDateTime << 32) | 
                  data.ftCreationTime.dwLowDateTime;
    
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
        stat->mode = _S_IFDIR | 0755;
    } else {
        stat->mode = _S_IFREG | 0644;
    }
    
    if (data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
        stat->mode &= ~0222;
    }
    
    stat->uid = 0;
    stat->gid = 0;
    stat->dev = 0;
    stat->ino = 0;
    stat->nlink = 1;
    stat->symlink_target[0] = '\0';
    
    return 0;
}

int zlite_set_file_times(const char *path, time_t mtime, time_t atime) {
    HANDLE hFile;
    FILETIME ft_mtime, ft_atime;
    ULARGE_INTEGER uli;
    wchar_t wpath[MAX_PATH];
    
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);
    
    hFile = CreateFileW(wpath, GENERIC_WRITE, FILE_SHARE_READ, NULL, 
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    
    if (hFile == INVALID_HANDLE_VALUE) {
        return -1;
    }
    
    /* Convert Unix time to Windows FILETIME */
    uli.QuadPart = ((uint64_t)atime + 11644473600ULL) * 10000000ULL;
    ft_atime.dwLowDateTime = uli.LowPart;
    ft_atime.dwHighDateTime = uli.HighPart;
    
    uli.QuadPart = ((uint64_t)mtime + 11644473600ULL) * 10000000ULL;
    ft_mtime.dwLowDateTime = uli.LowPart;
    ft_mtime.dwHighDateTime = uli.HighPart;
    
    BOOL result = SetFileTime(hFile, NULL, &ft_atime, &ft_mtime);
    CloseHandle(hFile);
    
    return result ? 0 : -1;
}

int zlite_set_file_mode(const char *path, uint32_t mode) {
    DWORD attr;
    wchar_t wpath[MAX_PATH];
    
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);
    
    attr = GetFileAttributesW(wpath);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return -1;
    }
    
    if (mode & 0200) {
        attr &= ~FILE_ATTRIBUTE_READONLY;
    } else {
        attr |= FILE_ATTRIBUTE_READONLY;
    }
    
    return SetFileAttributesW(wpath, attr) ? 0 : -1;
}

int zlite_platform_detect_links(const char *path, ZliteFileInfo *info) {
    ZliteFileStat stat;
    wchar_t wpath[MAX_PATH];
    DWORD attr;
    
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);
    
    attr = GetFileAttributesW(wpath);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        return -1;
    }
    
    if (zlite_stat_file(path, &stat) != 0) {
        return -1;
    }
    
    info->device = 0;
    info->inode = 0;
    info->attributes = attr;
    
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) {
        /* Windows symbolic link or junction */
        info->file_type = ZLITE_FILETYPE_SYMLINK;
        info->link_target = NULL;
        info->is_hardlink = 0;
    } else if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        info->file_type = ZLITE_FILETYPE_DIR;
        info->link_target = NULL;
        info->is_hardlink = 0;
    } else {
        info->file_type = ZLITE_FILETYPE_REGULAR;
        info->link_target = NULL;
        info->is_hardlink = 0;
    }
    
    return 0;
}

int zlite_platform_create_link(const char *target, const char *link_path, int link_type) {
    wchar_t wtarget[MAX_PATH], wlink[MAX_PATH];
    
    MultiByteToWideChar(CP_UTF8, 0, target, -1, wtarget, MAX_PATH);
    MultiByteToWideChar(CP_UTF8, 0, link_path, -1, wlink, MAX_PATH);
    
    if (link_type == ZLITE_FILETYPE_SYMLINK) {
        if (CreateSymbolicLinkW(wlink, wtarget, 0)) {
            return 0;
        }
    } else if (link_type == ZLITE_FILETYPE_HARDLINK) {
        if (CreateHardLinkW(wlink, wtarget, NULL)) {
            return 0;
        }
    }
    
    return -1;
}

int zlite_mkdir_recursive(const char *path) {
    char tmp[MAX_PATH];
    char *p;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    
    if (tmp[len - 1] == '\\' || tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '\\' || *p == '/') {
            *p = '\0';
            if (!CreateDirectoryA(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
                return -1;
            }
            *p = PATH_SEPARATOR;
        }
    }
    
    if (!CreateDirectoryA(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        return -1;
    }
    
    return 0;
}
