#define FUSE_USE_VERSION 30

#include <fuse.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>

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
        free(file->url);
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
            if (file->size == -1) {
                getFileSize(file);
            }
            st->st_size = file->size;
        }
        st->st_mode = S_IFREG | 0444;
        st->st_nlink = 1;
    }
    return 0;
}

static int do_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

    filler(buffer, ".", NULL, 0);
    filler(buffer, "..", NULL, 0);

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
    if (!file) {
        return 0;
    }


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

static struct fuse_operations operations = {
    .getattr	= do_getattr,
    .readdir	= do_readdir,
    .read	= do_read,
};


int main(int argc, char **argv) {
    loadIndex();
    return fuse_main( argc, argv, &operations, NULL );
}
