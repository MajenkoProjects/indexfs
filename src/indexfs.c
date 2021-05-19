#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <linux/fs.h>
#include <errno.h>

/* Index file layout:

/path/to/file https://url/to/file
/path/to/other https://url/to/other

*/

#define TDIR 0
#define TFILE 1

char *config = NULL;

typedef struct index_s {
    int type;
    char *file;
    char *url;
    long size;
    struct index_s *next;
} file_t;

file_t *fileindex = NULL;

struct block {
    size_t pos;
    void *buffer;
};

static file_t *getFileByName(const char *n) {
    for (file_t *scan = fileindex; scan; scan = scan->next) {
        if(!strcmp(scan->file, n)) {
            return scan;
        }
    }
    return NULL;
}

static file_t *createFileNode(const char *path, const char *url, int type) {
    file_t *file = (file_t *)malloc(sizeof(file_t));
    file->file = strdup(path);
    if (url == NULL) {
        file->url = NULL;
    } else {
        file->url = strdup(url);
    }
    file->size = -1;
    file->type = type;
    file->next = NULL;

    if (fileindex == NULL) {
        fileindex = file;
        return file;
    }

    for (file_t *scan = fileindex; scan; scan = scan->next) {
        if (scan->next == NULL) {
            scan->next = file;
            return file;
        }
    }
    free(file->file);
    if (file->url) free(file->url);
    free(file);
    return NULL;
}

static void deleteFileNode(file_t *file) {
    if (file != NULL) {
        if (file == fileindex) {
            fileindex = file->next;
        } else {
            for (file_t *scan = fileindex; scan; scan = scan->next) {
                if (scan->next == file) {
                    scan->next = file->next;
                }
            }
        }

        free(file->file);
        if (file->url != NULL) free(file->url);
        free(file);
    }
}

static file_t *createDirectory(const char *path) {
    file_t *file = getFileByName(path);
    if (file != NULL) {
        return NULL;
    } 

    file = createFileNode(path, NULL, TDIR);
    return file;
}

static file_t *createFile(const char *path, const char *url) {
    file_t *file = getFileByName(path);
    if (file != NULL) {
        return NULL;
    } 

    file = createFileNode(path, url, TFILE);
    return file;
}

static size_t getBlock(void *data, size_t size, size_t nmemb, void *userp) {
    struct block *block = (struct block *)userp;
    memcpy(block->buffer  + block->pos, data, nmemb);
    block->pos += nmemb;
    return nmemb;
}

static size_t getHeader(char *b, size_t size, size_t nitems, void *ud) {
    struct index_s *file = (struct index_s *)ud;

    if (strncasecmp(b, "content-length: ", 16) == 0) {
        file->size = strtol(b + 16, 0, 10);
    }

    if (strncasecmp(b, "location: ", 10) == 0) {
        char *trim = strtok(b + 10, " \t\r\n");
        if (file->url != NULL) free(file->url);
        file->url = strdup(trim);
    }
    return nitems;
}

static long getFileSize(struct index_s *file) {
    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, file->url);
    curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, file);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, getHeader);
    int res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_easy_strerror(res);
    }
    curl_easy_cleanup(curl);
    return 0;
}

static int fuse_truncate(const char *path, off_t offset) {
    file_t *file = getFileByName(path);
    if (file == NULL) return -ENOENT;
    if (file->type != TFILE) return -EISDIR;
    return 0;
}

static int fuse_mkdir(const char *path, mode_t mode) {
    file_t *newdir = createDirectory(path);
    if (newdir == NULL) {
        return -EEXIST;
    }
    return 0;
}

static int fuse_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

    filler(buffer, ".", NULL, 0);
    filler(buffer, "..", NULL, 0);

    int l = strlen(path);
    char search[l + 2];
    strcpy(search, path);
    if (search[l-1] != '/') {
        strcat(search, "/");
        l++;
    }

    if (fileindex == NULL) return 0;

    for (file_t *scan = fileindex; scan; scan = scan->next) {
        if (strncmp(scan->file, search, l) == 0) {
            char *sub = strdup(scan->file + l);
            char *fn = strtok(sub, "/");
            filler(buffer, fn, NULL, 0);
            free(sub);
        }
    }

    return 0;
}

static int fuse_getattr(const char *path, struct stat *st) {

    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_atime = time( NULL );
    st->st_mtime = time( NULL );

    if ((strcmp(path, ".") == 0) || (strcmp(path, "..") == 0)) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }

    if (strcmp(path, "/") == 0) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }


    file_t *file = getFileByName(path);

    if (file == NULL) return -ENOENT;

    if (file->type == TDIR) {
        st->st_mode = S_IFDIR | 0755;
        st->st_nlink = 2;
        return 0;
    }

    if (file->type == TFILE) {
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;

        if (file->url == NULL) {
            st->st_size = 0;
        } else {
            if (file->size == -1) {
                getFileSize(file);
            }
            st->st_size = file->size;
        }
        return 0;
    }
    
    return -1;
}

static int fuse_rmdir(const char *path) {
    file_t *file = getFileByName(path);
    if (file == NULL) return -ENOENT;
    if (file->type != TDIR) return -ENOTDIR;
    deleteFileNode(file);
    return 0;
}

static int fuse_open(const char *path, struct fuse_file_info *fi) {
    struct index_s *file = getFileByName(path);

    if (file == NULL) {
        if ((fi->flags & O_ACCMODE) == O_RDONLY) {
            return -ENOENT;
        }

        file = createFile(path, NULL);
        return 0;
    }
    
    return 0;
}

static int fuse_release(const char *path, struct fuse_file_info *fi) {
    return 0;
}

static int fuse_write(const char *path, const char *data, size_t len, off_t offset, struct fuse_file_info *fi) {
    return 0;
}


