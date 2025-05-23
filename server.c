/**
 * nonstop_networking
 * CS 341 - Spring 2025
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <bits/socket.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "common.h"
#include "format.h"
#include "includes/dictionary.h"

typedef struct {
    enum {
        READING_VERB,
        READING_HEADER,
        HANDLING_VERB,
        DONE,
        INVALID_VERB,
        INVALID_FILE,
        INCORRECT_DATA_AMOUNT
    } state;

    int sock;
    int local_file;
    verb action;
    ssize_t local_file_pos;
    ssize_t buffer_position;
    char header[1024];
    size_t file_size;
    bool size_read;
} client_info;

typedef struct {
    char ip[INET_ADDRSTRLEN]; // IPv4 address
    char port[6]; // Port as string (max 65535 + null)
} server_info;

void* server_info_copy_constructor(void* p) {
    server_info* copy = malloc(sizeof(server_info));
    strcpy(copy->ip, ((server_info*)p)->ip);
    strcpy(copy->port, ((server_info*)p)->port);
    return copy;
}

void* server_info_default_constructor() {
    return calloc(1, sizeof(server_info));
}

static dictionary* file_to_server;
static vector* mini_servers;
// List of all sub-servers for round-robin PUT
static size_t current_server_index = 0;


#define MAX_EVENTS 1000
static bool run_server = true;
static vector* files;

static void handler(int signum) {
    if (signum == SIGINT) {
        run_server = false;
    }
}

void set_nonblocking(int fd);
ssize_t read_n_from_client(client_info* client, void* buf, ssize_t n);
ssize_t write_n_to_client(const client_info* client, const void* buf, ssize_t n);
verb parse_verb(client_info* client);
void read_file_name(client_info* client);
void get(client_info* client);
void put(client_info* client);
void delete(client_info* client);
void list(client_info* client);
void add_server(client_info* client);
void send_ok_msg_to_client(const client_info* client);
void send_error_msg_to_client(const client_info* client);
void send_invalid_req_msg_to_client(const client_info* client);
void send_invalid_file_to_client(const client_info* client);
void send_incorrect_data_msg_to_client(const client_info* client);
void close_client_connection(const client_info* client);
void* client_info_copy_constructor(void* p);

ssize_t read_line_from_client(client_info* client, char* buffer, size_t size) {
    size_t idx = 0;

    while (idx < size - 1) {
        if (read(client->sock, buffer + idx, 1) <= 0) return -1;
        if (buffer[idx] == '\n') break;
        ++idx;
    }
    buffer[idx] = '\0';
    return (ssize_t)idx;
}

int main(int argc, char** argv) {
    file_to_server = dictionary_create(string_hash_function, string_compare, string_copy_constructor,
                                       free, server_info_copy_constructor,
                                       free); // Maps file name -> server_info*
    mini_servers = vector_create(server_info_copy_constructor, free, server_info_default_constructor);

    if (argc < 2) {
        print_server_usage();
        exit(1);
    }

    struct sigaction sa;

    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("sigaction() failed");
        exit(1);
    }
    if (sigaction(SIGPIPE, &sa, NULL) == -1) {
        perror("sigaction() failed");
        exit(1);
    }

    struct addrinfo hints = {0}, *res;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    const int status = getaddrinfo(NULL, argv[1], &hints, &res);
    if (status != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
    }

    const int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("socket() failed");
        freeaddrinfo(res);
        exit(1);
    }

    int opt = 1;
    if (setsockopt(sock, SOL_SOCKET,SO_REUSEADDR, &opt, sizeof(opt)) == -1 ||
        setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
        perror("setsockopt() failed");
        exit(1);
    }

    if (bind(sock, res->ai_addr, res->ai_addrlen) != 0) {
        perror("bind() failed");
        freeaddrinfo(res);
        exit(1);
    }

    freeaddrinfo(res);

    if (listen(sock, 10) != 0) {
        perror("listen() failed");
        exit(1);
    }


    const int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1() failed");
        exit(1);
    }

    /* ev contains the settings that we want to send to epoll */
    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sock, &ev) == -1) {
        perror("epoll_ctl() failed: server sock");
        exit(1);
    }

    dictionary* client_dictionary = dictionary_create(int_hash_function, int_compare, int_copy_constructor,
                                                      int_destructor, client_info_copy_constructor, free);

    files = string_vector_create();
    char* orig_dir = get_current_dir_name();
    char pi_share_dir[9] = "Pi-Share";
    if (mkdir(pi_share_dir, 0777) == -1 && errno != EEXIST) {
        perror("mkdir() failed");
        exit(1);
    }
    print_temp_directory(pi_share_dir);

    // Pi share scraping code
    struct dirent* entry;
    DIR* dir = opendir(pi_share_dir);
    if (dir == NULL) {
        perror("opendir() failed");
        exit(1);
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        struct stat entry_stat;
        char full_path[1024];
        snprintf(full_path, sizeof(full_path), "%s/%s", pi_share_dir, entry->d_name);
        if (stat(full_path, &entry_stat) == 0 && S_ISREG(entry_stat.st_mode)) {
            vector_push_back(files, entry->d_name);
        }
    }
    closedir(dir);
    // End of scraping code

    chdir(pi_share_dir);
    // ReSharper disable once CppDFALoopConditionNotUpdated
    while (run_server) {
        const int num_fds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (num_fds == -1) {
            perror("epoll_wait() failed");
        }
        for (int i = 0; i < num_fds; ++i) {
            if (events[i].data.fd == sock) { /* There is a new connection */
                struct sockaddr_in addr = {0};
                socklen_t addrlen = sizeof(addr);
                int client = accept(sock, (struct sockaddr*)&addr, &addrlen);
                if (client == -1) {
                    perror("accept() failed");
                }
                set_nonblocking(client);
                ev.events = EPOLLIN | EPOLLOUT; // | EPOLLET;
                ev.data.fd = client;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client, &ev) == -1) {
                    perror("epoll_ctl() failed: client sock");
                    exit(1);
                }
                client_info info = {READING_VERB, client, 0, V_UNKNOWN, 0, 0, {0}, 0,false};
                dictionary_set(client_dictionary, &client, &info);
            } else {
                int client = events[i].data.fd;
                client_info* info = dictionary_get(client_dictionary, &client);
                switch (info->state) {
                case READING_VERB:
                    info->action = parse_verb(info);
                    break;
                case READING_HEADER:
                    read_file_name(info);
                    break;
                case HANDLING_VERB: {
                    switch (info->action) {
                    case GET:
                        get(info);
                        break;
                    case PUT:
                        put(info);
                        break;
                    case DELETE:
                        delete(info);
                        break;
                    case LIST:
                        list(info);
                        break;
                    case V_UNKNOWN:
                        break;
                    case ADD_SERVER:
                        add_server(info);
                        break;
                    }
                    break;
                }
                default: /* Not possible to reach the DONE or ERROR state here */
                    break;
                }
                if (info->state == DONE) {
                    close_client_connection(info);
                    dictionary_remove(client_dictionary, &client);
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client, NULL) == -1) {
                        perror("epoll_ctl() failed: removing client sock");
                        exit(1);
                    }
                } else if (info->state == INVALID_VERB) {
                    /* There could have been an error when handling something else */
                    send_error_msg_to_client(info);
                    send_invalid_req_msg_to_client(info);
                    close_client_connection(info);
                    dictionary_remove(client_dictionary, &client);
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client, NULL) == -1) {
                        perror("epoll_ctl() failed: removing client sock");
                        exit(1);
                    }
                } else if (info->state == INVALID_FILE) {
                    send_error_msg_to_client(info);
                    send_invalid_file_to_client(info);
                    close_client_connection(info);
                    dictionary_remove(client_dictionary, &client);
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client, NULL) == -1) {
                        perror("epoll_ctl() failed: removing client sock");
                        exit(1);
                    }
                } else if (info->state == INCORRECT_DATA_AMOUNT) {
                    send_error_msg_to_client(info);
                    send_incorrect_data_msg_to_client(info);
                    close_client_connection(info);
                    dictionary_remove(client_dictionary, &client);
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client, NULL) == -1) {
                        perror("epoll_ctl() failed: removing client sock");
                        exit(1);
                    }
                }
            }
        }
    }
    dictionary_destroy(client_dictionary);
    vector_destroy(files);
    chdir(orig_dir);
    free(orig_dir);
}

