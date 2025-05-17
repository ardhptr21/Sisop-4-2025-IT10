#define FUSE_USE_VERSION 28
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <stdlib.h>

static const char *dirpath = "/app/it24_host";
static const char *logpath = "/app/antink-logs/it24.log";

void write_log(const char *level, const char *message) {
    time_t now;
    time(&now);
    struct tm *timeinfo = localtime(&now);
    char timestamp[30];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", timeinfo);
    
    FILE *log_file = fopen(logpath, "a");
    if (log_file) {
        fprintf(log_file, "antink-logger-1 | [%s] [%s] %s\n", timestamp, level, message);
        fclose(log_file);
    }
}

char* reverse_string(const char* str) {
    int len = strlen(str);
    char* rev = (char*)malloc(len + 1);
    for(int i = 0; i < len; i++) {
        rev[i] = str[len - 1 - i];
    }
    rev[len] = '\0';
    return rev;
}

void rot13_encrypt(char* text) {
    for(int i = 0; text[i] != '\0'; i++) {
        if(text[i] >= 'a' && text[i] <= 'z') {
            text[i] = 'a' + (text[i] - 'a' + 13) % 26;
        }
        else if(text[i] >= 'A' && text[i] <= 'Z') {
            text[i] = 'A' + (text[i] - 'A' + 13) % 26;
        }
    }
}

static int xmp_getattr(const char *path, struct stat *stbuf) {
    char fpath[1000];
    sprintf(fpath, "%s%s", dirpath, path);
    int res = lstat(fpath, stbuf);
    if (res == -1) return -errno;
    return 0;
}

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                      off_t offset, struct fuse_file_info *fi) {
    char fpath[1000];
    sprintf(fpath, "%s%s", dirpath, path);
    
    DIR *dp = opendir(fpath);
    if (dp == NULL) return -errno;
    
    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            filler(buf, de->d_name, &st, 0);
            continue;
        }

        char *filename = strdup(de->d_name);
        if (strstr(filename, "nafis") || strstr(filename, "kimcun")) {
            char msg[1000];
            sprintf(msg, "Anomaly detected %s in file: %s", 
                    strstr(filename, "nafis") ? "nafis" : "kimcun", filename);
            write_log("ALERT", msg);
            
            char *reversed = reverse_string(filename);
            filler(buf, reversed, &st, 0);
            free(reversed);
        } else {
            filler(buf, filename, &st, 0);
        }
        free(filename);
    }
    closedir(dp);
    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                   struct fuse_file_info *fi) {
    char fpath[1000];
    sprintf(fpath, "%s%s", dirpath, path);
    
    int fd = open(fpath, O_RDONLY);
    if (fd == -1) return -errno;
    
    int res = pread(fd, buf, size, offset);
    if (res == -1) res = -errno;
    
    if (strstr(path, "nafis") == NULL && strstr(path, "kimcun") == NULL) {
        if (res > 0) {
            buf[res] = '\0';
            rot13_encrypt(buf);
            write_log("ENCRYPT", "File has been encrypted");
        }
    }
    
    close(fd);
    return res;
}

static struct fuse_operations xmp_oper = {
    .getattr = xmp_getattr,
    .readdir = xmp_readdir,
    .read = xmp_read,
};

int main(int argc, char *argv[]) {
    umask(0);
    return fuse_main(argc, argv, &xmp_oper, NULL);
}