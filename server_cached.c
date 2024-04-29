#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <time.h>

#define MAX_CACHE_SIZE 5

typedef struct {
    char *filename;
    char *content;
    size_t size;
    time_t last_access;
} CacheEntry;

CacheEntry cache[MAX_CACHE_SIZE];
int cacheCount = 0;
pthread_mutex_t cacheMutex;
pthread_mutex_t logMutex;

void initCache() {
    for (int i = 0; i < MAX_CACHE_SIZE; i++) {
        cache[i].filename = NULL;
        cache[i].content = NULL;
        cache[i].size = 0;
        cache[i].last_access = 0;
    }
}

void freeCache() {
    for (int i = 0; i < MAX_CACHE_SIZE; i++) {
        free(cache[i].filename);
        free(cache[i].content);
    }
}

CacheEntry* findCacheEntry(const char *filename) {
    for (int i = 0; i < MAX_CACHE_SIZE; i++) {
        if (cache[i].filename && strcmp(cache[i].filename, filename) == 0) {
            cache[i].last_access = time(NULL);
            return &cache[i];
        }
    }
    return NULL;
}

void addToCache(const char *filename, const char *content, size_t size) {
    pthread_mutex_lock(&cacheMutex);

    //find least recently used entry
    int lruIndex = 0;
    time_t oldestAccess = time(NULL);
    for (int i = 0; i < MAX_CACHE_SIZE; i++) {
        if (cache[i].last_access < oldestAccess) {
            oldestAccess = cache[i].last_access;
            lruIndex = i;
        }
    }

    free(cache[lruIndex].filename);
    free(cache[lruIndex].content);
    cache[lruIndex].filename = strdup(filename);
    cache[lruIndex].content = strdup(content); // For simplicity, assuming text content
    cache[lruIndex].size = size;
    cache[lruIndex].last_access = time(NULL);

    pthread_mutex_unlock(&cacheMutex);
}

void logRequest(const char* filename, int size, double elapsed) {
    pthread_mutex_lock(&logMutex);
    FILE* logFile = fopen("stats_cached.txt", "a");
    if (logFile) {
        fprintf(logFile, "%s\t%d\t%.4lf\n", filename, size, elapsed);
        fclose(logFile);
    }
    pthread_mutex_unlock(&logMutex);
}

//handle each connection
void *handle_connection(void *socket_desc) {
    int sock = *(int *)socket_desc;
    free(socket_desc);

    char buffer[1024] = {0}, filename[1024] = {0}, response[1024];
    struct timespec startTime, endTime;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &startTime);

    read(sock, buffer, sizeof(buffer)); // Simplified; should check return value
    sscanf(buffer, "GET /%s HTTP", filename);

    pthread_mutex_lock(&cacheMutex);
    CacheEntry *entry = findCacheEntry(filename);
    pthread_mutex_unlock(&cacheMutex);

    int servedSize = 0;

    if (entry) {
        sprintf(response, "HTTP/1.1 200 OK\nContent-Length: %zu\nConnection: close\nContent-Type: text/html\n\n", entry->size);
        send(sock, response, strlen(response), 0);
        send(sock, entry->content, entry->size, 0);
        servedSize = entry->size;
    } else {
        FILE *file = fopen(filename, "rb");
        if (!file) {
            char *notFound = "HTTP/1.1 404 Not Found\n\n";
            send(sock, notFound, strlen(notFound), 0);
        } else {
            struct stat st;
            fstat(fileno(file), &st);
            char *fileContent = malloc(st.st_size);
            fread(fileContent, 1, st.st_size, file);
            addToCache(filename, fileContent, st.st_size);

            sprintf(response, "HTTP/1.1 200 OK\nContent-Length: %ld\nConnection: close\nContent-Type: text/html\n\n", st.st_size);
            send(sock, response, strlen(response), 0);
            send(sock, fileContent, st.st_size, 0);
            servedSize = st.st_size;

            free(fileContent);
            fclose(file);
        }
    }

    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &endTime);
    double diff = (endTime.tv_sec - startTime.tv_sec) + (endTime.tv_nsec - startTime.tv_nsec) / 1e9;
    
    logRequest(filename, servedSize, diff);

    close(sock);
    return NULL;
}

int main() {
    int server_fd, client_sock;
    struct sockaddr_in server_addr;
    int addr_len = sizeof(server_addr);

    pthread_mutex_init(&cacheMutex, NULL);
    pthread_mutex_init(&logMutex, NULL);
    initCache();

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(80);

    bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    listen(server_fd, 10);

    while (1) {
        client_sock = accept(server_fd, (struct sockaddr *)&server_addr, (socklen_t *)&addr_len);
        if (client_sock < 0) {
            perror("accept failed");
            continue;
        }

        int *new_sock = malloc(sizeof(int));
        *new_sock = client_sock;

        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_connection, (void *)new_sock) < 0) {
            perror("could not create thread");
            return 1;
        }
        pthread_detach(thread_id);
    }

    freeCache();
    pthread_mutex_destroy(&cacheMutex);
    pthread_mutex_destroy(&logMutex);
    close(server_fd);

    return 0;
}