static int fuse_write_buf(const char *path, struct fuse_bufvec *buf, off_t off, struct fuse_file_info *fi) {
    return 0;
}


static int fuse_utimens(const char *path, const struct timespec tv[2]) {
    return 0;
}

static int fuse_create(const char *path, mode_t mode, struct fuse_file_info *finfo) {
    file_t *file = getFileByName(path);
    if (file != NULL) return -EEXIST;
    createFile(path, NULL);
    return 0;
}

static int fuse_read( const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi )
{
    char range[100];
    sprintf(range, "%ld-%ld", offset, offset + size - 1);
    file_t *file = getFileByName(path);
    if (file == NULL) return -ENOENT;
    if (file->type == TDIR) return -EISDIR;
    if (file->url == NULL) return -ENXIO;

    struct block block;

    block.buffer = buffer;
    block.pos = 0;

    CURL *curl = curl_easy_init();
    curl_easy_setopt(curl, CURLOPT_URL, file->url);
    curl_easy_setopt(curl, CURLOPT_RANGE, range);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &block);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, getBlock);
    int res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_easy_strerror(res);
    }
    curl_easy_cleanup(curl);
    return block.pos;
}

static int fuse_listxattr(const char *path, char *buffer, size_t len) {
    if (strcmp(path, ".") == 0) return 0;
    if (strcmp(path, "..") == 0) return 0;

    file_t *file = getFileByName(path);
    if (file == NULL) return -ENOENT;
    if (file->type != TFILE) return 0;
    if (file->url == NULL) return 0;

    if (len == 0) {
        return 4;
    }

    strcpy(buffer, "url");
    return 4;
}

static int fuse_getxattr(const char *path, const char *attr, char *buffer, size_t len) {
    if (strcmp(path, ".") == 0) return 0;
    if (strcmp(path, "..") == 0) return 0;

    file_t *file = getFileByName(path);
    if (file == NULL) return -ENOENT;
    if (file->type != TFILE) return 0;
    if (file->url == NULL) return 0;
    
    if (len == 0) {
        return strlen(file->url);
    }

    strcpy(buffer, file->url);
    return strlen(file->url);
}

static int fuse_setxattr(const char *path, const char *attr, const char *value, size_t len, int flags) {

    if (strcmp(attr, "url") != 0) return -ENOENT;

    file_t *file = getFileByName(path);
    if (file == NULL) return -ENOENT;

    if (file->url != NULL) free(file->url);
    file->url = strdup(value);
    file->size = -1;
    return 0;
}

static int fuse_rename(const char *src, const char *dst) { 
    file_t *from = getFileByName(src);
    file_t *to = getFileByName(dst);

    if (from == NULL) return -ENOENT;

    free(from->file);
    from->file = strdup(dst);

    deleteFileNode(to);

    return 0;
}

static int fuse_unlink(const char *path) {
    struct index_s *file = getFileByName(path);
    if (file == NULL) return -ENOENT;
    deleteFileNode(file);
    return 0;
}

static void *fuse_init(struct fuse_conn_info *conn) {
    FILE *f = fopen(config, "r");
    if (f == NULL) return NULL;

    char entry[32768];
    while (fgets(entry, 32768, f) != NULL) {
        char *type = strtok(entry, "\t\r\n");
        char *name = strtok(NULL, "\t\r\n");
        char *url = strtok(NULL, "\t\r\n");

        if (name == NULL) continue;

        if (type[0] == 'F') {
            createFile(name, url);
        } else if (type[0] == 'D') {
            createDirectory(name);
        }
    }
    fclose(f);
    return NULL;
}

static void fuse_save(void * __attribute__((unused)) dunno) {
    FILE *f = fopen(config, "w");
    if (!f) {
        printf("Error saving state!\n");
        return;
    }
    for (file_t *scan = fileindex; scan; scan = scan->next) {
        if (scan->type == TDIR) {
            fprintf(f, "D\t%s\n", scan->file);
        } else if (scan->type == TFILE) {
            if (scan->url == NULL) {
                fprintf(f, "F\t%s\n", scan->file);
            } else {
                fprintf(f, "F\t%s\t%s\n", scan->file, scan->url);

            }
        }
    }
    fclose(f);
}

static struct fuse_operations operations = {

    .mkdir      = fuse_mkdir,
    .readdir	= fuse_readdir,
    .getattr	= fuse_getattr,
    .rmdir      = fuse_rmdir,
    .open       = fuse_open,
    .truncate   = fuse_truncate,
    .release    = fuse_release,
    .write_buf  = fuse_write_buf,
    .write      = fuse_write,
    .utimens    = fuse_utimens,
    .create     = fuse_create,
    .read       = fuse_read,
    .listxattr  = fuse_listxattr,
    .getxattr   = fuse_getxattr,
    .setxattr   = fuse_setxattr,
    .rename     = fuse_rename,
    .unlink     = fuse_unlink,
    .init       = fuse_init,
    .destroy    = fuse_save,
};

int main(int argc, char **argv) {

    int opt;

    char *args[argc];
    int a = 0;
    args[a++] = argv[0];

    while ((opt = getopt(argc, argv, "c:m:fdhsV")) != -1) {
        switch (opt) {
            case 'c':
                config = optarg;
                break;
            case 'm':
                args[a++] = optarg;
                break;
            case 'd':
                args[a++] = "-d";
                break;
            case 'f':
                args[a++] = "-f";
                break;
            case 'h':
                args[a++] = "-h";
                break;
            case 's':
                args[a++] = "-s";
                break;
            case 'V':
                args[a++] = "-V";
                break;
        }
    }

    if (config == NULL) {
        printf("Error: -c must be used to specify a config file location\n");
        return -1;
    }

    return fuse_main( a, args, &operations, NULL );
}
