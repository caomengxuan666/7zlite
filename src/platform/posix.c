#include "../../include/compat.h"
#include "../../include/7zlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/xattr.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* Define AT_FDCWD if not available (older systems) */
#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif

int zlite_stat_file(const char *path, ZliteFileStat *stat) {
    struct stat st;
    
    if (lstat(path, &st) != 0) {
        return -1;
    }
    
    stat->size = st.st_size;
    stat->mtime = st.st_mtime;
    stat->atime = st.st_atime;
    stat->ctime = st.st_ctime;
    stat->mode = st.st_mode;
    stat->uid = st.st_uid;
    stat->gid = st.st_gid;
    stat->dev = st.st_dev;
    stat->ino = st.st_ino;
    stat->nlink = st.st_nlink;
    
    if (S_ISLNK(st.st_mode)) {
        ssize_t len = readlink(path, stat->symlink_target, sizeof(stat->symlink_target) - 1);
        if (len >= 0) {
            stat->symlink_target[len] = '\0';
        } else {
            stat->symlink_target[0] = '\0';
        }
    } else {
        stat->symlink_target[0] = '\0';
    }
    
    return 0;
}

int zlite_set_file_times(const char *path, time_t mtime, time_t atime) {
    struct timespec times[2];
    
    times[0].tv_sec = atime;
    times[0].tv_nsec = 0;
    times[1].tv_sec = mtime;
    times[1].tv_nsec = 0;
    
    return utimensat(AT_FDCWD, path, times, 0);
}

int zlite_set_file_mode(const char *path, uint32_t mode) {
    return chmod(path, mode);
}

int zlite_platform_detect_links(const char *path, ZliteFileInfo *info) {
    ZliteFileStat stat;
    
    if (zlite_stat_file(path, &stat) != 0) {
        return -1;
    }
    
    info->device = stat.dev;
    info->inode = stat.ino;
    
    if (S_ISLNK(stat.mode)) {
        info->file_type = ZLITE_FILETYPE_SYMLINK;
        info->link_target = strdup(stat.symlink_target);
        info->is_hardlink = 0;
    } else if (S_ISREG(stat.mode) && stat.nlink > 1) {
        info->file_type = ZLITE_FILETYPE_HARDLINK;
        info->link_target = NULL;
        info->is_hardlink = 1;
    } else if (S_ISDIR(stat.mode)) {
        info->file_type = ZLITE_FILETYPE_DIR;
        info->link_target = NULL;
        info->is_hardlink = 0;
    } else {
        info->file_type = ZLITE_FILETYPE_REGULAR;
        info->link_target = NULL;
        info->is_hardlink = 0;
    }
    
    info->attributes = stat.mode;
    
    return 0;
}

int zlite_platform_create_link(const char *target, const char *link_path, int link_type) {
    if (link_type == ZLITE_FILETYPE_SYMLINK) {
        return symlink(target, link_path);
    } else if (link_type == ZLITE_FILETYPE_HARDLINK) {
        return link(target, link_path);
    }
    return -1;
}

int zlite_mkdir_recursive(const char *path) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
        return -1;
    }
    
    return 0;
}