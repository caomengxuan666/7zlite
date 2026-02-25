#include "../include/7zlite.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
#else
    #include <getopt.h>
#endif

#define VERSION "7zLite 1.0.1.3"

static void print_usage(void) {
    printf("7zLite - A lightweight 7z archive tool with link support\n\n");
    printf("Usage: 7zlite <command> [options] <archive> [files...]\n\n");
    printf("Commands:\n");
    printf("  a              Add files to archive\n");
    printf("  x              Extract files with full paths\n");
    printf("  e              Extract files (without directory names)\n");
    printf("  l              List archive contents\n");
    printf("  t              Test archive integrity\n\n");
    printf("Options:\n");
    printf("  -0..-9         Set compression level (0=store, 9=ultra)\n");
    printf("                 Default: 5\n");
    printf("  -m{method}     Set compression method (lzma2, lzma)\n");
    printf("                 Default: lzma2\n");
    printf("  -t{threads}    Set number of threads\n");
    printf("                 Default: auto\n");
    printf("  -v{size}       Set volume size (e.g., 100M, 1G)\n");
    printf("  -h, --help     Show this help message\n");
    printf("  -V, --version  Show version information\n\n");
    printf("Examples:\n");
    printf("  7zlite a archive.7z file1 file2 dir/\n");
    printf("  7zlite x archive.7z -ooutput/\n");
    printf("  7zlite l archive.7z\n");
    printf("  7zlite a -9 archive.7z files/  # Maximum compression\n");
    printf("  7zlite a -m lzma archive.7z file  # Use LZMA method\n");
}

static void print_version(void) {
    printf("%s\n", VERSION);
    printf("Built with LZMA SDK\n");
    printf("Supports: LZMA, LZMA2 compression\n");
    printf("          Hard links and symbolic links\n");
}

typedef struct {
    ZliteCommand command;
    char *archive_path;
    char **files;
    int num_files;
    char *output_dir;
    ZliteCompressOptions compress_opts;
    int show_help;
    int show_version;
} CommandLineArgs;

static int parse_args(int argc, char **argv, CommandLineArgs *args) {
#ifdef _WIN32
    /* Windows: manual parsing */
    int i;
    
    /* Initialize default values */
    memset(args, 0, sizeof(CommandLineArgs));
    args->compress_opts.level = ZLITE_LEVEL_DEFAULT;
    args->compress_opts.method = ZLITE_METHOD_LZMA2;
    args->compress_opts.solid = 1;
    args->compress_opts.num_threads = 0;
    args->compress_opts.volume_size = 0;
    args->command = ZLITE_CMD_ADD;
    
    /* First argument should be the command */
    if (argc < 2) {
        return ZLITE_ERROR_PARAM;
    }
    
    /* Check if it's a command */
    if (strcmp(argv[1], "a") == 0) {
        args->command = ZLITE_CMD_ADD;
    } else if (strcmp(argv[1], "x") == 0) {
        args->command = ZLITE_CMD_EXTRACT;
    } else if (strcmp(argv[1], "e") == 0) {
        args->command = ZLITE_CMD_EXTRACT;
    } else if (strcmp(argv[1], "l") == 0) {
        args->command = ZLITE_CMD_LIST;
    } else if (strcmp(argv[1], "t") == 0) {
        args->command = ZLITE_CMD_TEST;
    } else if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        args->show_help = 1;
        return ZLITE_OK;
    } else if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0) {
        args->show_version = 1;
        return ZLITE_OK;
    } else {
        return ZLITE_ERROR_PARAM;
    }
    
    /* Simple parsing for options and files */
    for (i = 2; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9') {
            /* Compression level: -0 to -9 */
            args->compress_opts.level = argv[i][1] - '0';
        } else if (argv[i][0] == '-' && strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            /* Output directory: -o path */
            args->output_dir = strdup(argv[++i]);
        } else if (argv[i][0] == '-' && argv[i][1] == 'o' && argv[i][2] != '\0') {
            /* Output directory: -opath */
            args->output_dir = strdup(argv[i] + 2);
        } else if (argv[i][0] != '-') {
            /* Archive path or files */
            if (!args->archive_path) {
                args->archive_path = argv[i];
            } else {
                /* For extract command, if there's only one remaining argument and no -o was provided,
                   treat it as output directory */
                if ((args->command == ZLITE_CMD_EXTRACT || args->command == ZLITE_CMD_LIST ||
                     args->command == ZLITE_CMD_TEST) && !args->output_dir && i == argc - 1) {
                    args->output_dir = strdup(argv[i]);
                } else {
                    args->files = &argv[i];
                    args->num_files = argc - i;
                    break;
                }
            }
        }
    }
    
    if (!args->archive_path) {
        fprintf(stderr, "Error: Archive path required\n");
        return ZLITE_ERROR_PARAM;
    }
    
    return ZLITE_OK;
    
