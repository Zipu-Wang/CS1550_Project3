#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <time.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <fcntl.h>

typedef struct {
    sem_t *sem;
    int shm_fd;
} shared_data;

//handle each connection
void handle_connection(int connfd) {
    char buffer[1024] = {0};
    char filename[1024] = {0};
    struct timespec startTime, endTime;
    float diff;

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &startTime);

    recv(connfd, buffer, sizeof(buffer), 0);
    sscanf(buffer, "GET /%s", filename);

    FILE *file = fopen(filename, "rb");
    if (!file) {
        char *notFound = "HTTP/1.1 404 Not Found\n\n";
        send(connfd, notFound, strlen(notFound), 0);
    } else {
        //prepare and send headers
        struct stat st;
        fstat(fileno(file), &st);
        int fileLength = st.st_size;
        dprintf(connfd, "HTTP/1.1 200 OK\nContent-Length: %d\nConnection: close\nContent-Type: text/html\n\n", fileLength);

        //send file content
        char fileBuffer[1024];
        int bytes_read;
        while ((bytes_read = fread(fileBuffer, 1, sizeof(fileBuffer), file)) > 0) {
            send(connfd, fileBuffer, bytes_read, 0);
        }

        fclose(file);

        clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &endTime);
        diff = (endTime.tv_sec - startTime.tv_sec) + (endTime.tv_nsec - startTime.tv_nsec) / 1e9f;

        //logging
        sem_t *sem = sem_open("sem_stats", 0);
        FILE *log = fopen("stats_proc.txt", "a");
        if (log) {
            fprintf(log, "%s\t%d\t%.4f\n", filename, fileLength, diff);
            fclose(log);
        }
        sem_post(sem);
    }

    close(connfd);
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(80);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    //shared semaphore setup
    sem_t *sem = sem_open("sem_stats", O_CREAT, 0660, 1);
    if (sem == SEM_FAILED) {
        perror("sem_open failed");
        exit(EXIT_FAILURE);
    }

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        if (new_socket < 0) {
            perror("accept");
            continue;
        }
        if (fork() == 0) {
            close(server_fd);
            handle_connection(new_socket);
            exit(0);
        }
        close(new_socket);
    }

    sem_close(sem);
    sem_unlink("sem_stats");

    return 0;
}
