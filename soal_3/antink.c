#define _DEFAULT_SOURCE
#define FUSE_USE_VERSION 28

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

char *getpath(const char *path);
char *reverse_name(const char *name);
char *strlwr(char *str);
void logger(const char *message);

static int xmp_getattr(const char *path, struct stat *stbuf);
static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

#define BASE_FOLDER "/it24_host"
#define LOG_FILE "/var/log/it24.log"

static struct fuse_operations xmp_oper = {
    .getattr = xmp_getattr,
    .readdir = xmp_readdir,
    .read = xmp_read,
};

int main(int argc, char *argv[]) {
    umask(0);
    return fuse_main(argc, argv, &xmp_oper, NULL);
}

char *getpath(const char *path) {
    char *fpath = malloc(strlen(BASE_FOLDER) + strlen(path) + 2);
    if (fpath == NULL) return NULL;
    sprintf(fpath, "%s/%s", BASE_FOLDER, path[0] == '/' ? path + 1 : path);
    return fpath;
}

char *reverse_name(const char *name) {
    int len = strlen(name);
    char *reversed_name = malloc(len + 1);
    if (reversed_name == NULL) return NULL;
    for (int i = 0; i < len; i++) {
        reversed_name[i] = name[len - i - 1];
    }
    reversed_name[len] = '\0';
    return reversed_name;
}

char *strlwr(char *str) {
    char *original = str;
    while (*str) {
        *str = tolower((unsigned char)*str);
        str++;
    }
    return original;
}

void logger(const char *message) {
    FILE *fp = fopen(LOG_FILE, "a");
    if (fp == NULL) return;
    char time_str[32];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    fprintf(fp, "[%s] %s\n", time_str, message);
    fclose(fp);
}

static int xmp_getattr(const char *path, struct stat *stbuf) {
    char *fpath = getpath(path);
    int res;
    res = lstat(fpath, stbuf);
    free(fpath);

    if (res == -1) {
        char *last_slash = strrchr(path, '/');
        if (last_slash != NULL) {
            char *new_path = malloc(strlen(path) + 1);
            if (new_path == NULL) return -ENOMEM;
            strncpy(new_path, path, last_slash - path);
            new_path[last_slash - path] = '\0';
            strcat(new_path, "/");
            char *reversed_name = reverse_name(last_slash + 1);
            if (reversed_name == NULL) {
                free(new_path);
                return -ENOMEM;
            }
            strcat(new_path, reversed_name);
            free(reversed_name);

            char *f_reversed_path = getpath(new_path);
            res = lstat(f_reversed_path, stbuf);
            free(new_path);
            free(f_reversed_path);
        }
    }
    if (res == -1) return -errno;
    return 0;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    char *fpath = getpath(path);
    if (fpath == NULL) return -ENOMEM;

    DIR *dp = opendir(fpath);
    free(fpath);
    if (dp == NULL) return -errno;

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (de->d_type == DT_DIR) continue;

        char *lowered_name = strlwr(strdup(de->d_name));
        if (strstr(lowered_name, "kimcun") || strstr(lowered_name, "nafis")) {
            char *reversed_name = reverse_name(de->d_name);
            if (reversed_name == NULL) {
                free(lowered_name);
                closedir(dp);
                return -ENOMEM;
            }

            filler(buf, reversed_name, NULL, 0);

            char message[2048];
            if (strstr(lowered_name, "kimcun")) {
                snprintf(message, sizeof(message), "[REVERSE] File %s has been reversed: %s", de->d_name, reversed_name);
                logger(message);
            } else {
                snprintf(message, sizeof(message), "[REVERSE] File %s has been reversed: %s", de->d_name, reversed_name);
                logger(message);
            }

            free(reversed_name);
            free(lowered_name);
        } else {
            filler(buf, de->d_name, NULL, 0);
        }
    }

    closedir(dp);
    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    int should_rot = 1;
    char target[PATH_MAX];

    char *last_slash = strrchr(path, '/');
    if (last_slash != NULL) {
        char *reversed_name = reverse_name(last_slash + 1);
        char *reversed_copy = strdup(reversed_name);
        strlwr(reversed_copy);

        if (strstr(reversed_copy, "kimcun") || strstr(reversed_copy, "nafis")) {
            char message[2048];
            if (strstr(reversed_copy, "kimcun")) {
                snprintf(message, sizeof(message), "[ALERT] Anomaly detected kimcun in file: %s", path);
                logger(message);
            } else {
                snprintf(message, sizeof(message), "[ALERT] Anomaly detected nafis in file: %s", path);
                logger(message);
            }

            should_rot = 0;
            size_t len = last_slash - path;
            strncpy(target, path, len);
            target[len] = '\0';
            strcat(target, "/");
            strcat(target, reversed_name);
        } else {
            strcpy(target, path);
        }

        free(reversed_name);
        free(reversed_copy);
    } else {
        strcpy(target, path);
    }

    char *fpath = getpath(target);
    if (fpath == NULL) return -ENOMEM;

    FILE *file = fopen(fpath, "rb");
    if (!file) {
        free(fpath);
        return -errno;
    }
    free(fpath);

    fseek(file, offset, SEEK_SET);
    size_t bytes_read = fread(buf, 1, size, file);
    fclose(file);

    if (should_rot) {
        char message[1024];
        snprintf(message, sizeof(message), "[ENCRYPT] File %s has been encrypted", path);
        logger(message);

        for (size_t i = 0; i < bytes_read; i++) {
            if (buf[i] >= 'a' && buf[i] <= 'z') {
                buf[i] = (buf[i] - 'a' + 13) % 26 + 'a';
            } else if (buf[i] >= 'A' && buf[i] <= 'Z') {
                buf[i] = (buf[i] - 'A' + 13) % 26 + 'A';
            }
        }
    }

    return bytes_read;
}
