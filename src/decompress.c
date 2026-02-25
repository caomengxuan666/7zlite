#include "../include/7zlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Debug macro */
#define DEBUG_DECOMPRESSION 0

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
#include "7zBuf.h"
#include "Lzma2Dec.h"

/* Use LZMA SDK's LZMA_PROPS_SIZE definition if available */
#ifndef LZMA_PROPS_SIZE
#define LZMA_PROPS_SIZE 1
#endif

/* Archive magic - same as standard 7z */
#define ARCHIVE_MAGIC "7z\xBC\xAF\x27\x1C"

static ISzAlloc g_Alloc = { SzAlloc, SzFree };
static ISzAlloc g_AllocTemp = { SzAlloc, SzFree };

/* ========================================================================
 * Custom format decompression (legacy format for hard link optimization)
 * ======================================================================== */

static int decompress_file_lzma2(const char *input_path, const char *output_path,
                                  uint64_t input_size, uint64_t output_size) {
    CFileSeqInStream inStream;
    CFileOutStream outStream;
    CLzma2Dec dec;
    Byte prop;
    SRes res;
    WRes wres;
    Byte *inBuf;
    Byte *outBuf;
    size_t inLen, outSize;
    ELzmaStatus status;
    size_t total_read;

    /* Open input file */
    wres = InFile_Open(&inStream.file, input_path);
    if (wres != 0) {
        return ZLITE_ERROR_FILE;
    }

    /* Open output file */
    wres = OutFile_Open(&outStream.file, output_path);
    if (wres != 0) {
        File_Close(&inStream.file);
        return ZLITE_ERROR_FILE;
    }
    
    /* Setup stream vtables */
    FileSeqInStream_CreateVTable(&inStream);
    FileOutStream_CreateVTable(&outStream);
    
    /* Read properties */
    {
        size_t read_size = 1;
        wres = File_Read(&inStream.file, &prop, &read_size);
        if (wres != 0 || read_size != 1) {
            File_Close(&inStream.file);
            File_Close(&outStream.file);
            return ZLITE_ERROR_CORRUPT;
        }
        DEBUG_PRINT("DEBUG: Read property byte: 0x%02X\n", prop);
    }

    /* Initialize decoder */
    Lzma2Dec_Construct(&dec);
    res = Lzma2Dec_Allocate(&dec, prop, &g_Alloc);
    if (res != SZ_OK) {
        Lzma2Dec_Free(&dec, &g_Alloc);
        File_Close(&inStream.file);
        File_Close(&outStream.file);
        return ZLITE_ERROR_CORRUPT;
    }

    Lzma2Dec_Init(&dec);
    
    /* Allocate buffers */
    inBuf = (Byte *)malloc(65536);
    outBuf = (Byte *)malloc(65536);
    if (!inBuf || !outBuf) {
        if (inBuf) free(inBuf);
        if (outBuf) free(outBuf);
        Lzma2Dec_Free(&dec, &g_Alloc);
        File_Close(&inStream.file);
        File_Close(&outStream.file);
        return ZLITE_ERROR_MEMORY;
    }
    
    /* Decode loop - using DecodeToDic with default dictionary */
    total_read = 0;
    size_t total_written = 0;
    bool finished = false;
    
    while (!finished) {
        /* Read input data */
        size_t srcLen = 65536;
        if (total_read + srcLen > input_size) {
            srcLen = input_size - total_read;
        }
        
        if (srcLen > 0) {
            wres = File_Read(&inStream.file, inBuf, &srcLen);
            if (wres != 0) {
                break;
            }
            total_read += srcLen;
        }
        
        /* Decode to dictionary with limit */
        size_t inProcessed = srcLen;
        size_t dicPosBefore = dec.decoder.dicPos;
        
        res = Lzma2Dec_DecodeToDic(&dec, output_size, inBuf, &inProcessed, LZMA_FINISH_END, &status);
        
        /* Write only newly decoded data */
        if (dec.decoder.dicPos > dicPosBefore) {
            size_t newly_decoded = dec.decoder.dicPos - dicPosBefore;
            wres = File_Write(&outStream.file, dec.decoder.dic + dicPosBefore, &newly_decoded);
            if (wres != 0 || newly_decoded != (dec.decoder.dicPos - dicPosBefore)) {
                free(inBuf);
                free(outBuf);
                Lzma2Dec_Free(&dec, &g_Alloc);
                File_Close(&inStream.file);
                File_Close(&outStream.file);
                return ZLITE_ERROR_WRITE;
            }
            total_written += newly_decoded;
        }
        
        /* Check for completion */
        if (status == LZMA_STATUS_FINISHED_WITH_MARK) {
            finished = true;
            break;
        }
        
        /* Check if all input consumed */
        if (total_read >= input_size) {
            break;
        }
        
        if (res != SZ_OK) {
            break;
        }
    }

    free(inBuf);
    free(outBuf);
    Lzma2Dec_Free(&dec, &g_Alloc);
    File_Close(&inStream.file);
    File_Close(&outStream.file);

    return (res == SZ_OK || finished) ? ZLITE_OK : ZLITE_ERROR_CORRUPT;
}

