#define _DEFAULT_SOURCE
#define FUSE_USE_VERSION 28

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <zip.h>

char *fullpath(const char *path, const char *base);
void write_log(const char *filename_txt, const char *filename_img);
void extract_zip(const char *zip_path);
void convert_file(char *folder, char *target);
unsigned char hex_char_to_val(char c);
unsigned char *hex_to_bytes(const char *hex, size_t *out_len);

static void *xmp_init(struct fuse_conn_info *conn);
static int xmp_getattr(const char *path, struct stat *stbuf);
static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

struct zip *zip_archive;

static struct fuse_operations xmp_oper = {
    .init = xmp_init,
    .getattr = xmp_getattr,
    .readdir = xmp_readdir,
    .read = xmp_read,
};

#define BASE_FOLDER "anomali"

int main(int argc, char *argv[]) {
    umask(0);
    return fuse_main(argc, argv, &xmp_oper, NULL);
}

void write_log(const char *filename_txt, const char *filename_img) {
    FILE *log = fopen("conversion.log", "a");
    if (!log) return;

    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char date[16];
    char time[16];

    strftime(date, sizeof(date), "%Y-%m-%d", t);
    strftime(time, sizeof(time), "%H:%M:%S", t);

    fprintf(log, "[%s][%s]: Successfully converted hexadecimal text %s to %s.\n",
            date, time, filename_txt, filename_img);

    fclose(log);
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

void extract_zip(const char *zip_path) {
    int err = 0;
    zip_t *zip = zip_open(zip_path, ZIP_RDONLY, &err);
    if (!zip) {
        fprintf(stderr, "Failed to open zip file: %s\n", zip_path);
        return;
    }

    zip_int64_t n_entries = zip_get_num_entries(zip, 0);
    for (zip_uint64_t i = 0; i < n_entries; i++) {
        const char *entry_name = zip_get_name(zip, i, 0);
        if (!entry_name) continue;

        if (entry_name[strlen(entry_name) - 1] == '/') {
            mkdir(entry_name, 0755);
            continue;
        }

        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "mkdir -p \"$(dirname \"%s\")\"", entry_name);
        system(cmd);

        zip_file_t *zf = zip_fopen_index(zip, i, 0);
        if (!zf) continue;

        FILE *fout = fopen(entry_name, "wb");
        if (!fout) {
            zip_fclose(zf);
            continue;
        }

        char buffer[4096];
        zip_int64_t n;
        while ((n = zip_fread(zf, buffer, sizeof(buffer))) > 0) {
            fwrite(buffer, 1, n, fout);
        }

        fclose(fout);
        zip_fclose(zf);
    }

    zip_close(zip);
}

unsigned char hex_char_to_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return 0;
}

unsigned char *hex_to_bytes(const char *hex, size_t *out_len) {
    size_t len = strlen(hex);
    *out_len = len / 2;
    unsigned char *buf = malloc(*out_len);
    for (size_t i = 0; i < *out_len; i++) {
        buf[i] = (hex_char_to_val(hex[2 * i]) << 4) | hex_char_to_val(hex[2 * i + 1]);
    }
    return buf;
}

void convert_file(char *folder, char *target) {
    struct stat st;
    if (stat(target, &st) == -1) {
        mkdir(target, 0755);
    }

    DIR *dir = opendir(folder);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type != DT_REG) continue;

        char *filename = entry->d_name;
        char *ext = strrchr(filename, '.');
        if (!ext || strcmp(ext, ".txt") != 0) continue;

        size_t src_len = strlen(folder) + strlen(filename) + 2;
        char *src_path = malloc(src_len);
        snprintf(src_path, src_len, "%s/%s", folder, filename);

        FILE *src = fopen(src_path, "r");
        if (!src) {
            free(src_path);
            continue;
        }

        fseek(src, 0, SEEK_END);
        long length = ftell(src);
        rewind(src);

        char *hex_buffer = malloc(length + 1);
        fread(hex_buffer, 1, length, src);
        hex_buffer[length] = '\0';
        fclose(src);
        free(src_path);

        size_t binary_len;
        unsigned char *binary = hex_to_bytes(hex_buffer, &binary_len);
        free(hex_buffer);

        time_t now = time(NULL);
        struct tm *t = localtime(&now);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H:%M:%S", t);

        size_t base_len = ext - filename;
        char *base_name = malloc(base_len + 1);
        strncpy(base_name, filename, base_len);
        base_name[base_len] = '\0';

        size_t newfile_len = base_len + strlen("_image_") + strlen(timestamp) + strlen(".png") + 1;
        char *new_filename = malloc(newfile_len);
        snprintf(new_filename, newfile_len, "%s_image_%s.png", base_name, timestamp);

        size_t dest_len = strlen(target) + strlen(new_filename) + 2;
        char *dest_path = malloc(dest_len);
        snprintf(dest_path, dest_len, "%s/%s", target, new_filename);

        FILE *dest = fopen(dest_path, "wb");
        if (!dest) {
            perror("Failed to open target file");
            free(base_name);
            free(new_filename);
            free(dest_path);
            free(binary);
            continue;
        }

        fwrite(binary, 1, binary_len, dest);
        fclose(dest);

        write_log(entry->d_name, new_filename);

        free(binary);
        free(base_name);
        free(new_filename);
        free(dest_path);
    }

    closedir(dir);
}

static void *xmp_init(struct fuse_conn_info *conn) {
    const char *zip_path = "anomali.zip";
    extract_zip(zip_path);
    convert_file("anomali", "anomali/image");
    remove(zip_path);
    return NULL;
}

static int xmp_getattr(const char *path, struct stat *stbuf) {
    char *fpath = fullpath(path, BASE_FOLDER);
    if (lstat(fpath, stbuf) == -1) {
        free(fpath);
        return -errno;
    }
    return 0;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    char *full_path = fullpath(path, BASE_FOLDER);
    DIR *dp = opendir(full_path);
    if (dp == NULL) {
        free(full_path);
        return -errno;
    }

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (filler(buf, de->d_name, NULL, 0)) break;
    }
    closedir(dp);
    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    char *fpath = fullpath(path, BASE_FOLDER);
    FILE *file = fopen(fpath, "rb");
    if (!file) {
        free(fpath);
        return -errno;
    }
    fseek(file, offset, SEEK_SET);
    size_t bytes_read = fread(buf, 1, size, file);
    fclose(file);
    free(fpath);
    if (bytes_read == 0) return -EIO;
    if (bytes_read < size) return bytes_read;

    return bytes_read;
}