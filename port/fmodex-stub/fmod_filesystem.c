/* POSIX filesystem backend for the Android FMOD binaries. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    FMOD_OK = 0,
    FMOD_ERR_FILE_BAD = 19,
    FMOD_ERR_FILE_COULDNOTSEEK = 20,
    FMOD_ERR_FILE_EOF = 22,
    FMOD_ERR_FILE_NOTFOUND = 23,
};

struct fmod_file {
    int fd;
    uint64_t position;
    volatile int lock;
};

typedef int (*event_create_fn)(void **);
typedef int (*event_get_system_fn)(void *, void **);
typedef int (*system_set_fs_fn)(void *, void *, void *, void *, void *, void *, void *, int);

static void file_lock(struct fmod_file *file) {
    while (__sync_lock_test_and_set(&file->lock, 1))
        while (file->lock) { }
}

static void file_unlock(struct fmod_file *file) {
    __sync_lock_release(&file->lock);
}

static int fs_open(const char *name, int unicode, unsigned int *size,
                   void **handle, void **userdata) {
    (void)unicode;
    (void)userdata;
    int fd = open(name, O_RDONLY);
    if (fd < 0)
        return FMOD_ERR_FILE_NOTFOUND;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 0 || (uint64_t)st.st_size > UINT32_MAX) {
        close(fd);
        return FMOD_ERR_FILE_BAD;
    }
    struct fmod_file *file = calloc(1, sizeof(*file));
    if (!file) {
        close(fd);
        return FMOD_ERR_FILE_BAD;
    }
    file->fd = fd;
    *size = (unsigned int)st.st_size;
    *handle = file;
    if (userdata)
        *userdata = NULL;
    return FMOD_OK;
}

static int fs_close(void *handle, void *userdata) {
    (void)userdata;
    struct fmod_file *file = handle;
    if (!file)
        return FMOD_ERR_FILE_BAD;
    int result = close(file->fd) == 0 ? FMOD_OK : FMOD_ERR_FILE_BAD;
    free(file);
    return result;
}

static int fs_read(void *handle, void *buffer, unsigned int size,
                   unsigned int *read_size, void *userdata) {
    (void)userdata;
    struct fmod_file *file = handle;
    if (!file) {
        *read_size = 0;
        return FMOD_ERR_FILE_BAD;
    }
    file_lock(file);
    ssize_t read_bytes = pread(file->fd, buffer, size, (off_t)file->position);
    *read_size = read_bytes > 0 ? (unsigned int)read_bytes : 0;
    if (read_bytes > 0)
        file->position += (unsigned int)read_bytes;
    file_unlock(file);
    if (read_bytes == (ssize_t)size)
        return FMOD_OK;
    return read_bytes >= 0 ? FMOD_ERR_FILE_EOF : FMOD_ERR_FILE_BAD;
}

static int fs_seek(void *handle, unsigned int position, void *userdata) {
    (void)userdata;
    struct fmod_file *file = handle;
    if (!file)
        return FMOD_ERR_FILE_COULDNOTSEEK;
    file_lock(file);
    file->position = position;
    file_unlock(file);
    return FMOD_OK;
}

int fmod_eventsystem_create(void **event_system) __asm__("FMOD_EventSystem_Create");
int fmod_eventsystem_create(void **event_system) {
    static event_create_fn real_create;
    static event_get_system_fn get_system;
    static system_set_fs_fn set_filesystem;
    if (!real_create)
        real_create = (event_create_fn)dlsym(RTLD_NEXT, "FMOD_EventSystem_Create");
    int result = real_create ? real_create(event_system) : FMOD_ERR_FILE_BAD;
    if (result != FMOD_OK || !event_system || !*event_system)
        return result;
    if (!get_system)
        get_system = (event_get_system_fn)dlsym(
            RTLD_NEXT, "_ZN4FMOD11EventSystem15getSystemObjectEPPNS_6SystemE");
    if (!set_filesystem)
        set_filesystem = (system_set_fs_fn)dlsym(RTLD_NEXT, "FMOD_System_SetFileSystem");
    void *system = NULL;
    if (!get_system || !set_filesystem ||
        get_system(*event_system, &system) != FMOD_OK || !system)
        return FMOD_ERR_FILE_BAD;
    return set_filesystem(system, fs_open, fs_close, fs_read, fs_seek, NULL, NULL, 0);
}