static int extract_custom_format(const char *archive_path, const char *output_dir, int list_only, int test_only) {
    FILE *fp;
    char magic[6];
    uint32_t file_count;
    int i;
    CrcGenerateTable();
    
    if (list_only) {
        printf("Archive: %s\n", archive_path);
        printf("\n  %-40s %-10s %-10s %-10s\n", "Name", "Type", "Size", "Compressed");
        printf("  %-40s %-10s %-10s %-10s\n", "----", "----", "----", "----------");
    } else if (test_only) {
        printf("Testing archive: %s\n", archive_path);
        printf("\n");
    } else {
        printf("Extracting to: %s\n", output_dir);
        printf("\n");
    }
    
    fp = fopen(archive_path, "rb");
    if (!fp) {
        fprintf(stderr, "Error: Cannot open archive\n");
        return ZLITE_ERROR_FILE;
    }
    
    /* Read and verify magic */
    if (fread(magic, 1, 6, fp) != 6 || memcmp(magic, ARCHIVE_MAGIC, 6) != 0) {
        fprintf(stderr, "Error: Invalid archive format\n");
        fclose(fp);
        return ZLITE_ERROR_CORRUPT;
    }
    
    /* Read file count */
    if (fread(&file_count, sizeof(uint32_t), 1, fp) != 1) {
        fclose(fp);
        return ZLITE_ERROR_CORRUPT;
    }
    
    if (!list_only && !test_only) {
        printf("Extracting %d files...\n", file_count);
    } else if (test_only) {
        printf("Testing %d files...\n", file_count);
    }
    
    for (i = 0; i < file_count; i++) {
        uint32_t path_len;
        char path[PATH_MAX];
        char output_path[PATH_MAX];
        int file_type;
        uint64_t size;
        uint64_t compressed_size;
        uint32_t crc;
        
        /* Read file info */
        if (fread(&path_len, sizeof(uint32_t), 1, fp) != 1 ||
            path_len >= PATH_MAX ||
            fread(path, 1, path_len, fp) != path_len ||
            fread(&file_type, sizeof(int), 1, fp) != 1 ||
            fread(&size, sizeof(uint64_t), 1, fp) != 1 ||
            fread(&compressed_size, sizeof(uint64_t), 1, fp) != 1 ||
            fread(&crc, sizeof(uint32_t), 1, fp) != 1) {
            break;
        }
        path[path_len] = '\0';
        
        /* Get type string for list */
        char type_str[20];
        switch (file_type) {
            case ZLITE_FILETYPE_REGULAR:
                strcpy(type_str, "File");
                break;
            case ZLITE_FILETYPE_DIR:
                strcpy(type_str, "Dir");
                break;
            case ZLITE_FILETYPE_SYMLINK:
                strcpy(type_str, "Symlink");
                break;
            case ZLITE_FILETYPE_HARDLINK:
                strcpy(type_str, "Hardlink");
                break;
            default:
                strcpy(type_str, "Unknown");
                break;
        }
        
        /* List mode */
        if (list_only) {
            printf("  %-40s %-10s %-10llu %-10llu\n", 
                   path, type_str, 
                   (unsigned long long)size, 
                   (unsigned long long)compressed_size);
            
            /* Skip compressed data */
            if (file_type == ZLITE_FILETYPE_REGULAR && compressed_size > 0) {
                fseek(fp, compressed_size, SEEK_CUR);
            }
            
            /* Skip symlink/hardlink target if present */
            if (file_type == ZLITE_FILETYPE_SYMLINK || file_type == ZLITE_FILETYPE_HARDLINK) {
                uint32_t target_len;
                if (fread(&target_len, sizeof(uint32_t), 1, fp) == 1) {
                    fseek(fp, target_len, SEEK_CUR);
                }
            }
            continue;
        }
        
        /* Create output path */
        snprintf(output_path, sizeof(output_path), "%s/%s", output_dir, path);

        /* Test mode */
        if (test_only) {
            if (file_type == ZLITE_FILETYPE_REGULAR && compressed_size > 0) {
                Byte *buffer = malloc(compressed_size);
                if (buffer && fread(buffer, 1, compressed_size, fp) == compressed_size) {
                    uint32_t calc_crc = CrcCalc(buffer, compressed_size);
                    if (crc == calc_crc) {
                        printf("  OK: %s\n", path);
                    } else {
                        printf("  CRC ERROR: %s (expected 0x%08X, got 0x%08X)\n", 
                               path, crc, calc_crc);
                    }
                }
                if (buffer) free(buffer);
            } else {
                printf("  OK: %s\n", path);
                
                /* Skip data */
                if (compressed_size > 0) {
                    fseek(fp, compressed_size, SEEK_CUR);
                }
                
                /* Skip symlink/hardlink target if present */
                if (file_type == ZLITE_FILETYPE_SYMLINK || file_type == ZLITE_FILETYPE_HARDLINK) {
                    uint32_t target_len;
                    if (fread(&target_len, sizeof(uint32_t), 1, fp) == 1) {
                        fseek(fp, target_len, SEEK_CUR);
                    }
                }
            }
            continue;
        }
        
        /* Extract mode */
        if (file_type == ZLITE_FILETYPE_DIR) {
            zlite_mkdir_recursive(output_path);
            printf("  Created directory: %s\n", path);
        } 
        else if (file_type == ZLITE_FILETYPE_SYMLINK) {
            uint32_t target_len;
            char target[PATH_MAX];
            if (fread(&target_len, sizeof(uint32_t), 1, fp) == 1 &&
                target_len < PATH_MAX &&
                fread(target, 1, target_len, fp) == target_len) {
                target[target_len] = '\0';
                zlite_create_link(target, output_path, ZLITE_FILETYPE_SYMLINK);
                printf("  Created symlink: %s -> %s\n", path, target);
            }
        }
        else if (file_type == ZLITE_FILETYPE_HARDLINK) {
            uint32_t target_len;
            char target[PATH_MAX];
            char full_target[PATH_MAX];
            if (fread(&target_len, sizeof(uint32_t), 1, fp) == 1 &&
                target_len < PATH_MAX &&
                fread(target, 1, target_len, fp) == target_len) {
                target[target_len] = '\0';
                snprintf(full_target, sizeof(full_target), "%s/%s", output_dir, target);
                
                /* Create output directory if needed */
                {
                    char dir_copy[PATH_MAX];
                    char *slash;
                    strcpy(dir_copy, output_path);
                    slash = strrchr(dir_copy, '/');
                    if (slash) {
                        *slash = '\0';
                        zlite_mkdir_recursive(dir_copy);
                    }
                }
                
                if (zlite_create_link(full_target, output_path, ZLITE_FILETYPE_HARDLINK) == 0) {
                    printf("  Created hardlink: %s -> %s\n", path, target);
                } else {
                    printf("  Failed to create hardlink: %s -> %s\n", path, target);
                }
            }
        }
        else if (file_type == ZLITE_FILETYPE_REGULAR && compressed_size > 0) {
            /* Read compressed data */
            Byte *buffer = malloc(compressed_size);
            if (buffer && fread(buffer, 1, compressed_size, fp) == compressed_size) {
                uint32_t calc_crc = CrcCalc(buffer, compressed_size);
                
                if (crc != calc_crc) {
                    printf("  CRC mismatch for %s\n", path);
                    free(buffer);
                    continue;
                }
                
                /* Create output directory if needed */
                {
                    char dir_copy[PATH_MAX];
                    char *slash;
                    strcpy(dir_copy, output_path);
                    slash = strrchr(dir_copy, '/');
                    if (slash) {
                        *slash = '\0';
                        zlite_mkdir_recursive(dir_copy);
                    }
                }
                
                /* Create temp files for decompression */
                char temp_input[PATH_MAX];
                char temp_output[PATH_MAX];
                snprintf(temp_input, sizeof(temp_input), "%s.zlite_tmp", output_path);
                snprintf(temp_output, sizeof(temp_output), "%s.zlite_out", output_path);
                
                /* Write compressed data */
                FILE *temp_fp = fopen(temp_input, "wb");
                if (temp_fp) {
                    fwrite(buffer, 1, compressed_size, temp_fp);
                    fclose(temp_fp);
                    
                    if (decompress_file_lzma2(temp_input, temp_output, 
                                             compressed_size - 1, size) == ZLITE_OK) {
                        rename(temp_output, output_path);
                        printf("  %s\n", path);
                    } else {
                        printf("  Failed to extract: %s\n", path);
                    }
                    remove(temp_input);
                }
            }
            if (buffer) free(buffer);
        } else {
            printf("  Skipped: %s\n", path);
            
            /* Skip symlink/hardlink target if present */
            if (file_type == ZLITE_FILETYPE_SYMLINK || file_type == ZLITE_FILETYPE_HARDLINK) {
                uint32_t target_len;
                if (fread(&target_len, sizeof(uint32_t), 1, fp) == 1) {
                    fseek(fp, target_len, SEEK_CUR);
                }
            }
        }
    }
    
    fclose(fp);
    
    if (list_only) {
        printf("\nTotal: %d files\n", file_count);
    } else if (test_only) {
        printf("\nAll tests passed!\n");
    } else {
        printf("\nExtraction complete!\n");
    }
    
    return ZLITE_OK;
}

