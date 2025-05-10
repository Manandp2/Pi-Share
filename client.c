/**
 * nonstop_networking
 * CS 341 - Spring 2025
 */
#include <ctype.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "common.h"
#include "format.h"

char** parse_args(int argc, char** argv);
verb check_args(char** args);
size_t write_all_to_server(int sock, const void* data, size_t size);
size_t read_all_from_server(int sock, void* buffer, size_t size);
ssize_t read_line_from_server(int sock, char* buffer, size_t size);
int connect_to_server(int sock, char* ip_addr, char* port);
bool parse_header(int sock);
size_t get_size(int sock);
ssize_t check_for_extra_data(int sock);
void get(int sock, char** args);
void put(int sock, char** args);
void delete(int sock, char** args);
void list(int sock);

int main(const int argc, char** argv) {
    char** args = parse_args(argc, argv);
    const verb action = check_args(args);
    // If there is a valid action, we need to connect for all possible cases
    struct addrinfo hints = {0}, *res;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    // ReSharper disable once CppDFANullDereference
    const int status = getaddrinfo(args[0], args[1], &hints, &res);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        exit(1);
    }

    const int sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == -1) {
        perror("socket() failed");
        freeaddrinfo(res);
        exit(1);
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) == -1) {
        perror("connect() failed");
        close(sock);
        freeaddrinfo(res);
        exit(1);
    }
    freeaddrinfo(res);
    // Now we are connected to the server

    switch (action) {
    case GET:
        get(sock, args);
        break;
    case PUT:
        put(sock, args);
        break;
    case DELETE:
        delete(sock, args);
        break;
    case LIST:
        list(sock);
        break;
    case V_UNKNOWN:
        break;
    }
    free(args);
}

/**
 * @brief Given commandline argc and argv, parses argv.
 *
 * @param argc argc from main()
 * @param argv argv from main()
 *
 * @returns char* array in the form of {host, port, method, remote, local, NULL}
 * where `method` is ALL CAPS
 */
char** parse_args(const int argc, char** argv) {
    if (argc < 3) {
        return NULL;
    }

    char* host = strtok(argv[1], ":");
    char* port = strtok(NULL, ":");
    if (port == NULL) {
        return NULL;
    }

    char** args = calloc(1, 6 * sizeof(char*));
    args[0] = host;
    args[1] = port;
    args[2] = argv[2];
    char* temp = args[2];
    while (*temp) {
        *temp = toupper((unsigned char)*temp);
        temp++;
    }
    if (argc > 3) {
        args[3] = argv[3];
    }
    if (argc > 4) {
        args[4] = argv[4];
    }

    return args;
}

/**
 * @brief Validates args to program.  If `args` are not valid, help information for the
 * program is printed.
 *
 * @param args    arguments to parse
 *
 * @returns a verb which corresponds to the request method
 */
verb check_args(char** args) {
    if (args == NULL) {
        print_client_usage();
        exit(1);
    }

    const char* command = args[2];

    if (strcmp(command, "LIST") == 0) {
        return LIST;
    }

    if (strcmp(command, "GET") == 0) {
        if (args[3] != NULL && args[4] != NULL) {
            return GET;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "DELETE") == 0) {
        if (args[3] != NULL) {
            return DELETE;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "PUT") == 0) {
        if (args[3] == NULL || args[4] == NULL) {
            print_client_help();
            exit(1);
        }
        return PUT;
    }

    // Not a valid Method
    print_client_help();
    exit(1);
}

/**
 * @brief writes size bytes of data to the server specified by sock
 * @param sock file descriptor of the server
 * @param data data to send to server
 * @param size number of bytes in data
 * @return true if write completes, false if write fails before size bytes are sent to server
 */
size_t write_all_to_server(const int sock, const void* data, const size_t size) {
    size_t bytes_written = 0;
    while (bytes_written < size) {
        const ssize_t cur_written = write(sock, data + bytes_written, size - bytes_written);
        if (cur_written == -1) {
            break;
        }
        bytes_written += cur_written;
    }
    return bytes_written;
}

/**
 * @brief Reads a specified number of bytes from a server socket into a buffer.
 *
 * @param sock The socket file descriptor to read from.
 * @param buffer Pointer to the buffer where data will be stored.
 * @param size The number of bytes to read.
 *
 * @returns The actual number of bytes read. It may be less than the requested size
 * if an end-of-stream or error occurs during reading.
 */
