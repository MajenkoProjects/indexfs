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

/* Index file layout:

/path/to/file https://url/to/file
/path/to/other https://url/to/other

*/

struct index_s {
    char *file;
    char *url;
    long size;
    struct index_s *next;
};

struct index_s *fileindex = NULL;

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

static void loadIndex() {
    struct index_s *head = NULL;

    FILE *f = fopen("/etc/index.fs", "r");
    char temp[1024];
    while (fgets(temp, 1024, f) != NULL) {
        char *file = strtok(temp, " \t\r\n");
        char *url = strtok(NULL, " \t\r\n");

        struct index_s *ent = (struct index_s *)malloc(sizeof(struct index_s));
        ent->file = strdup(file);
        ent->url = strdup(url);
        ent->next = NULL;
        ent->size = -1;
        if (head == NULL) {
            fileindex = ent;
            head = fileindex;
        } else {
            head->next = ent;
            head = ent;
        }
    }
    fclose(f);
}

static void deleteFile(struct index_s *file) {
    if (file != NULL) {
        if (file == fileindex) {
            fileindex = file->next;
        } else {
            for (struct index_s *scan = fileindex; scan; scan = scan->next) {
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

static void appendFile(struct index_s *file) {
    if (fileindex == NULL) {
        fileindex = file;
        return;
    }

    if (file != NULL) {
        for (struct index_s *scan = fileindex; scan; scan = scan->next) {
            if (scan->next == NULL) {
                scan->next = file;
                return;
            }
        }
    }
}

static int is_directory(const char *path) {
    int l = strlen(path);
    char search[l + 1];
    strcpy(search, path);
    if (search[l - 1] != '/') {
        strcat(search, "/");
        l++;
    }

    for (struct index_s *scan = fileindex; scan; scan = scan->next) {
        if (strncmp(scan->file, search, l) == 0) {
            return 1;
        }
    }
    return 0;
}

struct index_s *getFileByName(const char *n) {
    for (struct index_s *scan = fileindex; scan; scan = scan->next) {
        if(!strcmp(scan->file, n)) {
            return scan;
        }
    }
    return NULL;
}

static int do_getattr(const char *path, struct stat *st) {

    st->st_uid = getuid();
    st->st_gid = getgid();
    st->st_atime = time( NULL );
    st->st_mtime = time( NULL );

    if (is_directory(path)) {
        st->st_mode = S_IFDIR | 0555;
        st->st_nlink = 2;
    } else {
        struct index_s *file = getFileByName(path);
        if (file) {
            if (file->url == NULL) {
                st->st_size = 0;
            } else {
                if (file->size == -1) {
                    getFileSize(file);
                }
                st->st_size = file->size;
            }
        }
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;
    }
    return 0;
}

static int do_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

    filler(buffer, ".", NULL, 0);
    filler(buffer, ".", NULL, 0);

    int l = strlen(path);
    char search[l + 2];
    strcpy(search, path);
    if (search[l-1] != '/') {
        strcat(search, "/");
        l++;
    }

    for (struct index_s *scan = fileindex; scan; scan = scan->next) {
        if (strncmp(scan->file, search, l) == 0) {
            char *sub = strdup(scan->file + l);
            char *fn = strtok(sub, "/");
            filler(buffer, fn, NULL, 0);
            free(sub);
        }
    }

    return 0;
}

static int do_listxattr(const char *path, char *buffer, size_t len) {
    if (strcmp(path, ".") == 0) return 0;
    if (strcmp(path, "..") == 0) return 0;
    if (is_directory(path)) return 0;

    struct index_s *file = getFileByName(path);
    if (file == NULL) return -1;
    if (file->url == NULL) return 0;

    if (len == 0) {
        return 4;
    }

    strcpy(buffer, "url");
    return 4;
}

static int do_getxattr(const char *path, const char *attr, char *buffer, size_t len) {
    if (strcmp(path, ".") == 0) return 0;
    if (strcmp(path, "..") == 0) return 0;
    if (is_directory(path)) return 0;

    struct index_s *file = getFileByName(path);
    if (file == NULL) return -1;
    if (file->url == NULL) return 0;
    
    if (len == 0) {
        return strlen(file->url);
    }

    strcpy(buffer, file->url);
    return strlen(file->url);
}

static int do_setxattr(const char *path, const char *attr, const char *value, size_t len, int flags) {

    if (strcmp(attr, "url") != 0) {
        return -1;
    }

    struct index_s *file = getFileByName(path);
    if (file == NULL) {
        return -1;
    }

    if (file->url != NULL) free(file->url);
    file->url = strdup(value);
    file->size = -1;
    return 0;
}

struct block {
    size_t pos;
    void *buffer;
};

static size_t getBlock(void *data, size_t size, size_t nmemb, void *userp) {
    struct block *block = (struct block *)userp;
    memcpy(block->buffer  + block->pos, data, nmemb);
    block->pos += nmemb;
    return nmemb;
}

static int do_read( const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi )
{
    char range[100];
    sprintf(range, "%ld-%ld", offset, offset + size - 1);
    struct index_s *file = getFileByName(path);
    if (file == NULL) return 0;
    if (file->url == NULL) return 0;

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

static int do_rename(const char *src, const char *dst) { //, int flags) {
    printf("Rename from [%s] to [%s]\n", src, dst);

    struct index_s *from = getFileByName(src);
    struct index_s *to = getFileByName(dst);

//    if ((to != NULL) && (flags & RENAME_NOREPLACE)) {
//        return -1;
//    }
//
//    if ((to != NULL) && (flags & RENAME_EXCHANGE)) {
//        free(from->file);
//        from->file = strdup(dst);
//        free(to->file);
//        to->file = strdup(src);
//        return 0;
//    }

    free(from->file);
    from->file = strdup(dst);

    deleteFile(to);

    return 0;
}

static int do_unlink(const char *path) {
    struct index_s *file = getFileByName(path);
    if (file == NULL) return -1;
    deleteFile(file);
    return 0;
}

static int do_create(const char *path, mode_t mode, struct fuse_file_info *finfo) {
    printf("Do create on %s\n", path);
    struct index_s *file = getFileByName(path);
    if (file != NULL) return -1;

    file = (struct index_s *)malloc(sizeof(struct index_s));
    file->file = strdup(path);
    file->url = NULL;
    file->size = 0;
    appendFile(file);
    return 0;
}

static int do_open(const char *path, struct fuse_file_info *fi) {
    struct index_s *file = getFileByName(path);

    printf("Open on %s with %o\n", path, fi->flags);
    if (file == NULL) {
        if ((fi->flags & O_ACCMODE) == O_RDONLY) {
            return -1;
        }
        
        file = (struct index_s *)malloc(sizeof(struct index_s));
        file->file = strdup(path);
        file->url = NULL;
        file->size = 0;
        appendFile(file);
        return 0;
    }
    

    return 0;
    
}

static int do_write(const char *path, const char *data, size_t len, off_t offset, struct fuse_file_info *fi) {
    printf("Write on %s\n", path);
    return 0;
}

static int do_release(const char *path, struct fuse_file_info *fi) {
    printf("Release on %s\n", path);
    return 0;
}

static int do_write_buf(const char *path, struct fuse_bufvec *buf, off_t off, struct fuse_file_info *fi) {
    printf("Write buffer on %s\n", path);
    return 0;
}

static int do_truncate(const char *path, off_t offset) {
    printf("Truncate on %s\n", path);
    return 0;
}

static int do_utimens(const char *path, const struct timespec tv[2]) {
    return 0;
}

static struct fuse_operations operations = {
    .getattr	= do_getattr,
    .readdir	= do_readdir,
    .read	= do_read,
    .listxattr  = do_listxattr,
    .getxattr   = do_getxattr,
    .setxattr   = do_setxattr,
    .rename     = do_rename,
    .unlink     = do_unlink,
    .create     = do_create,
    .open       = do_open,
    .write      = do_write,
    .write_buf  = do_write_buf,
    .release    = do_release,
    .truncate   = do_truncate,
    .utimens    = do_utimens,
};


int main(int argc, char **argv) {
    loadIndex();
    return fuse_main( argc, argv, &operations, NULL );
}