/**
 * @brief Configures a file descriptor to operate in non-blocking mode.
 * @param fd File descriptor to be set to non-blocking.
 */
void set_nonblocking(const int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl(F_GETFL) failed");
        exit(1);
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl(F_SETFL) failed");
        exit(1);
    }
}

/**
 * @brief Reads `n` bytes from a non-blocking client
 * @param client client_info struct representing the client we want to read from
 * @param buf buffer to write data into
 * @param n number of bytes to read from the client
 * @return the number of bytes read, -1 if EOF was reached before reading the requested number of bytes,
 * or -2 if there was an error other than EAGAIN or EWOULDBLOCK
 */
ssize_t read_n_from_client(client_info* client, void* buf, const ssize_t n) {
    ssize_t num_read = 0;
    do {
        const ssize_t res = read(client->sock, buf + client->buffer_position + num_read, n - num_read);
        if (res == -1) {
            client->buffer_position += num_read;
            if (errno == EAGAIN || errno == EWOULDBLOCK || num_read != 0) {
                return num_read;
            }
            return -2; /* There was some other error */
        }
        num_read += res;
        if (res == 0 && num_read < n) {
            client->buffer_position += num_read;
            return -1; /* We hit EOF before reading all the bytes we were expecting */
        }
    } while (num_read != n);
    client->buffer_position += num_read;
    return num_read;
}

