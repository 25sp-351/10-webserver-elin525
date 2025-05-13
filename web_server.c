#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 1024
#define STATIC_DIR "static/"

// define a mutex lock for printing
pthread_mutex_t print_lock;

// define a structure to hold client connection information
typedef struct {
    int client_fd;
} client_connection_t;

// send HTTP response
void send_http_response(int client_fd, const char *content_type, const char *body, int body_length) {
    char header[BUFFER_SIZE];
    int header_length = snprintf(header, sizeof(header),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        content_type, body_length);
    
    // send header
    write(client_fd, header, header_length);
    // then send body
    write(client_fd, body, body_length);
}

// if the requested file is not found, send a 404 response
void send_not_found(int client_fd) {
    const char *body = "<h1>404 Not Found</h1>";
    send_http_response(client_fd, "text/html", body, strlen(body));
}

// handle static file requests depending on the path
void serve_static_file(int client_fd, const char *path) {
    char filepath[BUFFER_SIZE];
    snprintf(filepath, sizeof(filepath), "%s%s", STATIC_DIR, path + strlen("/static"));

    int file_fd = open(filepath, O_RDONLY);
    if (file_fd < 0) {
        send_not_found(client_fd);
        return;
    }

    // get file size
    struct stat file_stat;
    fstat(file_fd, &file_stat);
    int file_size = file_stat.st_size;

    // read file content
    char *file_content = malloc(file_size);
    read(file_fd, file_content, file_size);
    close(file_fd);

    // determine content type based on file extension
    // default to application/octet-stream if unknown
    const char *content_type = "application/octet-stream";

    // make sure the path is correct before you run the code, eg: static/images/rex.png
    if (strstr(filepath, ".png")) {
        content_type = "images/png";
    } else if (strstr(filepath, ".html")) {
        content_type = "txt/html";
    }

    send_http_response(client_fd, content_type, file_content, file_size);
    free(file_content);
}

// handle calculation requests
void serve_calc(int client_fd, const char *path) {
    int num1, num2;
    char operator[10];

    if (sscanf(path, "/calc/%9[^/]/%d/%d", operator, &num1, &num2) == 3) {
        char response_body[BUFFER_SIZE];
        int result = 0;
        if (strcmp(operator, "add") == 0) {
            result = num1 + num2;
        } else if (strcmp(operator, "sub") == 0) {
            result = num1 - num2;
        }else if (strcmp(operator, "mul") == 0) {
            result = num1 * num2;
        } else if (strcmp(operator, "div") == 0) {
            if (num2 == 0) {
                snprintf(response_body, sizeof(response_body), "Error: Division by zero");
                send_http_response(client_fd, "text/plain", response_body, strlen(response_body));
                return;
            }
            result = num1 / num2;
        } 
        snprintf(response_body, sizeof(response_body), "Result: %d\n", result);
        send_http_response(client_fd, "text/plain", response_body, strlen(response_body));
    } else {
        send_not_found(client_fd);
    }
}

// handle sleep requests
void serve_sleep(int client_fd, const char *path) {
    int seconds;
    if (sscanf(path, "/sleep/%d", &seconds) == 1) {
        sleep(seconds);
        char response_body[BUFFER_SIZE];
        snprintf(response_body, sizeof(response_body), "Sleep time was %d seconds", seconds);
        send_http_response(client_fd, "text/plain", response_body, strlen(response_body));
    } else {
        send_not_found(client_fd);
    }
}

// parse HTTP request and route to appropriate handler
void parse_http_request(int client_fd, const char *request) {
    char method[10], path[BUFFER_SIZE];
    sscanf(request, "%s %s", method, path);

    if (strcmp(method, "GET") != 0) {
        send_not_found(client_fd);
        return;
    }

    // route to appropriate handler based on path
    if (strncmp(path, "/static", strlen("/static")) == 0) {
        serve_static_file(client_fd, path);
    } else if (strncmp(path, "/calc", strlen("/calc")) == 0) {
        serve_calc(client_fd, path);
    } else if (strncmp(path, "/sleep", strlen("/sleep")) == 0) {
        serve_sleep(client_fd, path);
    } else {
        send_not_found(client_fd);
    }
}

// each thread handles a client connection
void *handle_client(void *arg) {
    client_connection_t *conn = (client_connection_t *)arg;
    char buffer[BUFFER_SIZE];

    // loop to read requests from the client, until the client closes the connection
    while (1) {
        ssize_t read_info = read(conn->client_fd, buffer, sizeof(buffer) - 1);
        if (read_info <= 0) {
            break;
        }
        buffer[read_info] = '\0';

        pthread_mutex_lock(&print_lock);
        printf("Received Request:\n%s\n", buffer);
        pthread_mutex_unlock(&print_lock);

        // handle the request
        parse_http_request(conn->client_fd, buffer);
    }

    close(conn->client_fd);
    free(conn);
    return NULL;
}

// start the server and listen for incoming connections
int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;

    if (argc == 3 && strcmp(argv[1], "-p") == 0) {
        port = atoi(argv[2]);
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket error");
        exit(EXIT_FAILURE);
    }

    int enable_reuse_addr = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &enable_reuse_addr, sizeof(enable_reuse_addr));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        close(server_fd);
        return 1;
    }

    pthread_mutex_init(&print_lock, NULL);
    printf("Server listening on port %d...\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (client_fd < 0) {
            perror("accept failed");
            continue;
        }

        // create a new client connection structure and pass it to the thread
        client_connection_t *connect_client_info = malloc(sizeof(client_connection_t));
        connect_client_info->client_fd = client_fd;

        pthread_t client_thread;
        pthread_create(&client_thread, NULL, handle_client, connect_client_info);
        pthread_detach(client_thread);
    }

    close(server_fd);
    pthread_mutex_destroy(&print_lock);
    return 0;
}
