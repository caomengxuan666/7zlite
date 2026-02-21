#include "../include/7zlite.h"
#include "../include/compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fnmatch.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#ifdef ZLITE_USE_POSIX
    #include <unistd.h>
    #include <sys/stat.h>
#endif

#ifdef ZLITE_USE_POSIX
    #include <dirent.h>
    #include <sys/stat.h>
#endif

typedef struct {
    ZliteFileInfo *files;
    int capacity;
    int count;
} FileList;

static FileList* filelist_create(void) {
    FileList *list = (FileList *)malloc(sizeof(FileList));
    if (!list) {
        return NULL;
    }
    
    list->capacity = 1024;
    list->count = 0;
    list->files = (ZliteFileInfo *)calloc(list->capacity, sizeof(ZliteFileInfo));
    
    if (!list->files) {
        free(list);
        return NULL;
    }
    
    return list;
}

static void filelist_free(FileList *list) {
    int i;
    
    if (!list) {
        return;
    }
    
    for (i = 0; i < list->count; i++) {
        if (list->files[i].path) {
            free(list->files[i].path);
        }
        if (list->files[i].link_target) {
            free(list->files[i].link_target);
        }
    }
    
    free(list->files);
    free(list);
}

static int filelist_add(FileList *list, const char *path, const ZliteFileInfo *info) {
    if (list->count >= list->capacity) {
        list->capacity *= 2;
        list->files = (ZliteFileInfo *)realloc(list->files, 
                                              list->capacity * sizeof(ZliteFileInfo));
        if (!list->files) {
            return -1;
        }
    }
    
    list->files[list->count].path = strdup(path);
    list->files[list->count].size = info->size;
    list->files[list->count].attributes = info->attributes;
    list->files[list->count].file_type = info->file_type;
    list->files[list->count].is_hardlink = info->is_hardlink;
    list->files[list->count].inode = info->inode;
    list->files[list->count].device = info->device;
    list->files[list->count].crc = 0;
    list->files[list->count].compressed_size = 0;
    
    if (info->link_target) {
        list->files[list->count].link_target = strdup(info->link_target);
    } else {
        list->files[list->count].link_target = NULL;
    }
    
    list->count++;
    
    return 0;
}

static int is_pattern(const char *str) {
    return (strchr(str, '*') != NULL || strchr(str, '?') != NULL);
}

static int filelist_add_recursive(FileList *list, const char *path, 
                                   HardLinkTable *link_table) {
#ifdef ZLITE_USE_POSIX
    DIR *dir;
    struct dirent *entry;
    struct stat st;
    char full_path[PATH_MAX];
    ZliteFileInfo info;
    
    if (lstat(path, &st) != 0) {
        return -1;
    }
    
    if (zlite_detect_links(path, &info) != 0) {
        return -1;
    }
    
    info.size = st.st_size;
    
    /* Check if this is a hard link we've already processed */
    if (info.is_hardlink) {
        struct HardLinkEntry *entry = zlite_link_table_find_or_add(link_table, path, 
                                                             info.inode, info.device);
        if (entry && entry->ref_count > 1 && strcmp(entry->first_path, path) != 0) {
            /* This is a duplicate hard link, just record it */
            filelist_add(list, path, &info);
            return 0;
        }
    }
    
    if (filelist_add(list, path, &info) != 0) {
        return -1;
    }
    
    /* If it's a directory, recurse */
    if (S_ISDIR(st.st_mode)) {
        dir = opendir(path);
        if (!dir) {
            return -1;
        }
        
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }
            
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            filelist_add_recursive(list, full_path, link_table);
        }
        
        closedir(dir);
    }
    
    return 0;
#else
    /* Windows implementation would use FindFirstFile/FindNextFile */
    return -1;
#endif
}

int zlite_collect_files(char **files, int num_files, ZliteFileInfo **result, 
                        int *result_count) {
    FileList *list;
    HardLinkTable *link_table;
    int i;
    
    list = filelist_create();
    if (!list) {
        return ZLITE_ERROR_MEMORY;
    }
    
    link_table = zlite_link_table_create();
    if (!link_table) {
        filelist_free(list);
        return ZLITE_ERROR_MEMORY;
    }
    
    for (i = 0; i < num_files; i++) {
        if (is_pattern(files[i])) {
            /* TODO: Implement pattern matching */
            /* For now, just treat as regular path */
        }
        
        if (filelist_add_recursive(list, files[i], link_table) != 0) {
            filelist_free(list);
            zlite_link_table_free(link_table);
            return ZLITE_ERROR_FILE;
        }
    }
    
    *result = list->files;
    *result_count = list->count;
    
    /* Free the list structure but keep the files array */
    free(list);
    zlite_link_table_free(link_table);
    
    return ZLITE_OK;
}

void zlite_free_file_list(ZliteFileInfo *files, int count) {
    int i;
    
    if (!files) {
        return;
    }
    
    for (i = 0; i < count; i++) {
        if (files[i].path) {
            free(files[i].path);
        }
        if (files[i].link_target) {
            free(files[i].link_target);
        }
    }
    
    free(files);
}