size_t read_all_from_server(const int sock, void* buffer, const size_t size) {
    size_t bytes_read = 0;
    while (bytes_read < size) {
        const ssize_t cur_read = read(sock, buffer + bytes_read, size - bytes_read);
        if (cur_read == -1 || cur_read == 0) {
            break;
        }
        bytes_read += cur_read;
    }
    return bytes_read;
}

ssize_t read_line_from_server(int sock, char* buffer, size_t size) {
    size_t idx = 0;

    while (idx < size - 1) {
        if (read(sock, buffer + idx, 1) <= 0) return -1;
        if (buffer[idx] == '\n') break;
        ++idx;
    }
    buffer[idx] = '\0';
    return (ssize_t)idx;
}

int connect_to_server(int sock, char* ip_addr, char* port) {
    struct addrinfo hints = {0}, *res;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    const int new_status = getaddrinfo(ip_addr, port, &hints, &res);
    if (new_status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(new_status));
        exit(1);
    }

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == -1) {
        perror("socket() failed");
        freeaddrinfo(res);
        exit(1);
    }

    if (connect(sock, res->ai_addr, res->ai_addrlen) == -1) {
        perror("connect() failed");
        close(sock);
        freeaddrinfo(res);
        exit(1);
    }
    freeaddrinfo(res);
    return sock;
}

/**
 * @brief Parses the server response header from the given socket.
 *
 * @param sock The socket file descriptor from which the server response header is read.
 *
 * @return Returns true if the header is successfully parsed and matches "OK\n", false otherwise.
 * If the header indicates an error, it additionally reads and prints the complete error message.
 */
bool parse_header(const int sock) {
    char* header = malloc(max_server_response_header_size + 1); // need one extra byte for '\0'
    if (read_all_from_server(sock, header, min_server_response_header_size) != min_server_response_header_size) {
        free(header);
        return false;
    }
    if (strncmp(header, "OK\n", min_server_response_header_size) != 0) {
        // There was an error, so read in the entire ERROR string
        if (read_all_from_server(sock, header + min_server_response_header_size,
                                 max_server_response_header_size - min_server_response_header_size) !=
            max_server_response_header_size - min_server_response_header_size) {
            free(header);
            return false;
        }
        // Read the entire error message, which we don't know the size of
        size_t buffer_size = 16;
        header = realloc(header, buffer_size);
        // char* msg = malloc(buffer_size);
        size_t total_read = max_server_response_header_size;
        size_t cur_read = 0;
        while ((cur_read = read(sock, header + total_read, buffer_size - total_read)) != 0 && (ssize_t)cur_read != -1) {
            total_read += cur_read;
            if (total_read == buffer_size) {
                buffer_size *= 2;
                header = realloc(header, buffer_size);
            }
        }
        if (total_read == buffer_size) {
            header = realloc(header, buffer_size + 1);
            header[buffer_size] = '\0';
        }
        print_error_message(header);
        free(header);
        return false;
    }
    header[min_server_response_header_size] = '\0';
    printf("%s", header);
    free(header);
    return true;
}

/**
 * @brief Gets [size] from the server, to be used after receiving an OK from the server
 * @param sock file descriptor of the server
 * @return the size of the following payload returned by the server
 */
size_t get_size(const int sock) {
    size_t size = 0;
    if (read_all_from_server(sock, &size, sizeof(size)) != sizeof(size)) exit(1);
    return size;
}

ssize_t check_for_extra_data(const int sock) {
    const int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL) failed");
        exit(1);
    }
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL) failed");
        exit(1);
    }
    ssize_t extra_data = 0;
    ssize_t cur_read = 0;
    char buffer[1024];
    while ((cur_read = read(sock, buffer, 1024)) != 0 && cur_read != -1) {
        extra_data += cur_read;
    }

    if (fcntl(sock, F_SETFL, flags) == -1) {
        perror("fcntl(F_SETFL) failed");
        exit(1);
    }
    return extra_data;
}

/**
 * @brief sends a GET request to the server specified by sock
 * @param sock file descriptor of the server
 * @param args list of arguments from parse_args
 */