/* ========================================================================
 * Standard 7z format decompression (using SDK)
 * ======================================================================== */

static void print_error(SRes res) {
    switch (res) {
        case SZ_OK:
            break;
        case SZ_ERROR_FAIL:
            fprintf(stderr, "Error: General error\n");
            break;
        case SZ_ERROR_MEM:
            fprintf(stderr, "Error: Memory allocation failed\n");
            break;
        case SZ_ERROR_CRC:
            fprintf(stderr, "Error: CRC error\n");
            break;
        case SZ_ERROR_UNSUPPORTED:
            fprintf(stderr, "Error: Unsupported archive\n");
            break;
        case SZ_ERROR_PARAM:
            fprintf(stderr, "Error: Invalid parameter\n");
            break;
        case SZ_ERROR_NO_ARCHIVE:
            fprintf(stderr, "Error: No archive\n");
            break;
        case SZ_ERROR_ARCHIVE:
            fprintf(stderr, "Error: Archive error\n");
            break;
        case SZ_ERROR_READ:
            fprintf(stderr, "Error: Read error\n");
            break;
        case SZ_ERROR_WRITE:
            fprintf(stderr, "Error: Write error\n");
            break;
        case SZ_ERROR_DATA:
            fprintf(stderr, "Error: Data error\n");
            break;
        case SZ_ERROR_INPUT_EOF:
            fprintf(stderr, "Error: Unexpected end of input\n");
            break;
        default:
            fprintf(stderr, "Error: Unknown error code %d\n", res);
            break;
    }
}

