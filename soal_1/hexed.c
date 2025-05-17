#define FUSE_USE_VERSION 28
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <zip.h>

void write_log(const char *filename_txt, const char *filename_img);

static void* xmp_init(struct fuse_conn_info *conn);
static int xmp_getattr(const char *path, struct stat *stbuf);
static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
static int xmp_open(const char *path, struct fuse_file_info *fi);
static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int convert_hex(const char *txt_path, char **buffer, size_t *buffer_size);
static int xmp_unlink(const char *path);

struct zip *zip_archive;

static struct fuse_operations xmp_oper = {
    .init = xmp_init,
    .getattr = xmp_getattr,
    .readdir = xmp_readdir,
    .open = xmp_open,
    .read = xmp_read,
    .unlink = xmp_unlink,
};

int main(int argc, char *argv[]) {
    umask(0);
    return fuse_main(argc, argv, &xmp_oper, NULL);
}

static void* xmp_init(struct fuse_conn_info *conn) {
    zip_archive = zip_open("anomali.zip", 0, NULL);
    printf("halo");
    if (!zip_archive) {
        fprintf(stderr, "Failed to open zip\n");
        exit(1);
    }
    return NULL;
}

static int xmp_getattr(const char *path, struct stat *stbuf) {
    memset(stbuf, 0, sizeof(struct stat));
    if (strcmp(path, "/") == 0) {
        stbuf->st_mode = S_IFDIR | 0755;
        stbuf->st_nlink = 2;
        return 0;
    }

    const char *filename = path + 1;  
    struct zip_stat zs;
    if (zip_stat(zip_archive, filename, 0, &zs) == 0) {
        stbuf->st_mode = S_IFREG | 0444;
        stbuf->st_nlink = 1;
        stbuf->st_size = zs.size;
        return 0;
    }
     return -ENOENT;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
  if (strcmp(path, "/") != 0)
        return -ENOENT;

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    zip_int64_t count = zip_get_num_entries(zip_archive, 0);
    for (zip_uint64_t i = 0; i < count; ++i) {
        const char *name = zip_get_name(zip_archive, i, 0);
        filler(buf, name, NULL, 0);
    }
    return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi) {
    const char *filename = path + 1;
    struct zip_stat zs;
    if (zip_stat(zip_archive, filename, 0, &zs) != 0)
    return -ENOENT;

    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    const char *filename = path + 1;

     if (strstr(filename, ".txt")) {
        char *img_buffer;
        size_t img_size;

        if (convert_hex(filename, &img_buffer, &img_size) == 0) {
            struct stat st = {0};
            if (stat("image", &st) == -1) {
                mkdir("image", 0755);
            }

            time_t now = time(NULL);
            struct tm *t = localtime(&now);
            char image_filename[256];

            snprintf(image_filename, sizeof(image_filename),
                "image/%.*s_image_%04d-%02d-%02d_%02d:%02d:%02d.png",
                (int)(strlen(filename)-4), filename,
                t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                t->tm_hour, t->tm_min, t->tm_sec);

            FILE *img_fp = fopen(image_filename, "wb");
            if (img_fp) {
                fwrite(img_buffer, 1, img_size, img_fp);
                fclose(img_fp);

                write_log(filename, image_filename);
                free(img_buffer);
            }
        }
    }

    struct zip_file *zf = zip_fopen(zip_archive, filename, 0);
    if (!zf) return -ENOENT;

    if (offset > 0) {
    char dump[offset];
    zip_fread(zf, dump, offset);
    }

    int bytes_read = zip_fread(zf, buf, size);
    zip_fclose(zf);
    return bytes_read;
}                  

static int xmp_unlink(const char *path) {
    const char *filename = path + 1;

    if(strstr(filename, ".zip")) {
     if (remove(filename) == 0) {
          return 0;
        } else {
          return -errno;
     }
   }
    return -EPERM;
} 

static int convert_hex(const char *txt_path, char **buffer, size_t *buffer_size) {
    struct zip_file *zf = zip_fopen(zip_archive, txt_path, 0);
    if (!zf) {
      return -1;
    }

    struct zip_stat zs;
    zip_stat(zip_archive, txt_path, 0, &zs);
    if (zip_stat(zip_archive, txt_path, 0, &zs) != 0) {
      return -1;
    }

    char *hex_str = malloc(zs.size + 1);
    zip_fread(zf, hex_str, zs.size);
    hex_str[zs.size] = '\0';
    zip_fclose(zf);

    size_t byte_len = strlen(hex_str) / 2;
    unsigned char *img_data = malloc(byte_len);
    if (!img_data) {
        free(hex_str);
        return -1;
    }

    for (size_t i = 0; i < byte_len; i++) {
        char byte_pair[3] = {hex_str[2*i], hex_str[2*i+1], '\0'};
        img_data[i] = (unsigned char) strtol(byte_pair, NULL, 16);
    }

    *buffer = (char *)img_data;
    *buffer_size = byte_len;

    free(hex_str);
    return 0;
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
