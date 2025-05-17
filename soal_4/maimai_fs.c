#define _DEFAULT_SOURCE
#define FUSE_USE_VERSION 28

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

char *fullpath(const char *path, const char *base);
char *mappath(const char *path);
void iv_from_filename(const char *filepath, unsigned char iv[16]);
int aes256_cbc_encrypt(const unsigned char *plaintext, int plaintext_len, const unsigned char *key, const unsigned char *iv, unsigned char **ciphertext);
int aes256_cbc_decrypt(const unsigned char *ciphertext, int ciphertext_len, const unsigned char *key, const unsigned char *iv, unsigned char **plaintext);
int gzip_compress(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len);
int gzip_decompress(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len);

static int xmp_getattr(const char *path, struct stat *stbuf);
static int xmp_create(const char *path, mode_t mode, struct fuse_file_info *fi);
static int xmp_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int xmp_release(const char *path, struct fuse_file_info *fi);
static int xmp_unlink(const char *path);
static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

struct fuse_operations xmp_oper = {
    .getattr = xmp_getattr,
    .readdir = xmp_readdir,
    .create = xmp_create,
    .write = xmp_write,
    .release = xmp_release,
    .read = xmp_read,
    .unlink = xmp_unlink,
};

#define BASE_FOLDER "chiho"
#define AES_KEY_SIZE 34
#define AES_BLOCK_SIZE 16

const unsigned char AES_KEY[AES_KEY_SIZE] = "SopSopSopSisopAnomaliSistemOperasi";

int main(int argc, char *argv[]) {
    umask(0);
    return fuse_main(argc, argv, &xmp_oper, NULL);
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

char *mappath(const char *path) {
    char target[PATH_MAX];

    if (strncmp(path, "/starter/", 9) == 0) {
        snprintf(target, sizeof(target), "%s.mai", path);
    } else if (strncmp(path, "/metro/", 7) == 0) {
        snprintf(target, sizeof(target), "%s.ccc", path);
    } else if (strncmp(path, "/dragon/", 8) == 0) {
        snprintf(target, sizeof(target), "%s.rot", path);
    } else if (strncmp(path, "/blackrose/", 11) == 0) {
        snprintf(target, sizeof(target), "%s.bin", path);
    } else if (strncmp(path, "/heaven/", 8) == 0) {
        snprintf(target, sizeof(target), "%s.enc", path);
    } else if (strncmp(path, "/skystreet/", 11) == 0) {
        snprintf(target, sizeof(target), "%s.gz", path);
    } else {
        snprintf(target, sizeof(target), "%s", path);
    }

    return fullpath(target, BASE_FOLDER);
}

void iv_from_filename(const char *filepath, unsigned char iv[16]) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char *)filepath, strlen(filepath), hash);
    memcpy(iv, hash, 16);
}

int aes256_cbc_encrypt(const unsigned char *plaintext, int plaintext_len, const unsigned char *key, const unsigned char *iv, unsigned char **ciphertext) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    int len;
    int ciphertext_len;

    *ciphertext = malloc(plaintext_len + EVP_MAX_BLOCK_LENGTH);
    if (!*ciphertext) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv)) {
        free(*ciphertext);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    if (1 != EVP_EncryptUpdate(ctx, *ciphertext, &len, plaintext, plaintext_len)) {
        free(*ciphertext);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len = len;

    if (1 != EVP_EncryptFinal_ex(ctx, *ciphertext + len, &len)) {
        free(*ciphertext);
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }
    ciphertext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    return ciphertext_len;
}

int aes256_cbc_decrypt(const unsigned char *ciphertext, int ciphertext_len, const unsigned char *key, const unsigned char *iv, unsigned char **plaintext) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return -1;

    *plaintext = malloc(ciphertext_len);
    if (!*plaintext) {
        EVP_CIPHER_CTX_free(ctx);
        return -1;
    }

    int len = 0, plaintext_len = 0;

    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv)) goto err;
    if (1 != EVP_DecryptUpdate(ctx, *plaintext, &len, ciphertext, ciphertext_len)) goto err;
    plaintext_len = len;
    if (1 != EVP_DecryptFinal_ex(ctx, *plaintext + len, &len)) goto err;
    plaintext_len += len;

    EVP_CIPHER_CTX_free(ctx);
    return plaintext_len;

err:
    EVP_CIPHER_CTX_free(ctx);
    free(*plaintext);
    *plaintext = NULL;
    return -1;
}