void get(int sock, char** args) {
    const char* remote_file = args[3];
    const char* local_file = args[4];
    char* header_msg;
    asprintf(&header_msg, "GET %s\n", remote_file);
    const size_t header_msg_len = strlen(header_msg);

    char ip_addr[32] = {0};
    char port[32] = {0};
    do {
        if (write_all_to_server(sock, header_msg, header_msg_len) != header_msg_len) {
            free(header_msg);
            exit(1);
        }
        shutdown(sock, SHUT_WR);

        if (parse_header(sock)) {
            // Now, we get <ip addr:str>\n<port:str>\n
            read_line_from_server(sock, ip_addr, 64);
            read_line_from_server(sock, port, 64);
            if (strcmp(ip_addr, "0.0.0.0") != 0) {
                // We need to reconnect to the new server and resend the request
                shutdown(sock, SHUT_RD);
                close(sock);
                sock = connect_to_server(sock, ip_addr, port);
                continue;
            }
            // read the size
            const size_t file_size = get_size(sock);
            const int local_fd = open(local_file, O_WRONLY | O_CREAT | O_TRUNC, 0777);
            char buffer[1024];
            size_t cur_read = 0;
            size_t total_read = 0;
            while ((cur_read = read_all_from_server(sock, buffer, 1024)) != 0) {
                total_read += cur_read;
                if (total_read > file_size) {
                    print_received_too_much_data();
                    exit(1);
                }
                write(local_fd, buffer, cur_read);
            }
            if (total_read < file_size) {
                print_too_little_data();
            } else if (check_for_extra_data(sock) != 0) {
                print_received_too_much_data();
            }
            shutdown(sock, SHUT_RD);
        }
    } while (strcmp(ip_addr, "0.0.0.0") != 0);

    free(header_msg);
}

/**
 * @brief sends a PUT request to the server specified by sock
 * @param sock file descriptor of the server
 * @param args list of arguments from parse_args
 */
void put(const int sock, char** args) {
    const char* local_file = args[4];
    const char* remote_file = args[3];
    const int fd = open(local_file, O_RDONLY);
    if (fd == -1) {
        perror("file does not exist");
        exit(1);
    }
    // First, write the PUT request and the remote file name
    char* header_msg;
    asprintf(&header_msg, "PUT %s\n", remote_file);
    const size_t header_msg_len = strlen(header_msg);
    if (write_all_to_server(sock, header_msg, header_msg_len) != header_msg_len) {
        free(header_msg);
        exit(1);
    }
    free(header_msg);

    // Next, write file size
    struct stat file_stat;
    fstat(fd, &file_stat);
    const size_t file_size = file_stat.st_size;
    if (write_all_to_server(sock, &file_size, sizeof(file_size)) != sizeof(file_size)) {
        exit(1);
    }

    // Now, we need to write the actual file to the server
    char* buffer[1024];
    ssize_t read_result = 0;
    do {
        read_result = read(fd, buffer, 1024);
        if (read_result != 0 && read_result != -1) {
            write_all_to_server(sock, buffer, read_result);
        }
    } while (read_result != 0 && read_result != -1);

    shutdown(sock, SHUT_WR);
    if (parse_header(sock)) {
        print_success();
    }
    shutdown(sock, SHUT_RD);
}

/**
 * @brief sends a DELETE request to the server specified by sock
 * @param sock file descriptor of the server
 * @param args list of arguments from parse_args
 */
void delete(const int sock, char** args) {
    const char* remote_file = args[3];
    char* header_msg;
    asprintf(&header_msg, "DELETE %s\n", remote_file);
    const size_t header_msg_len = strlen(header_msg);
    if (write_all_to_server(sock, header_msg, header_msg_len) != header_msg_len) {
        free(header_msg);
        exit(1);
    }
    shutdown(sock, SHUT_WR);
    free(header_msg);
    if (parse_header(sock)) {
        print_success();
    }
    shutdown(sock, SHUT_RD);
}

/**
 * @brief sends a LIST request to the server specified by sock
 * @param sock file descriptor of the server
 */
void list(const int sock) {
    if (write_all_to_server(sock, "LIST\n", list_request_size) != list_request_size) {
        print_connection_closed();
        exit(1);
    }
    shutdown(sock, SHUT_WR);
    if (parse_header(sock)) { // The server responded properly, now we need to read the list
        const size_t size = get_size(sock);
        char* list = malloc(size + 1);
        if (read_all_from_server(sock, list, size) != size) {
            print_too_little_data();
            print_connection_closed();
            exit(1);
        }
        list[size] = '\0';
        printf("%s", list);
        const ssize_t extra_data = check_for_extra_data(sock);
        if (extra_data != 0) {
            print_received_too_much_data();
        }
        free(list);
    }
    shutdown(sock, SHUT_RD);
}