static int extract_standard_7z(const char *archive_path, const char *output_dir, int list_only, int test_only) {
    CFileInStream archiveStream;
    CLookToRead2 lookStream;
    CSzArEx db;
    SRes res;
    UInt32 i;
    UInt16 *temp = NULL;
    size_t tempSize = 0;
    UInt32 blockIndex = 0xFFFFFFFF;
    Byte *outBuffer = NULL;
    size_t outBufferSize = 0;
    
    #define kInputBufSize ((size_t)1 << 18)
    
    /* Initialize CRC table */
    CrcGenerateTable();
    
    /* Initialize archive database */
    SzArEx_Init(&db);
    
    /* Open archive file */
    archiveStream.wres = InFile_Open(&archiveStream.file, archive_path);
    if (archiveStream.wres != 0) {
        fprintf(stderr, "Error: Cannot open archive file\n");
        return ZLITE_ERROR_FILE;
    }
    
    FileInStream_CreateVTable(&archiveStream);
    
    /* Initialize look stream */
    LookToRead2_CreateVTable(&lookStream, False);
    lookStream.buf = (Byte *)ISzAlloc_Alloc(&g_Alloc, kInputBufSize);
    if (!lookStream.buf) {
        File_Close(&archiveStream.file);
        SzArEx_Free(&db, &g_Alloc);
        fprintf(stderr, "Error: Memory allocation failed\n");
        return ZLITE_ERROR_MEMORY;
    }
    lookStream.bufSize = kInputBufSize;
    lookStream.realStream = &archiveStream.vt;
    LookToRead2_INIT(&lookStream);
    
    /* Open archive */
    res = SzArEx_Open(&db, &lookStream.vt, &g_Alloc, &g_AllocTemp);
    if (res != SZ_OK) {
        ISzAlloc_Free(&g_Alloc, lookStream.buf);
        File_Close(&archiveStream.file);
        SzArEx_Free(&db, &g_Alloc);
        print_error(res);
        return ZLITE_ERROR_CORRUPT;
    }
    
    if (list_only) {
        printf("Archive: %s\n", archive_path);
        printf("\n  %-40s %-10s %-10s\n", "Name", "Type", "Size");
        printf("  %-40s %-10s %-10s\n", "----", "----", "----");
    } else if (test_only) {
        printf("Testing archive: %s\n", archive_path);
        printf("\n");
    } else {
        printf("Extracting to: %s\n", output_dir);
        printf("\n");
    }
    
    /* Process each file */
    for (i = 0; i < db.NumFiles; i++) {
        size_t offset = 0;
        size_t outSizeProcessed = 0;
        const BoolInt isDir = SzArEx_IsDir(&db, i);
        
        /* Skip directories in extract/test mode without full paths */
        if (!list_only && !test_only && isDir) {
            continue;
        }
        
        /* Get filename */
        size_t len = SzArEx_GetFileNameUtf16(&db, i, NULL);
        if (len > tempSize) {
            SzFree(NULL, temp);
            tempSize = len;
            temp = (UInt16 *)SzAlloc(NULL, tempSize * sizeof(temp[0]));
            if (!temp) {
                res = SZ_ERROR_MEM;
                break;
            }
        }
        SzArEx_GetFileNameUtf16(&db, i, temp);
        
        /* Convert UTF-16 to UTF-8 for display */
        char utf8_path[PATH_MAX];
        size_t utf8_len = 0;
        {
            const UInt16 *src = temp;
            const UInt16 *srcEnd = temp + len - 1;  /* -1 for null terminator */
            Byte *dest = (Byte *)utf8_path;
            
            while (src < srcEnd && utf8_len < PATH_MAX - 1) {
                UInt32 val = *src++;
                if (val < 0x80) {
                    *dest++ = (Byte)val;
                    utf8_len++;
                } else if (val < 0x800) {
                    *dest++ = (Byte)(0xC0 | (val >> 6));
                    *dest++ = (Byte)(0x80 | (val & 0x3F));
                    utf8_len += 2;
                } else {
                    *dest++ = (Byte)(0xE0 | (val >> 12));
                    *dest++ = (Byte)(0x80 | ((val >> 6) & 0x3F));
                    *dest++ = (Byte)(0x80 | (val & 0x3F));
                    utf8_len += 3;
                }
            }
            *dest = '\0';
        }
        
        /* List mode */
        if (list_only) {
            UInt64 fileSize = SzArEx_GetFileSize(&db, i);
            char size_str[32];
            {
                UInt64 val = fileSize;
                char *p = size_str + 31;
                *p = '\0';
                do {
                    *--p = '0' + (val % 10);
                    val /= 10;
                } while (val != 0);
            }
            
            const char *type = isDir ? "Dir" : "File";
            printf("  %-40s %-10s %-10s\n", utf8_path, type, size_str);
            continue;
        }
        
        /* Test/Extract mode */
        if (test_only) {
            printf("  Testing: %s\n", utf8_path);
            if (!isDir) {
                res = SzArEx_Extract(&db, &lookStream.vt, i,
                    &blockIndex, &outBuffer, &outBufferSize,
                    &offset, &outSizeProcessed,
                    &g_Alloc, &g_AllocTemp);
                if (res != SZ_OK) {
                    print_error(res);
                    break;
                }
            }
        } else {
            printf("  Extracting: %s\n", utf8_path);
            
            if (!isDir) {
                res = SzArEx_Extract(&db, &lookStream.vt, i,
                    &blockIndex, &outBuffer, &outBufferSize,
                    &offset, &outSizeProcessed,
                    &g_Alloc, &g_AllocTemp);
                if (res != SZ_OK) {
                    print_error(res);
                    break;
                }
                
                /* Write to file */
                {
                    char full_path[PATH_MAX];
                    snprintf(full_path, sizeof(full_path), "%s/%s", output_dir, utf8_path);
                    
                    /* Create directory */
                    {
                        char dir_copy[PATH_MAX];
                        char *slash;
                        strcpy(dir_copy, full_path);
                        slash = strrchr(dir_copy, '/');
                        if (slash) {
                            *slash = '\0';
                            zlite_mkdir_recursive(dir_copy);
                        }
                    }
                    
                    /* Write file */
                    FILE *outFile = fopen(full_path, "wb");
                    if (outFile) {
                        fwrite(outBuffer + offset, 1, outSizeProcessed, outFile);
                        fclose(outFile);
                    } else {
                        fprintf(stderr, "Error: Cannot create file %s\n", full_path);
                    }
                }
            }
        }
    }
    
    /* Cleanup */
    if (outBuffer) {
        ISzAlloc_Free(&g_Alloc, outBuffer);
        outBuffer = NULL;
    }
    if (temp) {
        SzFree(NULL, temp);
    }
    SzArEx_Free(&db, &g_Alloc);
    ISzAlloc_Free(&g_Alloc, lookStream.buf);
    File_Close(&archiveStream.file);
    
    if (res != SZ_OK) {
        print_error(res);
        return ZLITE_ERROR_CORRUPT;
    }
    
    if (list_only) {
        printf("\nTotal: %d files\n", db.NumFiles);
    } else if (test_only) {
        printf("\nAll tests passed!\n");
    } else {
        printf("\nExtraction complete!\n");
    }
    
    return ZLITE_OK;
}