int gzip_compress(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len) {
    int ret;
    z_stream strm;
    const size_t temp_chunk_size = 8192;

    unsigned char *temp_out = malloc(temp_chunk_size);
    if (!temp_out) return Z_MEM_ERROR;

    *out = NULL;
    *out_len = 0;

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    // Use deflateInit2 to enable gzip header (windowBits = 16 + MAX_WBITS)
    ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                       MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        free(temp_out);
        return ret;
    }

    strm.next_in = (unsigned char *)in;
    strm.avail_in = in_len;

    unsigned char *compressed = NULL;
    size_t total_out = 0;

    do {
        strm.next_out = temp_out;
        strm.avail_out = temp_chunk_size;

        ret = deflate(&strm, strm.avail_in ? Z_NO_FLUSH : Z_FINISH);
        if (ret == Z_STREAM_ERROR) {
            free(compressed);
            free(temp_out);
            deflateEnd(&strm);
            return ret;
        }

        size_t have = temp_chunk_size - strm.avail_out;
        if (have > 0) {
            unsigned char *tmp = realloc(compressed, total_out + have);
            if (!tmp) {
                free(compressed);
                free(temp_out);
                deflateEnd(&strm);
                return Z_MEM_ERROR;
            }
            compressed = tmp;
            memcpy(compressed + total_out, temp_out, have);
            total_out += have;
        }

    } while (ret != Z_STREAM_END);

    deflateEnd(&strm);
    free(temp_out);

    *out = compressed;
    *out_len = total_out;
    return Z_OK;
}

int gzip_decompress(const unsigned char *in, size_t in_len, unsigned char **out, size_t *out_len) {
    int ret;
    z_stream strm;
    const size_t temp_chunk_size = 8192;
    unsigned char *temp_out = malloc(temp_chunk_size);
    if (!temp_out) return Z_MEM_ERROR;

    *out = NULL;
    *out_len = 0;

    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;

    // Use inflateInit2 with windowBits = 16 + MAX_WBITS to decode gzip headers
    ret = inflateInit2(&strm, MAX_WBITS + 16);
    if (ret != Z_OK) {
        free(temp_out);
        return ret;
    }

    strm.next_in = (unsigned char *)in;
    strm.avail_in = in_len;

    unsigned char *decompressed = NULL;
    size_t total_out = 0;

    do {
        strm.next_out = temp_out;
        strm.avail_out = temp_chunk_size;

        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            free(temp_out);
            free(decompressed);
            inflateEnd(&strm);
            return ret;
        }

        size_t have = temp_chunk_size - strm.avail_out;
        if (have > 0) {
            unsigned char *tmp = realloc(decompressed, total_out + have);
            if (!tmp) {
                free(decompressed);
                free(temp_out);
                inflateEnd(&strm);
                return Z_MEM_ERROR;
            }
            decompressed = tmp;
            memcpy(decompressed + total_out, temp_out, have);
            total_out += have;
        }

    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    free(temp_out);

    *out = decompressed;
    *out_len = total_out;

    return Z_OK;
}

static int xmp_getattr(const char *path, struct stat *stbuf) {
    int res;
    char target[PATH_MAX];

    if (strncmp(path, "/7sref/", 7) == 0) {
        char *underscore = strchr(path + 7, '_');
        if (underscore != NULL) {
            size_t len = underscore - path - 6;
            strncpy(target, path + 6, len);
            target[len] = '\0';
            strcat(target, "/");
            strcat(target, path + strlen(path) - strlen(underscore) + 1);
        } else {
            strcpy(target, path);
        }
    } else {
        strcpy(target, path);
    }
    char *map_path = mappath(target);

    res = lstat(map_path, stbuf);
    if (res == -1) {
        char *orig_path = fullpath(path, BASE_FOLDER);
        res = lstat(orig_path, stbuf);
        free(orig_path);
        if (res == -1) {
            free(map_path);
            return -errno;
        }
    }

    free(map_path);
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
};

