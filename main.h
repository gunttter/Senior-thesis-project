#ifndef _HTTP_H
#define _HTTP_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>

typedef struct
{
    char *method;
    char *path;
    char *body;
    int body_fd;
} http_request_t;

typedef struct
{
    int status_code;
    char *content_type;
    char *body;
    size_t body_length;
    char *headers;
} http_response_t;

typedef struct
{
    int status_code;
    char *content_type;
    char *body;
} http_response_writer_t;

typedef void (*http_handler_t)(http_request_t *, http_response_t *);

typedef struct
{
    pthread_t *threads;
    int thread_count;
    http_handler_t handler;
    pthread_mutex_t mutex;
} thread_pool_t;

thread_pool_t thread_pool;

int server_fd;

void handle_request(int client_fd, http_handler_t handler)
{
    char buffer[1024];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));

    if (bytes_read < 0)
    {
        perror("Failed to read from client socket");
        close(client_fd);
        return;
    }

    printf("\nRequest:\n%s\n", buffer);

    char *request_buffer = strdup(buffer);

    char *request_line = strtok(request_buffer, "\r\n");
    char *method = strtok(request_line, " ");
    char *path = strtok(NULL, " ");

    char *header_line;
    int content_length = 0;
    while ((header_line = strtok(NULL, "\r\n")) != NULL)
    {

        if (strncmp(header_line, "Content-Length:", strlen("Content-Length:")) == 0)
        {
            content_length = atoi(header_line + strlen("Content-Length:"));
            printf("Content Length: %d\n", content_length);
        }
    }

    http_request_t request;
    request.method = strdup(method);
    request.path = strdup(path);
    request.body = NULL;
    request.body_fd = -1;

    if ((strcmp(request.method, "POST") == 0 || strcmp(request.method, "PUT") == 0 || strcmp(request.method, "PATCH") == 0))
    {
        char *body = malloc(content_length + 1);
        ssize_t total_bytes_read = 0;

        while (total_bytes_read < content_length)
        {
            ssize_t remaining_bytes = content_length - total_bytes_read;
            ssize_t body_bytes_read = read(client_fd, body + total_bytes_read, remaining_bytes);
            if (body_bytes_read < 0)
            {
                perror("Failed to read request body");
                close(client_fd);
                free(body);
                free(request.method);
                free(request.path);
                return;
            }

            total_bytes_read += body_bytes_read;
        }

        body[content_length] = '\0';
        request.body = body;
    }

    http_response_t response;
    handler(&request, &response);

    ssize_t headers_sent = send(client_fd, response.headers, strlen(response.headers), 0);
    if (headers_sent < 0)
    {
        perror("Failed to send response headers");
        close(client_fd);
        free(request.method);
        free(request.path);
        free(request.body);
        return;
    }

    ssize_t body_sent = send(client_fd, response.body, response.body_length, 0);
    if (body_sent < 0)
    {
        perror("Failed to send response body");
        close(client_fd);
        free(request.method);
        free(request.path);
        free(request.body);
        return;
    }

    free(request.method);
    free(request.path);
    if (request.body != NULL)
    {
        free(request.body);
    }
}

void *thread_function(void *arg)
{
    http_handler_t handler = thread_pool.handler;
    int client_fd = *(int *)arg;
    free(arg);

    handle_request(client_fd, handler);
    close(client_fd);

    pthread_mutex_lock(&thread_pool.mutex);
    thread_pool.thread_count--;
    pthread_mutex_unlock(&thread_pool.mutex);

    pthread_exit(NULL);
}

void sigint_handler(int sig)
{
    printf("\nReceived signal %d, shutting down server...\n", sig);
    close(server_fd);
    exit(0);
}

void init_thread_pool(int thread_count, http_handler_t handler)
{
    pthread_mutex_init(&thread_pool.mutex, NULL);
    thread_pool.thread_count = thread_count;
    thread_pool.handler = handler;
    thread_pool.threads = malloc(thread_count * sizeof(pthread_t));
}

void destroy_thread_pool()
{
    pthread_mutex_destroy(&thread_pool.mutex);
    free(thread_pool.threads);
}

void listen_and_serve(char *port, int thread_count, http_handler_t handler)
{
    init_thread_pool(thread_count, handler);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("Failed to create a server socket");
        exit(EXIT_FAILURE);
    }

    const int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) == -1)
    {
        perror("Failed to make port reusable");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(atoi(port));
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("Port is binned");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 10) < 0)
    {
        perror("Failed to start listening for connections");
        exit(EXIT_FAILURE);
    }
    printf("Listening on port %s...\n", port);

    signal(SIGINT, sigint_handler);

    while (1)
    {
        struct sockaddr_in client_addr = {0};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0)
        {
            perror("Failed to accept a new connection");
            exit(EXIT_FAILURE);
        }

        int *arg = malloc(sizeof(int));
        *arg = client_fd;

        pthread_mutex_lock(&thread_pool.mutex);
        thread_pool.thread_count++;
        pthread_mutex_unlock(&thread_pool.mutex);

        pthread_create(&thread_pool.threads[thread_pool.thread_count - 1], NULL, thread_function, arg);
        pthread_detach(thread_pool.threads[thread_pool.thread_count - 1]);
    }

    destroy_thread_pool();
}
void write_response(int status_code, char *content_type, char *body, http_response_t *response)
{
    response->status_code = status_code;
    response->content_type = content_type;
    response->body = strdup(body);
    response->body_length = strlen(body);

    char *status_message;
    switch (status_code)
    {
    case 200:
        status_message = "OK";
        break;
    case 404:
        status_message = "Not Found";
        break;
    case 500:
        status_message = "Internal Server Error";
        break;
    default:
        status_message = "Unknown";
        break;
    }

    char headers[512]; // Preallocate buffer for headers
    int headers_length = snprintf(headers, sizeof(headers), "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %lu\r\n", status_code, status_message, content_type, response->body_length);

    response->headers = malloc(headers_length + 1);
    strncpy(response->headers, headers, headers_length + 1);
    printf("Response:\n%s\n", headers);
    printf("%s\n", response->body);

    free(response->body);
}
#endif