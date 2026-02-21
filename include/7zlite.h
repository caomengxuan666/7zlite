#ifndef ZLITE_H
#define ZLITE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/types.h>
#endif

/* Compression levels */
#define ZLITE_LEVEL_MIN     0
#define ZLITE_LEVEL_MAX     9
#define ZLITE_LEVEL_DEFAULT 5

/* Compression methods */
#define ZLITE_METHOD_LZMA2  0
#define ZLITE_METHOD_LZMA   1

/* Return codes */
#define ZLITE_OK            0
#define ZLITE_ERROR_MEMORY  1
#define ZLITE_ERROR_FILE    2
#define ZLITE_ERROR_PARAM   3
#define ZLITE_ERROR_READ    4
#define ZLITE_ERROR_WRITE   5
#define ZLITE_ERROR_CORRUPT 6
#define ZLITE_ERROR_UNSUPPORTED 7

/* File types */
#define ZLITE_FILETYPE_REGULAR 0
#define ZLITE_FILETYPE_DIR     1
#define ZLITE_FILETYPE_SYMLINK 2
#define ZLITE_FILETYPE_HARDLINK 3

/* Command types */
typedef enum {
    ZLITE_CMD_ADD,
    ZLITE_CMD_EXTRACT,
    ZLITE_CMD_LIST,
    ZLITE_CMD_TEST,
    ZLITE_CMD_DELETE,
    ZLITE_CMD_RENAME
} ZliteCommand;

/* Compression options */
typedef struct {
    int level;
    int method;
    int solid;
    int num_threads;
    uint64_t volume_size;
} ZliteCompressOptions;

/* File info structure */
typedef struct {
    char *path;
    char *link_target;
    uint64_t size;
    uint64_t compressed_size;
    uint32_t attributes;
    uint32_t crc;
    int file_type;
    int is_hardlink;
    uint64_t inode;
    uint64_t device;
} ZliteFileInfo;

/* Archive handle */
typedef struct ZliteArchive ZliteArchive;

/* Hard link table */
typedef struct HardLinkTable HardLinkTable;

/* Archive operations */
ZliteArchive* zlite_archive_create(const char *path, int create);
void zlite_archive_close(ZliteArchive *archive);
const char* zlite_archive_get_path(ZliteArchive *archive);

/* File operations */
int zlite_add_files(ZliteArchive *archive, char **files, int num_files, 
                    const ZliteCompressOptions *options);
int zlite_extract_files(ZliteArchive *archive, const char *output_dir);
int zlite_list_files(ZliteArchive *archive);
int zlite_test_archive(ZliteArchive *archive);

/* File list management */
int zlite_collect_files(char **files, int num_files, ZliteFileInfo **result, 
                        int *result_count);
void zlite_free_file_list(ZliteFileInfo *files, int count);

/* Link support */
int zlite_detect_links(const char *path, ZliteFileInfo *info);
int zlite_create_link(const char *target, const char *link_path, int link_type);

/* Hard link table structures */
struct HardLinkEntry {
    uint64_t inode;
    uint64_t device;
    char *first_path;
    int ref_count;
};

struct HardLinkTable {
    struct HardLinkEntry *entries;
    int capacity;
    int count;
};

/* Hard link table management */
HardLinkTable* zlite_link_table_create(void);
void zlite_link_table_free(HardLinkTable *table);
struct HardLinkEntry* zlite_link_table_find_or_add(HardLinkTable *table, const char *path,
                                            uint64_t inode, uint64_t device);

/* Platform abstraction */
typedef struct {
    uint64_t size;
    time_t mtime;
    time_t atime;
    time_t ctime;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t dev;
    uint64_t ino;
    uint32_t nlink;
    char symlink_target[4096];
} ZliteFileStat;

int zlite_stat_file(const char *path, ZliteFileStat *stat);
int zlite_set_file_times(const char *path, time_t mtime, time_t atime);
int zlite_set_file_mode(const char *path, uint32_t mode);
int zlite_mkdir_recursive(const char *path);

#endif /* 7ZLITE_H */