static int xmp_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    char *fpath = fullpath(path, BASE_FOLDER);
    FILE *file = fopen(fpath, "r+b");
    if (!file) {
        file = fopen(fpath, "w+b");
        if (!file) {
            free(fpath);
            return -errno;
        }
    }

    size_t written = 0;
    fseek(file, offset, SEEK_SET);

    if (strncmp(path, "/metro/", 7) == 0) {
        char *encoded = malloc(size);
        if (!encoded) {
            fclose(file);
            free(fpath);
            return -ENOMEM;
        }
        for (size_t i = 0; i < size; i++) {
            encoded[i] = (unsigned char)(buf[i] + (i % 256));
        }
        written = fwrite(encoded, 1, size, file);
        free(encoded);
    } else if (strncmp(path, "/dragon/", 8) == 0) {
        char *encoded = malloc(size);
        if (!encoded) {
            fclose(file);
            free(fpath);
            return -ENOMEM;
        }
        for (size_t i = 0; i < size; i++) {
            if (buf[i] >= 'a' && buf[i] <= 'z') {
                encoded[i] = (buf[i] - 'a' + 13) % 26 + 'a';
            } else if (buf[i] >= 'A' && buf[i] <= 'Z') {
                encoded[i] = (buf[i] - 'A' + 13) % 26 + 'A';
            } else {
                encoded[i] = buf[i];
            }
        }
        written = fwrite(encoded, 1, size, file);
        free(encoded);
    } else if (strncmp(path, "/heaven/", 8) == 0) {
        unsigned char iv[16];
        iv_from_filename(path, iv);

        unsigned char *ciphertext = NULL;
        int ciphertext_len = aes256_cbc_encrypt((unsigned char *)buf, (int)size, AES_KEY, iv, &ciphertext);
        if (ciphertext_len < 0) {
            fclose(file);
            free(fpath);
            return -EIO;
        }

        if (fseek(file, offset, SEEK_SET) != 0) {
            free(ciphertext);
            fclose(file);
            free(fpath);
            return -EIO;
        }

        size_t bytes_written = fwrite(ciphertext, 1, ciphertext_len, file);
        free(ciphertext);

        fclose(file);
        free(fpath);

        if (bytes_written < (size_t)ciphertext_len) return -EIO;
        return size;
    } else if (strncmp(path, "/skystreet/", 11) == 0) {
        unsigned char *compressed = NULL;
        size_t compressed_len = 0;
        int ret = gzip_compress((unsigned char *)buf, size, &compressed, &compressed_len);
        if (ret != Z_OK) {
            fclose(file);
            free(fpath);
            return -EIO;
        }

        written = fwrite(compressed, 1, compressed_len, file);
        free(compressed);

        fclose(file);
        free(fpath);
        return size;
    } else {
        written = fwrite(buf, 1, size, file);
    }

    fclose(file);
    free(fpath);
    if (written < size) return -EIO;
    return written;
};

static int xmp_release(const char *path, struct fuse_file_info *fi) {
    char *orig_path = fullpath(path, BASE_FOLDER);
    char *map_path = mappath(path);

    if (rename(orig_path, map_path) == -1) {
        free(orig_path);
        free(map_path);
        return -errno;
    }
    free(orig_path);
    free(map_path);

    return 0;
};

static int xmp_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    char target[PATH_MAX];

    if (strncmp(path, "/7sref/", 7) == 0) {
        char *underscore = strchr(path + 7, '_');
        if (underscore != NULL) {
            size_t len = underscore - path - 6;
            strncpy(target, path + 6, len);
            target[len] = '\0';
            strcat(target, "/");
            strcat(target, path + strlen(path) - strlen(underscore) + 1);
        } else {
            strcpy(target, path);
        }
    } else {
        strcpy(target, path);
    }

    char *fpath = mappath(target);
    FILE *file = fopen(fpath, "rb");
    if (!file) {
        free(fpath);
        return -errno;
    }
    fseek(file, offset, SEEK_SET);
    size_t bytes_read = fread(buf, 1, size, file);

    if (strncmp(target, "/metro/", 7) == 0) {
        for (size_t i = 0; i < bytes_read; i++) {
            buf[i] = (unsigned char)(buf[i] - (i % 256));
        }
    } else if (strncmp(target, "/dragon/", 8) == 0) {
        for (size_t i = 0; i < bytes_read; i++) {
            if (buf[i] >= 'a' && buf[i] <= 'z') {
                buf[i] = (buf[i] - 'a' + 13) % 26 + 'a';
            } else if (buf[i] >= 'A' && buf[i] <= 'Z') {
                buf[i] = (buf[i] - 'A' + 13) % 26 + 'A';
            }
        }
    } else if (strncmp(target, "/heaven/", 8) == 0) {
        unsigned char iv[16];
        iv_from_filename(path, iv);

        off_t aligned_offset = offset - (offset % AES_BLOCK_SIZE);
        size_t read_len = size + (offset - aligned_offset) + AES_BLOCK_SIZE;

        FILE *file = fopen(fpath, "rb");
        if (!file) {
            free(fpath);
            return -errno;
        }

        unsigned char *ciphertext = malloc(read_len);
        if (!ciphertext) {
            fclose(file);
            free(fpath);
            return -ENOMEM;
        }

        size_t bytes_read = fread(ciphertext, 1, read_len, file);
        fclose(file);
        free(fpath);

        if (bytes_read == 0) {
            free(ciphertext);
            return 0;
        }

        unsigned char *plaintext = NULL;
        int plaintext_len = aes256_cbc_decrypt(ciphertext, (int)bytes_read, AES_KEY, iv, &plaintext);
        if (plaintext_len < 0) {
            free(ciphertext);
            return -EIO;
        }

        size_t start = offset - aligned_offset;
        if (start > (size_t)plaintext_len) start = plaintext_len;

        size_t to_copy = size;
        if (start + to_copy > (size_t)plaintext_len) {
            to_copy = plaintext_len - start;
        }

        memcpy(buf, plaintext + start, to_copy);
        free(plaintext);

        return (int)to_copy;
    } else if (strncmp(path, "/skystreet/", 11) == 0) {
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);
        fseek(file, 0, SEEK_SET);

        if (file_size <= 0) {
            fclose(file);
            free(fpath);
            return 0;
        }

        unsigned char *compressed_data = malloc(file_size);
        if (!compressed_data) {
            fclose(file);
            free(fpath);
            return -ENOMEM;
        }

        size_t read_bytes = fread(compressed_data, 1, file_size, file);
        fclose(file);
        free(fpath);

        if (read_bytes != (size_t)file_size) {
            free(compressed_data);
            return -EIO;
        }

        unsigned char *decompressed_data = NULL;
        size_t decompressed_len = 0;

        int ret = gzip_decompress(compressed_data, file_size, &decompressed_data, &decompressed_len);
        free(compressed_data);
        if (ret != Z_OK) {
            free(decompressed_data);
            return -EIO;
        }

        if ((size_t)offset > decompressed_len) {
            free(decompressed_data);
            return 0;
        }

        size_t to_copy = size;
        if (offset + to_copy > decompressed_len) {
            to_copy = decompressed_len - offset;
        }

        memcpy(buf, decompressed_data + offset, to_copy);
        free(decompressed_data);

        return (int)to_copy;
    }

    fclose(file);
    free(fpath);
    return bytes_read;
}

