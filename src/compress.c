#include "../include/7zlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#include "7z.h"
#include "7zAlloc.h"
#include "7zFile.h"
#include "7zCrc.h"
#include "Lzma2Enc.h"
#include "LzmaEnc.h"

#define LZMA_PROPS_SIZE 5

static ISzAlloc g_Alloc = { SzAlloc, SzFree };

static size_t CFileOutStream_Write(const ISeqOutStream *pp, const void *data, size_t size) {
    CFileOutStream *p = (CFileOutStream *)pp;
    size_t written;
    WRes res = File_Write(&p->file, data, &written);
    if (res != 0) {
        return 0;
    }
    return written;
}

static int compress_file_lzma2(const char *input_path, const char *output_path,
                               int level, uint64_t *compressed_size) {
    CLzma2EncHandle enc;
    CFileSeqInStream inStream;
    CFileOutStream outStream;
    Byte props[LZMA_PROPS_SIZE];
    size_t propsSize = LZMA_PROPS_SIZE;
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
    wres = OutFile_Open(&outStream.file, input_path);
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
                props2.lzmaProps.level = 9;
                props2.lzmaProps.dictSize = 1 << 27;
                break;
            case 9:
                props2.lzmaProps.level = 9;
                props2.lzmaProps.dictSize = 1 << 27;
                props2.lzmaProps.numThreads = 2;
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
    
    /* Get encoder properties */
    {
        Byte prop = Lzma2Enc_WriteProperties(enc);
        props[0] = prop;
        propsSize = 1;
    }
    
    /* Write properties to output */
    {
        size_t written = propsSize;
        wres = File_Write(&outStream.file, props, &written);
        if (wres != 0 || written != propsSize) {
            Lzma2Enc_Destroy(enc);
            File_Close(&inStream.file);
            File_Close(&outStream.file);
            return ZLITE_ERROR_WRITE;
        }
    }
    
    /* Encode */
    res = Lzma2Enc_Encode2(enc, &outStream.vt, NULL, 0, &inStream.vt, NULL, 0, NULL);
    
    /* Get compressed size */
    File_GetLength(&outStream.file, compressed_size);
    *compressed_size -= propsSize;
    
    Lzma2Enc_Destroy(enc);
    File_Close(&inStream.file);
    File_Close(&outStream.file);
    
    return (res == SZ_OK) ? ZLITE_OK : ZLITE_ERROR_CORRUPT;
}

int zlite_add_files(ZliteArchive *archive, char **files, int num_files,
                    const ZliteCompressOptions *options) {
    ZliteFileInfo *file_list;
    int file_count;
    int i;
    int result;
    
    /* Collect files */
    result = zlite_collect_files(files, num_files, &file_list, &file_count);
    if (result != ZLITE_OK) {
        return result;
    }
    
    /* Initialize CRC table */
    CrcGenerateTable();
    
    /* Write archive header */
    /* TODO: Implement proper 7z archive format */
    
    /* Process each file */
    for (i = 0; i < file_count; i++) {
        ZliteFileInfo *info = &file_list[i];
        
        /* Skip hard link duplicates */
        if (info->is_hardlink && info->link_target == NULL) {
            /* This is the first occurrence of a hard link, process it */
        } else if (info->is_hardlink && info->link_target != NULL) {
            /* This is a duplicate hard link, skip storing data */
            continue;
        }
        
        /* Handle symlinks specially */
        if (info->file_type == ZLITE_FILETYPE_SYMLINK) {
            /* Write symlink info */
            /* TODO: Implement */
            continue;
        }
        
        /* Handle directories specially */
        if (info->file_type == ZLITE_FILETYPE_DIR) {
            /* Write directory info */
            /* TODO: Implement */
            continue;
        }
        
        /* Compress regular files */
        if (info->file_type == ZLITE_FILETYPE_REGULAR || 
            (info->is_hardlink && info->link_target == NULL)) {
            uint64_t compressed_size;
            char output_path[PATH_MAX];
            snprintf(output_path, sizeof(output_path), "%s.tmp", zlite_archive_get_path(archive));
            result = compress_file_lzma2(info->path, output_path, 
                                         options->level, &compressed_size);
            if (result != ZLITE_OK) {
                fprintf(stderr, "Error compressing '%s'\n", info->path);
                zlite_free_file_list(file_list, file_count);
                return result;
            }
            /* TODO: Append compressed data to archive */
            remove(output_path);
        }
    }
    
    zlite_free_file_list(file_list, file_count);
    
    printf("Added %d files to archive\n", file_count);
    
    return ZLITE_OK;
}
