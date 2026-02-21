#include "../include/7zlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "7z.h"
#include "7zAlloc.h"
#include "7zFile.h"
#include "7zCrc.h"
#include "Lzma2Dec.h"

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

static int decompress_file_lzma2(FILE *input_file, const char *output_path,
                                  uint64_t input_size, uint64_t output_size) {
    CFileInStream inStream;
    CFileOutStream outStream;
    CLzma2Dec dec;
    Byte props[LZMA_PROPS_SIZE];
    size_t propsSize;
    SRes res;
    WRes wres;
    Byte *inBuf;
    Byte *outBuf;
    size_t inLen, outSize;
    ELzmaStatus status;
    size_t total_read;
    
    /* Open output file */
    wres = OutFile_Open(&outStream.file, output_path);
    if (wres != 0) {
        return ZLITE_ERROR_FILE;
    }
    
    /* Setup stream vtables */
    FileOutStream_CreateVTable(&outStream);
    
    /* Read properties */
    propsSize = fread(props, 1, LZMA_PROPS_SIZE, input_file);
    if (propsSize != LZMA_PROPS_SIZE) {
        File_Close(&outStream.file);
        return ZLITE_ERROR_CORRUPT;
    }
    
    /* Initialize decoder */
    Lzma2Dec_Construct(&dec);
    res = Lzma2Dec_Allocate(&dec, props[0], &g_Alloc);
    if (res != SZ_OK) {
        Lzma2Dec_Free(&dec, &g_Alloc);
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
        
        inLen = fread(inBuf, 1, to_read, input_file);
        if (inLen == 0) {
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
    File_Close(&outStream.file);
    
    return (res == SZ_OK) ? ZLITE_OK : ZLITE_ERROR_CORRUPT;
}

int zlite_extract_files(ZliteArchive *archive, const char *output_dir) {
    /* TODO: Implement proper 7z archive parsing and extraction */
    printf("Extracting files to: %s\n", output_dir);
    
    /* Create output directory if needed */
    zlite_mkdir_recursive(output_dir);
    
    return ZLITE_OK;
}

int zlite_list_files(ZliteArchive *archive) {
    /* TODO: Implement archive listing */
    printf("Archive: %s\n", zlite_archive_get_path(archive));
    printf("Listing files...\n");
    
    return ZLITE_OK;
}

int zlite_test_archive(ZliteArchive *archive) {
    /* TODO: Implement archive testing */
    printf("Testing archive: %s\n", zlite_archive_get_path(archive));
    
    return ZLITE_OK;
}