static int xmp_unlink(const char *path) {
    char target[PATH_MAX];

    if (strncmp(path, "/7sref/", 7) == 0) {
        char *underscore = strchr(path + 7, '_');
        if (underscore != NULL) {
            size_t len = underscore - path - 6;
            strncpy(target, path + 6, len);
            target[len] = '\0';
            strcat(target, "/");
            strcat(target, path + strlen(path) - strlen(underscore) + 1);
        } else {
            strcpy(target, path);
        }
    } else {
        strcpy(target, path);
    }

    char *fpath = mappath(target);
    struct stat st;
    if (stat(fpath, &st) == -1) {
        free(fpath);
        return -ENOENT;
    }
    if (remove(fpath) == -1) {
        free(fpath);
        return -errno;
    }
    free(fpath);
    return 0;
};

static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    if (strcmp(path, "/7sref") == 0) {
        char *subdirs[] = {"starter", "metro", "dragon", "blackrose", "heaven", "skystreet"};
        for (int i = 0; i < sizeof(subdirs) / sizeof(subdirs[0]); i++) {
            char *subdirfpath = fullpath(subdirs[i], BASE_FOLDER);
            DIR *dp = opendir(subdirfpath);
            if (dp == NULL) {
                free(subdirfpath);
                continue;
            }

            struct dirent *de;
            while ((de = readdir(dp)) != NULL) {
                if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
                char *dot = strrchr(de->d_name, '.');
                if (dot != NULL) {
                    size_t new_len = dot - de->d_name;
                    char *modified = strndup(de->d_name, new_len);
                    if (modified == NULL) {
                        closedir(dp);
                        free(subdirfpath);
                        return -ENOMEM;
                    }
                    char prefix[PATH_MAX];
                    sprintf(prefix, "%s_%s", subdirs[i], modified);
                    if (filler(buf, prefix, NULL, 0) != 0) {
                        free(modified);
                        closedir(dp);
                        free(subdirfpath);
                        return -ENOMEM;
                    }
                    free(modified);
                } else {
                    if (filler(buf, de->d_name, NULL, 0) != 0) {
                        closedir(dp);
                        free(subdirfpath);
                        return -ENOMEM;
                    }
                }
            }
            closedir(dp);
            free(subdirfpath);
        }

        return 0;
    }

    char *fpath = fullpath(path, BASE_FOLDER);
    DIR *dp = opendir(fpath);
    if (dp == NULL) {
        free(fpath);
        return -errno;
    }

    struct dirent *de;
    while ((de = readdir(dp)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char *dot = strrchr(de->d_name, '.');
        if (dot != NULL) {
            size_t new_len = dot - de->d_name;
            char *modified = strndup(de->d_name, new_len);
            if (modified == NULL) {
                closedir(dp);
                free(fpath);
                return -ENOMEM;
            }
            if (filler(buf, modified, NULL, 0) != 0) {
                free(modified);
                closedir(dp);
                free(fpath);
                return -ENOMEM;
            }
            free(modified);
        } else {
            if (filler(buf, de->d_name, NULL, 0) != 0) {
                closedir(dp);
                free(fpath);
                return -ENOMEM;
            }
        }
    }
    closedir(dp);
    free(fpath);
    return 0;
};
