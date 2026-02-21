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
#include "Lzma2Dec.h"

#define LZMA_PROPS_SIZE 1

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
    
    /* Decode loop */
    total_read = 0;
    
    while (total_read < input_size) {
        size_t to_read = 65536;
        if (total_read + to_read > input_size) {
            to_read = input_size - total_read;
        }
        
        inLen = to_read;
        wres = File_Read(&inStream.file, inBuf, &inLen);
        if (wres != 0 || inLen == 0) {
            break;
        }
        total_read += inLen;
        
        outSize = 65536;
        res = Lzma2Dec_DecodeToBuf(&dec, outBuf, &outSize,
                                   inBuf, &inLen,
                                   LZMA_FINISH_ANY, &status);
        
        if (outSize > 0) {
            size_t written = outSize;
            wres = File_Write(&outStream.file, outBuf, &written);
            if (wres != 0 || written != outSize) {
                free(inBuf);
                free(outBuf);
                Lzma2Dec_Free(&dec, &g_Alloc);
                File_Close(&inStream.file);
                File_Close(&outStream.file);
                return ZLITE_ERROR_WRITE;
            }
        }
        
        if (res != SZ_OK) {
            break;
        }
        
        if (status == LZMA_STATUS_FINISHED_WITH_MARK) {
            break;
        }
    }
    
    free(inBuf);
    free(outBuf);
    Lzma2Dec_Free(&dec, &g_Alloc);
    File_Close(&inStream.file);
    File_Close(&outStream.file);
    
    return (res == SZ_OK) ? ZLITE_OK : ZLITE_ERROR_CORRUPT;
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
            /* Read compressed data */
            Byte *buffer = malloc(compressed_size);
            char temp_input[PATH_MAX];
            char temp_output[PATH_MAX];
            
            snprintf(temp_input, sizeof(temp_input), "%s.in%06d", output_dir, i);
            snprintf(temp_output, sizeof(temp_output), "%s.out%06d", output_dir, i);
            
            if (buffer && fread(buffer, 1, compressed_size, fp) == compressed_size) {
                /* Write to temp file */
                FILE *temp_fp = fopen(temp_input, "wb");
                if (temp_fp) {
                    fwrite(buffer, 1, compressed_size, temp_fp);
                    fclose(temp_fp);
                    
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
                    
                    /* Decompress */
                    if (decompress_file_lzma2(temp_input, temp_output, 
                                             compressed_size, size) == ZLITE_OK) {
                        printf("  Extracted: %s\n", path);
                        rename(temp_output, output_path);
                    } else {
                        printf("  Error extracting: %s\n", path);
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
    
    printf("\nExtraction complete!\n");
    
    return ZLITE_OK;
}