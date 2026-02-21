#include "../include/7zlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Debug macro */
#define DEBUG_COMPRESSION 0

#if DEBUG_COMPRESSION
#define DEBUG_PRINT(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...) do {} while(0)
#endif

/* Debug macro */
#define DEBUG_DECOMPRESSION 1

#if DEBUG_DECOMPRESSION
#define DEBUG_PRINT(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINT(fmt, ...) do {} while(0)
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "7z.h"
#include "7zAlloc.h"
#include "7zFile.h"
#include "7zCrc.h"
#include "Lzma2Enc.h"
#include "LzmaEnc.h"

/* Use LZMA SDK's LZMA_PROPS_SIZE definition if available */
#ifndef LZMA_PROPS_SIZE
#define LZMA_PROPS_SIZE 1
#endif

static ISzAlloc g_Alloc = { SzAlloc, SzFree };

static int compress_file_lzma2(const char *input_path, const char *output_path,
                               int level, uint64_t *compressed_size) {
    CLzma2EncHandle enc;
    CFileSeqInStream inStream;
    CFileOutStream outStream;
    Byte prop;
    SRes res;
    WRes wres;
    uint64_t file_size;

    /* Open input file */
    wres = InFile_Open(&inStream.file, input_path);
    if (wres != 0) {
        return ZLITE_ERROR_FILE;
    }
    
    /* Get input file size */
    File_GetLength(&inStream.file, &file_size);
    
    /* Open output file */
    wres = OutFile_Open(&outStream.file, output_path);
    if (wres != 0) {
        File_Close(&inStream.file);
        return ZLITE_ERROR_FILE;
    }
    
    /* Setup stream vtables */
    FileSeqInStream_CreateVTable(&inStream);
    FileOutStream_CreateVTable(&outStream);
    
    /* Create encoder */
    enc = Lzma2Enc_Create(&g_Alloc, &g_Alloc);
    if (!enc) {
        File_Close(&inStream.file);
        File_Close(&outStream.file);
        return ZLITE_ERROR_MEMORY;
    }
    
    /* Set encoder properties */
    {
        CLzma2EncProps props2;
        Lzma2EncProps_Init(&props2);
        
        /* Enable end mark */
        props2.lzmaProps.writeEndMark = 1;
        
        /* Map compression level to properties */
        switch (level) {
            case 0:
                props2.lzmaProps.level = 0;
                props2.lzmaProps.dictSize = 1 << 16;
                break;
            case 1:
                props2.lzmaProps.level = 1;
                props2.lzmaProps.dictSize = 1 << 20;
                break;
            case 2:
                props2.lzmaProps.level = 3;
                props2.lzmaProps.dictSize = 1 << 22;
                break;
            case 3:
                props2.lzmaProps.level = 5;
                props2.lzmaProps.dictSize = 1 << 24;
                break;
            case 4:
                props2.lzmaProps.level = 7;
                props2.lzmaProps.dictSize = 1 << 25;
                break;
            case 5:
            case 6:
                props2.lzmaProps.level = 7;
                props2.lzmaProps.dictSize = 1 << 26;
                break;
            case 7:
            case 8:
            case 9:
                props2.lzmaProps.level = 9;
                props2.lzmaProps.dictSize = 1 << 26;
                break;
            default:
                props2.lzmaProps.level = 5;
                props2.lzmaProps.dictSize = 1 << 26;
                break;
        }
        
        Lzma2EncProps_Normalize(&props2);
        res = Lzma2Enc_SetProps(enc, &props2);
        if (res != SZ_OK) {
            Lzma2Enc_Destroy(enc);
            File_Close(&inStream.file);
            File_Close(&outStream.file);
            return ZLITE_ERROR_PARAM;
        }
    }
    
    /* Get and write encoder properties */
    {
        prop = Lzma2Enc_WriteProperties(enc);
        size_t written = 1;
        wres = File_Write(&outStream.file, &prop, &written);
        if (wres != 0 || written != 1) {
            Lzma2Enc_Destroy(enc);
            File_Close(&inStream.file);
            File_Close(&outStream.file);
            return ZLITE_ERROR_WRITE;
        }
    }
    
    /* Encode */
    DEBUG_PRINT("DEBUG: Starting encoding...\n");
    res = Lzma2Enc_Encode2(enc, &outStream.vt, NULL, 0, &inStream.vt, NULL, 0, NULL);
    DEBUG_PRINT("DEBUG: Encoding result: %d\n", res);
    
    /* Get compressed size */
    File_GetLength(&outStream.file, compressed_size);
    DEBUG_PRINT("DEBUG: Compressed file size (with prop): %llu\n", (unsigned long long)*compressed_size);
    *compressed_size -= 1; /* Subtract prop byte */
    DEBUG_PRINT("DEBUG: Compressed data size (without prop): %llu\n", (unsigned long long)*compressed_size);
    
    Lzma2Enc_Destroy(enc);
    File_Close(&inStream.file);
    File_Close(&outStream.file);
    
    return (res == SZ_OK) ? ZLITE_OK : ZLITE_ERROR_CORRUPT;
}

/* Simple archive header for testing */
#define ARCHIVE_MAGIC "7z\xBC\xAF\x27\x1C"

