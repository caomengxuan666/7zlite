#include "../include/7zlite.h"
#include "../include/compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Hard link tracking for efficient storage */
/* Structures are defined in 7zlite.h */

static HardLinkTable *g_link_table = NULL;

HardLinkTable* zlite_link_table_create(void) {
    HardLinkTable *table = (HardLinkTable *)malloc(sizeof(HardLinkTable));
    if (!table) {
        return NULL;
    }
    
    table->capacity = 1024;
    table->count = 0;
    table->entries = (struct HardLinkEntry *)calloc(table->capacity, sizeof(struct HardLinkEntry));
    
    if (!table->entries) {
        free(table);
        return NULL;
    }
    
    return table;
}

void zlite_link_table_free(HardLinkTable *table) {
    int i;
    
    if (!table) {
        return;
    }
    
    for (i = 0; i < table->count; i++) {
        if (table->entries[i].first_path) {
            free(table->entries[i].first_path);
        }
    }
    
    free(table->entries);
    free(table);
}

struct HardLinkEntry* zlite_link_table_find_or_add(HardLinkTable *table, 
                                            const char *path,
                                            uint64_t inode,
                                            uint64_t device) {
    int i;
    
    /* First check if we already have this inode/device pair */
    for (i = 0; i < table->count; i++) {
        if (table->entries[i].inode == inode && 
            table->entries[i].device == device) {
            table->entries[i].ref_count++;
            return &table->entries[i];
        }
    }
    
    /* Not found, add new entry */
    if (table->count >= table->capacity) {
        table->capacity *= 2;
        table->entries = (struct HardLinkEntry *)realloc(table->entries, 
                                                  table->capacity * sizeof(struct HardLinkEntry));
        if (!table->entries) {
            return NULL;
        }
    }
    
    table->entries[table->count].inode = inode;
    table->entries[table->count].device = device;
    table->entries[table->count].first_path = strdup(path);
    table->entries[table->count].ref_count = 1;
    
    return &table->entries[table->count++];
}

/* Global link table for archive operations */
HardLinkTable* zlite_get_global_link_table(void) {
    if (!g_link_table) {
        g_link_table = zlite_link_table_create();
    }
    return g_link_table;
}

void zlite_cleanup_global_link_table(void) {
    if (g_link_table) {
        zlite_link_table_free(g_link_table);
        g_link_table = NULL;
    }
}

/* Forward declarations for platform-specific implementations */
extern int zlite_platform_detect_links(const char *path, ZliteFileInfo *info);
extern int zlite_platform_create_link(const char *target, const char *link_path, int link_type);

int zlite_detect_links(const char *path, ZliteFileInfo *info) {
    return zlite_platform_detect_links(path, info);
}

int zlite_create_link(const char *target, const char *link_path, int link_type) {
    return zlite_platform_create_link(target, link_path, link_type);
}