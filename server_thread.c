#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <time.h>

pthread_mutex_t mutex;

//handle connection
void *handle_connection(void *socket_desc) {
    int sock = *(int*)socket_desc;
    free(socket_desc);

    char buffer[1024] = {0}, filename[1024] = {0};
    struct timespec startTime, endTime;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &startTime);

    read(sock, buffer, sizeof(buffer));
    sscanf(buffer, "GET /%s HTTP", filename);

    FILE *file = fopen(filename, "rb");
    if (!file) {
        char *response = "HTTP/1.1 404 Not Found\n\nFile not found.";
        send(sock, response, strlen(response), 0);
    } else {
        struct stat st;
        fstat(fileno(file), &st);
        int fileLength = st.st_size;
        dprintf(sock, "HTTP/1.1 200 OK\nContent-Length: %d\nConnection: close\nContent-Type: text/html\n\n", fileLength);

        // Sending the file content
        char fileBuffer[1024];
        int bytesRead;
        while ((bytesRead = fread(fileBuffer, 1, sizeof(fileBuffer), file)) > 0) {
            send(sock, fileBuffer, bytesRead, 0);
        }

        fclose(file);

        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &endTime);
        double diff = (endTime.tv_sec - startTime.tv_sec) + (endTime.tv_nsec - startTime.tv_nsec) / 1e9;

        // Synchronize access to log file
        pthread_mutex_lock(&mutex);
        FILE *log = fopen("stats_thread.txt", "a");
        if (log) {
            fprintf(log, "%s\t%d\t%.4f\n", filename, fileLength, diff);
            fclose(log);
        }
        pthread_mutex_unlock(&mutex);
    }

    close(sock);
    return NULL;
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    pthread_mutex_init(&mutex, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(80);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 10);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        int *new_sock = malloc(sizeof(int));
        *new_sock = new_socket;

        pthread_t sniffer_thread;
        if (pthread_create(&sniffer_thread, NULL, handle_connection, (void*) new_sock) < 0) {
            perror("could not create thread");
            return 1;
        }
        pthread_detach(sniffer_thread);
    }

    pthread_mutex_destroy(&mutex);
    return 0;
}