/* ========================================================================
 * Public API - Auto-detect format
 * ======================================================================== */

int zlite_list_files(ZliteArchive *archive) {
    const char *archive_path = zlite_archive_get_path(archive);
    
    /* Try standard 7z format first */
    if (extract_standard_7z(archive_path, NULL, 1, 0) == ZLITE_OK) {
        return ZLITE_OK;
    }
    
    /* Fallback to custom format */
    return extract_custom_format(archive_path, NULL, 1, 0);
}

int zlite_test_archive(ZliteArchive *archive) {
    const char *archive_path = zlite_archive_get_path(archive);
    
    /* Try standard 7z format first */
    if (extract_standard_7z(archive_path, NULL, 0, 1) == ZLITE_OK) {
        return ZLITE_OK;
    }
    
    /* Fallback to custom format */
    return extract_custom_format(archive_path, NULL, 0, 1);
}

int zlite_extract_files(ZliteArchive *archive, const char *output_dir) {
    const char *archive_path = zlite_archive_get_path(archive);
    
    /* Try standard 7z format first */
    if (extract_standard_7z(archive_path, output_dir, 0, 0) == ZLITE_OK) {
        return ZLITE_OK;
    }
    
    /* Fallback to custom format */
    return extract_custom_format(archive_path, output_dir, 0, 0);
}
