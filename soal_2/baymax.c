#define _DEFAULT_SOURCE
#define FUSE_USE_VERSION 28

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *fullpath(const char *path, const char *base);

static int xmp_getattr(const char *path, struct stat *stbuf);
static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi);
static int xmp_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int xmp_release(const char *path, struct fuse_file_info *fi);
static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int xmp_unlink(const char *path);

static struct fuse_operations xmp_oper = {
    .getattr = xmp_getattr,
    .readdir = xmp_readdir,
    .create = xmp_create,
    .write = xmp_write,
    .release = xmp_release,
    .read = xmp_read,
    .unlink = xmp_unlink,
};

#define BASE_FOLDER "relics"

int main(int argc, char *argv[]) {
    umask(0);

    return fuse_main(argc, argv, &xmp_oper, NULL);
    return 0;
}

char *fullpath(const char *path, const char *base) {
    char *cwd = getcwd(NULL, 0);
    if (cwd == NULL) return NULL;

    char *full_path = malloc(strlen(cwd) + strlen(base) + strlen(path) + 3);
    if (full_path == NULL) {
        free(cwd);
        return NULL;
    }

    sprintf(full_path, "%s/%s/%s", cwd, base, path[0] == '/' ? path + 1 : path);
    free(cwd);

    return full_path;
}

static int xmp_getattr(const char *path, struct stat *stbuf) {
    if (strcmp(path, "/") == 0) {
        char *fpath = fullpath(path, BASE_FOLDER);
        int res = lstat(fpath, stbuf);
        free(fpath);
        if (res == -1) return -errno;
        return 0;
    }

    DIR *dp = opendir(fullpath("/", BASE_FOLDER));
    if (dp == NULL) return -errno;

    struct dirent *de;
    off_t total_size = 0;
    struct stat st;
    int found = 0;

    while ((de = readdir(dp)) != NULL) {
        if (strncmp(de->d_name, path + 1, strlen(path) - 1) == 0) {
            char *dot = strrchr(de->d_name, '.');
            if (dot && strlen(dot + 1) == 3) {
                char *fpath = fullpath(de->d_name, BASE_FOLDER);
                if (stat(fpath, &st) == 0) {
                    total_size += st.st_size;
                    if (!found) {
                        stbuf->st_mode = st.st_mode;
                        stbuf->st_uid = st.st_uid;
                        stbuf->st_gid = st.st_gid;
                        stbuf->st_atime = st.st_atime;
                        stbuf->st_mtime = st.st_mtime;
                        found = 1;
                    }
                }
                free(fpath);
            }
        }
    }

    closedir(dp);
    if (!found) return -ENOENT;
    stbuf->st_size = total_size;
    stbuf->st_nlink = 1;

    return 0;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    char *full_path = fullpath(path, BASE_FOLDER);

    DIR *dp = opendir(full_path);
    if (dp == NULL) return -errno;

    struct dirent *de;
    char last_base[NAME_MAX] = {0};

    while ((de = readdir(dp)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (de->d_type == DT_DIR) continue;

        char temp[NAME_MAX];
        strncpy(temp, de->d_name, NAME_MAX);
        temp[NAME_MAX - 1] = '\0';

        char *last_dot = strrchr(temp, '.');
        if (last_dot && strlen(last_dot + 1) == 3 &&
            isdigit(last_dot[1]) && isdigit(last_dot[2]) && isdigit(last_dot[3])) {
            *last_dot = '\0';
        }

        if (strcmp(temp, last_base) == 0) continue;
        strcpy(last_base, temp);

        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;

        if (filler(buf, temp, &st, 0)) break;
    }

    closedir(dp);
    return 0;
}

static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi) {
    char *fpath = fullpath(path, BASE_FOLDER);

    struct stat st;
    if (stat(fpath, &st) == 0) {
        free(fpath);
        return -EEXIST;
    }

    FILE *file = fopen(fpath, "wb");
    if (file == NULL) {
        free(fpath);
        return -errno;
    }
    fclose(file);
    free(fpath);

    return 0;
}