int zlite_add_files(ZliteArchive *archive, char **files, int num_files,
                    const ZliteCompressOptions *options) {
    ZliteFileInfo *file_list;
    int file_count;
    int i;
    int result;
    FILE *archive_fp;
    uint64_t total_files = 0;
    uint64_t total_size = 0;

    /* Collect files */
    result = zlite_collect_files(files, num_files, &file_list, &file_count);

    if (result != ZLITE_OK) {
        return result;
    }

    /* Initialize CRC table */
    CrcGenerateTable();

    /* Open archive file */
    archive_fp = fopen(zlite_archive_get_path(archive), "wb");
    if (!archive_fp) {
        fprintf(stderr, "Error: Cannot open archive for writing\n");
        zlite_free_file_list(file_list, file_count);
        return ZLITE_ERROR_FILE;
    }
    
    /* Write simple header */
    fwrite(ARCHIVE_MAGIC, 1, 6, archive_fp);
    
    /* Write file count */
    fwrite(&file_count, sizeof(uint32_t), 1, archive_fp);
    
    /* Process each file */
    for (i = 0; i < file_count; i++) {
        ZliteFileInfo *info = &file_list[i];
        char temp_path[PATH_MAX];
        uint64_t compressed_size = 0;
        uint32_t path_len;
        uint32_t crc = 0;
        
        /* Handle hard link references */
        if (info->is_hardlink && info->link_target) {
            /* This is a reference to another file in the archive */
            int hardlink_type = ZLITE_FILETYPE_HARDLINK;
            path_len = strlen(info->path);
            fwrite(&path_len, sizeof(uint32_t), 1, archive_fp);
            fwrite(info->path, 1, path_len, archive_fp);
            fwrite(&hardlink_type, sizeof(int), 1, archive_fp);
            fwrite(&info->size, sizeof(uint64_t), 1, archive_fp);
            fwrite(&compressed_size, sizeof(uint64_t), 1, archive_fp);
            fwrite(&crc, sizeof(uint32_t), 1, archive_fp);

            /* Store reference path */
            uint32_t target_len = strlen(info->link_target);
            fwrite(&target_len, sizeof(uint32_t), 1, archive_fp);
            fwrite(info->link_target, 1, target_len, archive_fp);

            printf("  %s [hardlink -> %s]\n", info->path, info->link_target);
            continue;
        }
        
        /* Skip directories for now */
        if (info->file_type == ZLITE_FILETYPE_DIR) {
            path_len = strlen(info->path);
            fwrite(&path_len, sizeof(uint32_t), 1, archive_fp);
            fwrite(info->path, 1, path_len, archive_fp);
            fwrite(&info->file_type, sizeof(int), 1, archive_fp);
            fwrite(&info->size, sizeof(uint64_t), 1, archive_fp);
            fwrite(&compressed_size, sizeof(uint64_t), 1, archive_fp);
            fwrite(&crc, sizeof(uint32_t), 1, archive_fp);
            
            printf("  %s [dir]\n", info->path);
            continue;
        }
        
        /* Handle symlinks */
        if (info->file_type == ZLITE_FILETYPE_SYMLINK) {
            path_len = strlen(info->path);
            fwrite(&path_len, sizeof(uint32_t), 1, archive_fp);
            fwrite(info->path, 1, path_len, archive_fp);
            fwrite(&info->file_type, sizeof(int), 1, archive_fp);
            fwrite(&info->size, sizeof(uint64_t), 1, archive_fp);
            fwrite(&compressed_size, sizeof(uint64_t), 1, archive_fp);
            fwrite(&crc, sizeof(uint32_t), 1, archive_fp);
            
            if (info->link_target) {
                uint32_t target_len = strlen(info->link_target);
                fwrite(&target_len, sizeof(uint32_t), 1, archive_fp);
                fwrite(info->link_target, 1, target_len, archive_fp);
            }
            
            printf("  %s [symlink -> %s]\n", info->path, info->link_target ? info->link_target : "NULL");
            continue;
        }
        
        /* Compress regular files */
        snprintf(temp_path, sizeof(temp_path), "%s.tmp%06d",
                 zlite_archive_get_path(archive), i);

        result = compress_file_lzma2(info->path, temp_path,
                                     options->level, &compressed_size);
        
        if (result == ZLITE_OK) {
            /* Read compressed data and write to archive */
            FILE *compressed_fp = fopen(temp_path, "rb");
            if (compressed_fp) {
                Byte *buffer = malloc(compressed_size);
                if (buffer) {
                    size_t read_size = fread(buffer, 1, compressed_size, compressed_fp);
                    
                    /* Calculate CRC */
                    crc = CrcCalc(buffer, read_size);
                    
                    /* Write file info */
                    path_len = strlen(info->path);
                    DEBUG_PRINT("DEBUG: Writing file info: path=%s, size=%llu, compressed_size=%llu\n",
                               info->path, (unsigned long long)info->size, (unsigned long long)compressed_size);
                    fwrite(&path_len, sizeof(uint32_t), 1, archive_fp);
                    fwrite(info->path, 1, path_len, archive_fp);
                    fwrite(&info->file_type, sizeof(int), 1, archive_fp);
                    fwrite(&info->size, sizeof(uint64_t), 1, archive_fp);
                    fwrite(&compressed_size, sizeof(uint64_t), 1, archive_fp);
                    fwrite(&crc, sizeof(uint32_t), 1, archive_fp);
                    
                    /* Write compressed data */
                    fwrite(buffer, 1, read_size, archive_fp);
                    
                    free(buffer);
                    
                    printf("  %s (%llu -> %llu bytes, %.1f%%)\n", 
                           info->path, 
                           (unsigned long long)info->size,
                           (unsigned long long)compressed_size,
                           info->size > 0 ? (compressed_size * 100.0 / info->size) : 0.0);
                    
                    total_files++;
                    total_size += info->size;
                }
                fclose(compressed_fp);
            }
            remove(temp_path);
        } else {
            fprintf(stderr, "Error compressing '%s'\n", info->path);
        }
    }
    
    fclose(archive_fp);
    zlite_free_file_list(file_list, file_count);
    
    printf("\nCompressed %d files (%llu bytes)\n", total_files, 
           (unsigned long long)total_size);
    
    return ZLITE_OK;
}