#else
    /* Unix/Linux: use getopt_long */
    int opt;
    int long_index = 0;
    
    static struct option long_options[] = {
        {"help",    no_argument,       0, 'h'},
        {"version", no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };
    
    /* Initialize default values */
    memset(args, 0, sizeof(CommandLineArgs));
    args->compress_opts.level = ZLITE_LEVEL_DEFAULT;
    args->compress_opts.method = ZLITE_METHOD_LZMA2;
    args->compress_opts.solid = 1;
    args->compress_opts.num_threads = 0; /* Auto-detect */
    args->compress_opts.volume_size = 0;
    args->command = ZLITE_CMD_ADD;
    
    /* First argument should be the command */
    if (argc < 2) {
        return ZLITE_ERROR_PARAM;
    }
    
    /* Check if it's a command */
    if (strcmp(argv[1], "a") == 0) {
        args->command = ZLITE_CMD_ADD;
    } else if (strcmp(argv[1], "x") == 0) {
        args->command = ZLITE_CMD_EXTRACT;
    } else if (strcmp(argv[1], "e") == 0) {
        args->command = ZLITE_CMD_EXTRACT;
    } else if (strcmp(argv[1], "l") == 0) {
        args->command = ZLITE_CMD_LIST;
    } else if (strcmp(argv[1], "t") == 0) {
        args->command = ZLITE_CMD_TEST;
    } else if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        args->show_help = 1;
        return ZLITE_OK;
    } else if (strcmp(argv[1], "-V") == 0 || strcmp(argv[1], "--version") == 0) {
        args->show_version = 1;
        return ZLITE_OK;
    } else {
        return ZLITE_ERROR_PARAM;
    }
    
    /* Parse options */
    while ((opt = getopt_long(argc - 1, argv + 1, "0123456789m:t:v:ho:V", 
                              long_options, &long_index)) != -1) {
        switch (opt) {
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                args->compress_opts.level = opt - '0';
                break;
            case 'm':
                if (strcmp(optarg, "lzma2") == 0) {
                    args->compress_opts.method = ZLITE_METHOD_LZMA2;
                } else if (strcmp(optarg, "lzma") == 0) {
                    args->compress_opts.method = ZLITE_METHOD_LZMA;
                } else {
                    fprintf(stderr, "Error: Unknown compression method '%s'\n", optarg);
                    return ZLITE_ERROR_PARAM;
                }
                break;
            case 't':
                args->compress_opts.num_threads = atoi(optarg);
                break;
            case 'v':
                /* Parse volume size */
                if (strstr(optarg, "G") || strstr(optarg, "g")) {
                    args->compress_opts.volume_size = (uint64_t)atof(optarg) * 1024 * 1024 * 1024;
                } else if (strstr(optarg, "M") || strstr(optarg, "m")) {
                    args->compress_opts.volume_size = (uint64_t)atof(optarg) * 1024 * 1024;
                } else if (strstr(optarg, "K") || strstr(optarg, "k")) {
                    args->compress_opts.volume_size = (uint64_t)atof(optarg) * 1024;
                } else {
                    args->compress_opts.volume_size = (uint64_t)atoi(optarg);
                }
                break;
            case 'o':
                args->output_dir = strdup(optarg);
                break;
            case 'h':
                args->show_help = 1;
                return ZLITE_OK;
            case 'V':
                args->show_version = 1;
                return ZLITE_OK;
            default:
                return ZLITE_ERROR_PARAM;
        }
    }
    
    /* Get archive path */
    int arg_pos = optind + 1;
    if (arg_pos >= argc) {
        fprintf(stderr, "Error: Archive path required\n");
        return ZLITE_ERROR_PARAM;
    }
    args->archive_path = argv[arg_pos];
    
    /* Get files to add */
    args->files = &argv[arg_pos + 1];
    args->num_files = argc - arg_pos - 1;
    
    return ZLITE_OK;
#endif
}

int zlite_cli_main(int argc, char **argv) {
    CommandLineArgs args;
    ZliteArchive *archive;
    int result;

    result = parse_args(argc, argv, &args);

    if (result != ZLITE_OK) {
        print_usage();
        return 1;
    }

    if (args.show_help) {
        print_usage();
        return 0;
    }

    if (args.show_version) {
        print_version();
        return 0;
    }

    /* Open archive */
    archive = zlite_archive_create(args.archive_path,
                                   args.command == ZLITE_CMD_ADD);
    if (!archive) {
        fprintf(stderr, "Error: Cannot open archive '%s'\n", args.archive_path);
        return 1;
    }

    /* Execute command */
    switch (args.command) {
        case ZLITE_CMD_ADD:
            if (args.num_files == 0) {
                fprintf(stderr, "Error: No files specified for adding\n");
                zlite_archive_close(archive);
                return 1;
            }
            result = zlite_add_files(archive, args.files, args.num_files,
                                    &args.compress_opts);
            break;
        case ZLITE_CMD_EXTRACT:
            result = zlite_extract_files(archive, args.output_dir ? args.output_dir : ".");
            break;
        case ZLITE_CMD_LIST:
            result = zlite_list_files(archive);
            break;
        case ZLITE_CMD_TEST:
            result = zlite_test_archive(archive);
            break;
        default:
            fprintf(stderr, "Error: Unsupported command\n");
            result = ZLITE_ERROR_UNSUPPORTED;
            break;
    }
    
    zlite_archive_close(archive);
    
    if (args.output_dir) {
        free(args.output_dir);
    }
    
    return result;
}