static int xmp_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    const char *filename = strrchr(path, '/');
    if (filename == NULL)
        filename = path;
    else
        filename++;

    size_t bytes_written = 0;
    size_t current_offset = offset;
    int fragment_number = current_offset / 1024;

    while (bytes_written < size) {
        size_t fragment_offset = current_offset % 1024;
        size_t to_write = 1024 - fragment_offset;
        if (to_write > size - bytes_written) {
            to_write = size - bytes_written;
        }

        char fragment_name[NAME_MAX];
        snprintf(fragment_name, sizeof(fragment_name), "%s.%03d", filename, fragment_number);
        char *fpath = fullpath(fragment_name, BASE_FOLDER);

        FILE *fragment = fopen(fpath, "r+b");
        if (!fragment) {
            fragment = fopen(fpath, "w+b");
            if (!fragment) {
                free(fpath);
                return -errno;
            }
        }

        fseek(fragment, fragment_offset, SEEK_SET);
        fwrite(buf + bytes_written, 1, to_write, fragment);
        fclose(fragment);
        free(fpath);

        bytes_written += to_write;
        current_offset += to_write;
        fragment_number++;
    }

    return bytes_written;
}

static int xmp_release(const char *path, struct fuse_file_info *fi) {
    char *fpath = fullpath(path, BASE_FOLDER);
    if (remove(fpath) == -1) {
        free(fpath);
        return -errno;
    }
    free(fpath);
    return 0;
}

static int compare_fragments(const void *a, const void *b) {
    return strcmp(*(const char **)a, *(const char **)b);
}
static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    DIR *dp = opendir(fullpath("/", BASE_FOLDER));
    if (dp == NULL) return -errno;

    struct dirent *de;
    off_t total_offset = 0;
    int bytes_read = 0;

    char *fragments[100];
    int fragment_count = 0;

    while ((de = readdir(dp)) != NULL) {
        char *dot = strrchr(de->d_name, '.');
        if (!dot || strlen(dot + 1) != 3) continue;

        if (strncmp(de->d_name, path + 1, strlen(path) - 1) == 0) {
            fragments[fragment_count] = strdup(de->d_name);
            fragment_count++;
        }
    }
    closedir(dp);

    if (fragment_count == 0) return -ENOENT;

    qsort(fragments, fragment_count, sizeof(char *), compare_fragments);

    for (int i = 0; i < fragment_count; i++) {
        char *fragment_path = fullpath(fragments[i], BASE_FOLDER);

        struct stat st;
        if (stat(fragment_path, &st) == -1) {
            for (int j = 0; j < fragment_count; j++) free(fragments[j]);
            return -errno;
        }

        FILE *fragment_file = fopen(fragment_path, "rb");
        if (!fragment_file) {
            for (int j = 0; j < fragment_count; j++) {
                free(fragments[j]);
            }
            return -errno;
        }
        free(fragment_path);

        if (total_offset + st.st_size <= offset) {
            total_offset += st.st_size;
            fclose(fragment_file);
            continue;
        }

        if (offset > total_offset) fseek(fragment_file, offset - total_offset, SEEK_SET);

        size_t to_read = size - bytes_read;
        size_t read_bytes = fread(buf + bytes_read, 1, to_read, fragment_file);
        bytes_read += read_bytes;

        total_offset += read_bytes;

        fclose(fragment_file);

        if (bytes_read >= size) break;
    }

    for (int i = 0; i < fragment_count; i++) free(fragments[i]);

    if (bytes_read == 0) return -EIO;

    return bytes_read;
}

static int xmp_unlink(const char *path) {
    const char *filename = strrchr(path, '/');
    if (!filename)
        filename = path;
    else
        filename++;

    DIR *dp = opendir(fullpath("/", BASE_FOLDER));
    if (!dp) return -errno;

    struct dirent *de;
    int deleted_any = 0;

    while ((de = readdir(dp)) != NULL) {
        if (strncmp(de->d_name, filename, strlen(filename)) != 0) continue;

        const char *after = de->d_name + strlen(filename);
        if (after[0] != '.' || strlen(after + 1) != 3) continue;

        if (!isdigit(after[1]) || !isdigit(after[2]) || !isdigit(after[3])) continue;

        char *fpath = fullpath(de->d_name, BASE_FOLDER);
        if (unlink(fpath) == -1) {
            free(fpath);
            closedir(dp);
            return -errno;
        }

        deleted_any = 1;
        free(fpath);
    }

    closedir(dp);
    if (!deleted_any) return -ENOENT;
    return 0;
}