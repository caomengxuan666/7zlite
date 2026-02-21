#include "../include/7zlite.h"
#include "../include/compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ZliteArchive {
    char *path;
    int is_writable;
    FILE *fp;
    void *internal_data;
};

ZliteArchive* zlite_archive_create(const char *path, int create) {
    ZliteArchive *archive;
    const char *mode;
    
    archive = (ZliteArchive *)malloc(sizeof(ZliteArchive));
    if (!archive) {
        return NULL;
    }
    
    archive->path = strdup(path);
    if (!archive->path) {
        free(archive);
        return NULL;
    }
    
    archive->is_writable = create;
    mode = create ? "wb" : "rb";
    
    archive->fp = fopen(path, mode);
    if (!archive->fp) {
        free(archive->path);
        free(archive);
        return NULL;
    }
    
    archive->internal_data = NULL;
    
    return archive;
}

void zlite_archive_close(ZliteArchive *archive) {
    if (!archive) {
        return;
    }
    
    if (archive->fp) {
        fclose(archive->fp);
    }
    
    if (archive->path) {
        free(archive->path);
    }
    
    if (archive->internal_data) {
        free(archive->internal_data);
    }
    
    free(archive);
}

const char* zlite_archive_get_path(ZliteArchive *archive) {
    return archive ? archive->path : NULL;
}