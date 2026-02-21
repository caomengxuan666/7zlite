#ifndef COMPAT_H
#define COMPAT_H

#ifdef _WIN32
    #define PATH_SEPARATOR '\\'
    #define PATH_SEPARATOR_STR "\\"
    #define ZLITE_USE_WINDOWS_API 1
#else
    #define PATH_SEPARATOR '/'
    #define PATH_SEPARATOR_STR "/"
    #define ZLITE_USE_POSIX 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Thread support */
#ifdef _WIN32
    #include <windows.h>
    typedef HANDLE zlite_thread_t;
    typedef DWORD zlite_thread_id_t;
    #define ZLITE_THREAD_CALL WINAPI
#else
    #include <pthread.h>
    typedef pthread_t zlite_thread_t;
    typedef pthread_t zlite_thread_id_t;
    #define ZLITE_THREAD_CALL
#endif

/* Directory operations */
#ifdef _WIN32
    #define zlite_mkdir(path) _mkdir(path)
    typedef struct _finddata_t zlite_dirent_t;
#else
    #define zlite_mkdir(path) mkdir(path, 0755)
    typedef struct {
        char d_name[256];
        int d_type;
    } zlite_dirent_t;
#endif

/* File operations */
#ifdef _WIN32
    typedef HANDLE zlite_file_t;
    #define ZLITE_INVALID_FILE INVALID_HANDLE_VALUE
#else
    typedef int zlite_file_t;
    #define ZLITE_INVALID_FILE (-1)
#endif

#ifdef __cplusplus
}
#endif

#endif /* COMPAT_H */
