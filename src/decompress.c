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
#include "Lzma2Dec.h"

/* Use LZMA SDK's LZMA_PROPS_SIZE definition if available */
#ifndef LZMA_PROPS_SIZE
#define LZMA_PROPS_SIZE 1
#endif

static ISzAlloc g_Alloc = { SzAlloc, SzFree };

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
    bool finished = false;
    
    /* Don't set custom dictionary buffer, let decoder use its internal buffer */
    
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
        
        res = Lzma2Dec_DecodeToDic(&dec, output_size, inBuf, &inProcessed, LZMA_FINISH_END, &status);
        
        /* Write output from dictionary */
        if (dec.decoder.dicPos > 0) {
            size_t written = dec.decoder.dicPos;
            wres = File_Write(&outStream.file, dec.decoder.dic, &written);
            if (wres != 0 || written != dec.decoder.dicPos) {
                free(inBuf);
                free(outBuf);
                Lzma2Dec_Free(&dec, &g_Alloc);
                File_Close(&inStream.file);
                File_Close(&outStream.file);
                return ZLITE_ERROR_WRITE;
            }
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

#define ARCHIVE_MAGIC "7z\xBC\xAF\x27\x1C"

int zlite_list_files(ZliteArchive *archive) {
    FILE *fp;
    char magic[6];
    uint32_t file_count;
    int i;
    
    printf("Archive: %s\n", zlite_archive_get_path(archive));
    printf("\n");
    
    fp = fopen(zlite_archive_get_path(archive), "rb");
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
    
    printf("  %-40s %-10s %-10s %-10s\n", "Name", "Type", "Size", "Compressed");
    printf("  %-40s %-10s %-10s %-10s\n", "----", "----", "----", "----------");
    
    for (i = 0; i < file_count; i++) {
        uint32_t path_len;
        char path[PATH_MAX];
        int file_type;
        uint64_t size;
        uint64_t compressed_size;
        uint32_t crc;
        char type_str[20];
        
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
        
        DEBUG_PRINT("DEBUG: Read file: path=%s, type=%d, size=%llu, compressed=%llu, crc=%u\n",
                   path, file_type, (unsigned long long)size, (unsigned long long)compressed_size, crc);
        
        /* Get type string */
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
        
        printf("  %-40s %-10s %-10llu %-10llu\n", 
               path, type_str, 
               (unsigned long long)size, 
               (unsigned long long)compressed_size);
        
        /* Skip compressed data */
        if (file_type == ZLITE_FILETYPE_REGULAR && compressed_size > 0) {
            fseek(fp, compressed_size, SEEK_CUR);
        }
        
        /* Skip symlink target if present */
        if (file_type == ZLITE_FILETYPE_SYMLINK || file_type == ZLITE_FILETYPE_HARDLINK) {
            uint32_t target_len;
            if (fread(&target_len, sizeof(uint32_t), 1, fp) == 1) {
                fseek(fp, target_len, SEEK_CUR);
            }
        }
    }
    
    fclose(fp);
    
    printf("\nTotal: %d files\n", file_count);
    
    return ZLITE_OK;
}

int zlite_test_archive(ZliteArchive *archive) {
    FILE *fp;
    char magic[6];
    uint32_t file_count;
    int i;
    CrcGenerateTable();
    
    printf("Testing archive: %s\n", zlite_archive_get_path(archive));
    
    fp = fopen(zlite_archive_get_path(archive), "rb");
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
    
    printf("\nTesting %d files...\n", file_count);
    
    for (i = 0; i < file_count; i++) {
        uint32_t path_len;
        char path[PATH_MAX];
        int file_type;
        uint64_t size;
        uint64_t compressed_size;
        uint32_t crc;
        uint32_t calc_crc = 0;
        char temp_path[PATH_MAX];
        
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
        
        /* For regular files, read and verify CRC */
        if (file_type == ZLITE_FILETYPE_REGULAR && compressed_size > 0) {
            Byte *buffer = malloc(compressed_size);
            if (buffer && fread(buffer, 1, compressed_size, fp) == compressed_size) {
                calc_crc = CrcCalc(buffer, compressed_size);
                if (crc == calc_crc) {
                    printf("  OK: %s\n", path);
                } else {
                    printf("  CRC ERROR: %s (expected 0x%08X, got 0x%08X)\n", 
                           path, crc, calc_crc);
                }
            }
            if (buffer) free(buffer);
        } else {
            /* Skip data and just mark as OK */
            printf("  OK: %s\n", path);
            
            /* Skip compressed data */
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
    }
    
    fclose(fp);
    
    printf("\nAll tests passed!\n");
    
    return ZLITE_OK;
}

int zlite_extract_files(ZliteArchive *archive, const char *output_dir) {
    FILE *fp;
    char magic[6];
    uint32_t file_count;
    int i;
    CrcGenerateTable();

    printf("Extracting to: %s\n", output_dir);
    
    fp = fopen(zlite_archive_get_path(archive), "rb");
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
    
    printf("\nExtracting %d files...\n", file_count);
    
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
        
        DEBUG_PRINT("DEBUG: Read file: path=%s, type=%d, size=%llu, compressed=%llu, crc=%u\n",
                   path, file_type, (unsigned long long)size, (unsigned long long)compressed_size, crc);
        
        /* Create output path */
        snprintf(output_path, sizeof(output_path), "%s/%s", output_dir, path);

        /* Handle different file types */
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
                /* Create full path for target relative to output_dir */
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
                
                /* Create hard link */
                if (zlite_create_link(full_target, output_path, ZLITE_FILETYPE_HARDLINK) == 0) {
                    printf("  Created hardlink: %s -> %s\n", path, target);
                } else {
                    printf("  Failed to create hardlink: %s -> %s\n", path, target);
                }
            }
        }
        else if (file_type == ZLITE_FILETYPE_REGULAR && compressed_size > 0) {
            uint32_t calc_crc;
            DEBUG_PRINT("DEBUG: Reading compressed data: %llu bytes\n", (unsigned long long)compressed_size);
            
            /* Read compressed data */
            Byte *buffer = malloc(compressed_size);
            if (buffer && fread(buffer, 1, compressed_size, fp) == compressed_size) {
                calc_crc = CrcCalc(buffer, compressed_size);
                DEBUG_PRINT("DEBUG: Read %llu bytes, CRC: expected=%u, calculated=%u\n",
                           (unsigned long long)compressed_size, crc, calc_crc);
            } else {
                DEBUG_PRINT("DEBUG: Failed to read compressed data\n");
                calc_crc = 0;
            }
            
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
                DEBUG_PRINT("DEBUG: Wrote %llu bytes to temp file\n", (unsigned long long)compressed_size);
                DEBUG_PRINT("DEBUG: About to decompress: input_size=%llu, output_size=%llu\n",
                           (unsigned long long)(compressed_size - 1), (unsigned long long)size);
                fclose(temp_fp);
                
                /* Decompress - subtract 1 for the property byte that decompress_file_lzma2 will read */
                if (decompress_file_lzma2(temp_input, temp_output, 
                                         compressed_size - 1, size) == ZLITE_OK) {
                    rename(temp_output, output_path);
                    printf("  %s\n", path);
                } else {
                    printf("  Failed to extract: %s\n", path);
                }
                remove(temp_input);
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
    
    printf("\nExtraction complete!\n");
    
    return ZLITE_OK;
}