ssize_t write_n_to_client(const client_info* client, const void* buf, ssize_t n) {
    ssize_t num_written = 0;
    while (num_written != n) {
        const ssize_t res = write(client->sock, buf, n);
        if (res == -1 || res == 0) {
            break;
        }
        num_written += res;
    }
    return num_written;
}

/**
 * @brief Determines the verb the client is using, will update the client's state depending on the request content.
 * Also, resets header and buffer_position if the verb is determined and further header data needs to be read.
 * @param client the client whose verb needs to be determined
 * @return The verb the client is using, V_UNKNOWN if not able to be determined yet, or some error has occurred
 */
verb parse_verb(client_info* client) {
    // Remember, the client socket is non-blocking
    ssize_t pos = client->buffer_position;
    if (pos < 4) { /* Can't tell if we have 'GET '/'PUT ' yet */
        ssize_t res = read_n_from_client(client, client->header, 4 - pos);
        if (res == -1 || res == -2) {
            client->state = INVALID_VERB;
            return V_UNKNOWN;
        }
    } else if (pos < 5) { /* Can't tell if we have 'LIST\n' yet */
        ssize_t res = read_n_from_client(client, client->header, 5 - pos);
        if (res == -1 || res == -2) {
            client->state = INVALID_VERB;
            return V_UNKNOWN;
        }
    } else if (pos < 7) { /* Can't tell if we have 'DELETE ' yet */
        ssize_t res = read_n_from_client(client, client->header, 7 - pos);
        if (res == -1 || res == -2) {
            client->state = INVALID_VERB;
            return V_UNKNOWN;
        }
    } else if (pos < 11) { /* Can't tell if we have 'ADD_SERVER\n' yet */
        ssize_t res = read_n_from_client(client, client->header, 11 - pos);
        if (res == -1 || res == -2) {
            client->state = INVALID_VERB;
            return V_UNKNOWN;
        }
    } else {
        client->state = INVALID_VERB;
        return V_UNKNOWN;
    }

    pos = client->buffer_position;
    if (pos == 4 && strncmp(client->header, "GET ", 4) == 0) {
        memset(client->header, 0, client->buffer_position);
        client->buffer_position = 0;
        client->state = READING_HEADER;
        return GET;
    }
    if (pos == 4 && strncmp(client->header, "PUT ", 4) == 0) {
        memset(client->header, 0, client->buffer_position);
        client->buffer_position = 0;
        client->state = READING_HEADER;
        return PUT;
    }
    if (pos == 5 && strncmp(client->header, "LIST\n", 5) == 0) {
        client->state = HANDLING_VERB;
        return LIST;
    }
    if (pos == 7 && strncmp(client->header, "DELETE ", 7) == 0) {
        memset(client->header, 0, client->buffer_position);
        client->buffer_position = 0;
        client->state = READING_HEADER;
        return DELETE;
    }
    if (pos == 11 && strncmp(client->header, "ADD_SERVER ", 11) == 0) {
        memset(client->header, 0, client->buffer_position);
        client->buffer_position = 0;
        client->state = READING_HEADER;
        return ADD_SERVER;
    }

    return V_UNKNOWN;
}

/**
 * @brief Should only be used if VERB is one of {GET, PUT, DELETE}.
 * Reads the file name from `client`.
 * Also updates the client's state to HANDLING_VERB once the full file name has been read.
 * Otherwise, sets the state to ERROR if the client provides malformed input.
 * @param client client_info representing the client to read from
 */
void read_file_name(client_info* client) {
    ssize_t res;
    do {
        res = read(client->sock, client->header + client->buffer_position, 1);
        if (res == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            break;
        }
        client->buffer_position += res;
    } while (client->header[client->buffer_position - 1] != '\n' && res != 0 && client->buffer_position != 1024);
    if (client->header[client->buffer_position - 1] == '\n') { /* We are good! */
        client->header[--client->buffer_position] = '\0';
        client->state = HANDLING_VERB;
        return;
    }
    if (res == -1 || client->buffer_position == 1024 || res == 0) {
        client->state = INCORRECT_DATA_AMOUNT; // TOO_MUCH_DATA?
    }
}

/**
 * @brief Completes a GET request for a client.
 * Only to be used after `parse_verb` and `read_file_name` have succeeded on this client.
 * `client->header` contains the requested file name and `client->buffer_position` is the length of the string.
 * @param client client that has a GET request
 */
void get(client_info* client) {
    //check the dictionary for the file name, if it exists, then get the server info from the dictionary
    //send that server info the client
    //change the client state to stateDone

    /* Check if the file exists */
    // First: check if main server has it
    bool file_found = false;
    VECTOR_FOR_EACH(
        files, f,
        if (strcmp(f, client->header) == 0) {
        file_found = true;
        break;
        }
    );
    if (file_found) {
        // Serve locally
        send_ok_msg_to_client(client);
        write_n_to_client(client, "0.0.0.0\n0\n", 10);
        client->local_file = open(client->header, O_RDONLY);
        struct stat s;
        fstat(client->local_file, &s);
        const size_t file_size = s.st_size;
        if (write_n_to_client(client, &file_size, sizeof(file_size)) != sizeof(file_size)) {
            client->state = INCORRECT_DATA_AMOUNT;
            return;
        }

        char buffer[1024];
        ssize_t read_result = 0;
        do {
            read_result = read(client->local_file, buffer, 1024);
            if (read_result > 0) {
                write_n_to_client(client, buffer, read_result);
            }
        } while (read_result > 0);

        client->state = DONE;
        return;
    }

    // Otherwise: check dictionary and redirect
    server_info* info;
    if (dictionary_contains(file_to_server, client->header)) {
        info = dictionary_get(file_to_server, client->header);
    } else {
        send_invalid_file_to_client(client);
        client->state = DONE;
        return;
    }
    send_ok_msg_to_client(client);
    char msg[64];
    snprintf(msg, sizeof(msg), "%s\n%s\n", info->ip, info->port);
    write_n_to_client(client, msg, strlen(msg));


    client->state = DONE;
}

void put(client_info* client) {
    //if turn index is not 0, then redirect to the next server at that index
    //send the ip and port of the server to the client
    //change the client state to stateDone
    //increment the index, make sure it wraps around
    //exit the funciton
    //if the index is 0, then we do this
    size_t n = vector_size(mini_servers);
    if (client->local_file == 0) {
        if (current_server_index != 0 && n > 0) {
            // Redirect to the correct mini server
            server_info* target = vector_get(mini_servers, current_server_index - 1);
            dictionary_set(file_to_server, client->header, target);

            char msg[64];
            snprintf(msg, sizeof(msg), "%s\n%s\n", target->ip, target->port);
            write_n_to_client(client, msg, strlen(msg));

            client->state = DONE;
            current_server_index = (current_server_index + 1) % (n + 1); // wrap around including self
            return;
        } else {
            // need to increment the server index when its our turn as well
            write_n_to_client(client, "0.0.0.0\n0\n", 10);
            current_server_index = (current_server_index + 1) % (n + 1); // wrap around including self
        }
    }

    if (client->local_file == 0) {
        bool file_found = false;
        VECTOR_FOR_EACH(
            files, f,
            if (strcmp(f, client->header) == 0) {
            file_found = true;
            break;
            }
        );
        if (!file_found) {
            vector_push_back(files, client->header);
        }
        client->local_file = open(client->header, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
        memset(client->header, 0, client->buffer_position);
        client->buffer_position = 0;
    }
    if (client->size_read == false) {
        read_n_from_client(client, &client->file_size, sizeof(client->file_size));
        if ((size_t)client->buffer_position < sizeof(client->file_size)) {
            return;
        } else {
            client->size_read = true;
            client->buffer_position = 0;
        }
    }


    do {
        const ssize_t read_result = read(client->sock, client->header, 1024);
        if (read_result == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        }
        if (read_result == 0 && client->local_file_pos < (ssize_t)client->file_size) {
            client->state = INCORRECT_DATA_AMOUNT;
            return;
        }
        write(client->local_file, client->header, read_result);
        client->local_file_pos += read_result;
    } while (client->local_file_pos < (ssize_t)client->file_size);
    client->state = DONE;
}

void delete(client_info* client) {
    // Same beginning as get, instead of sending delete
    /* Check if the file exists */
    bool file_found = false;
    size_t pos = 0;
    VECTOR_FOR_EACH(
        files, f,
        if (strcmp(f, client->header) == 0) {
        file_found = true;
        break;
        }
        ++pos;
    );
    if (!file_found) {
        client->state = INVALID_FILE;
        return;
    }
    send_ok_msg_to_client(client);
    /* The only difference with GET is deleting */
    unlink(client->header);
    vector_erase(files, pos);
    client->state = DONE;
}

void list(client_info* client) {
    size_t buffer_size = 128;
    char* file_list = malloc(buffer_size);
    size_t total_bytes = 0;
    VECTOR_FOR_EACH(
        files, file,
        const size_t len = strlen(file);
        if (total_bytes + len + 1 >= buffer_size) {
        buffer_size *= 2;
        file_list = realloc(file_list, buffer_size);
        }
        memcpy(file_list + total_bytes, file, len);
        total_bytes += len;
        file_list[total_bytes++] = '\n';
    );
    // Also send all the files on other servers
    if (dictionary_size(file_to_server) > 0) {
        vector* v = dictionary_keys(file_to_server);
        for (size_t i = 0; i < vector_size(v); ++i) {
            char* file = vector_get(v, i);
            const size_t len = strlen(file);
            if (total_bytes + len + 1 >= buffer_size) {
                buffer_size *= 2;
                file_list = realloc(file_list, buffer_size);
            }
            memcpy(file_list + total_bytes, file, len);
            total_bytes += len;
            file_list[total_bytes++] = '\n';
        }
        --total_bytes;
    }

    send_ok_msg_to_client(client);
    if (write_n_to_client(client, &total_bytes, sizeof(total_bytes)) != sizeof(total_bytes)) {
        client->state = INCORRECT_DATA_AMOUNT;
        return;
    }
    if (write_n_to_client(client, file_list, (ssize_t)total_bytes) != (ssize_t)total_bytes) {
        client->state = INCORRECT_DATA_AMOUNT;
        return;
    }
    free(file_list);
    client->state = DONE;
}

void add_server(client_info* client) {
    // client->header is <ip> <port>
    char* p = strchr(client->header, ' ');
    *p = '\0';
    server_info s;
    strcpy(s.ip, client->header);
    strcpy(s.port, p + 1);


    size_t bytes_read = 0;
    size_t size;

    read(client->sock, &size, sizeof(size));
    // while (bytes_read < sizeof(size_t)) {
    //     const ssize_t cur_read = read(client->sock, &size + bytes_read, sizeof(size_t) - bytes_read);
    //     if (cur_read == -1 || cur_read == 0) {
    //         break;
    //     }
    //     bytes_read += cur_read;
    // }

    char buffer[1024] = {0};
    bytes_read = 0;
    while (bytes_read < size) {
        bytes_read += read_line_from_client(client, buffer, 1024);
        dictionary_set(file_to_server, buffer, &s);
    }
    vector_push_back(mini_servers, &s);
    send_ok_msg_to_client(client); // Notify the client that the operation was successful
    client->state = DONE;
}

void send_ok_msg_to_client(const client_info* client) {
    write_n_to_client(client, "OK\n", 3);
}

void send_error_msg_to_client(const client_info* client) {
    write_n_to_client(client, "ERROR\n", 6);
}

void send_invalid_req_msg_to_client(const client_info* client) {
    write_n_to_client(client, err_bad_request, 12);
}

void send_invalid_file_to_client(const client_info* client) {
    write_n_to_client(client, err_no_such_file, 13);
}

void send_incorrect_data_msg_to_client(const client_info* client) {
    write_n_to_client(client, err_bad_file_size, 14);
}

void close_client_connection(const client_info* client) {
    shutdown(client->sock, SHUT_RDWR);
}

void* client_info_copy_constructor(void* p) {
    client_info* new_client_info = malloc(sizeof(client_info));
    memcpy(new_client_info, p, sizeof(client_info));
    return new_client_info;
}
