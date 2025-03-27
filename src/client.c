#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include "common.h"

#define FILE_REQUEST_PREFIX "GET_FILE:"

int main(int argc, char *argv[]) {
    int sock;
    struct addrinfo hints, *result;
    char buffer[BUFFER_SIZE] = {0};
    char *server_ip;
    char filename[256] = {0};
    
    // Check if server IP was provided
    if (argc < 2) {
        printf("Usage: %s <server_ip> [filename]\n", argv[0]);
        printf("Using default IP: %s\n", SERVER_IP);
        server_ip = SERVER_IP;
    } else {
        server_ip = argv[1];
    }
    
    // Check if filename was provided
    if (argc < 3) {
        printf("Enter filename to request: ");
        if (fgets(filename, sizeof(filename), stdin) == NULL) {
            fprintf(stderr, "Error reading filename\n");
            return -1;
        }
        // Remove newline character if present
        size_t len = strlen(filename);
        if (len > 0 && filename[len-1] == '\n') {
            filename[len-1] = '\0';
        }
    } else {
        strncpy(filename, argv[2], sizeof(filename) - 1);
    }

    // Initialize address info struct
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP

    // Get address info for the server
    int s = getaddrinfo(server_ip, "8000", &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    // Create socket
    sock = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (sock < 0) {
        perror("Socket creation failed");
        freeaddrinfo(result);
        return -1;
    }

    // Connect to server
    if (connect(sock, result->ai_addr, result->ai_addrlen) < 0) {
        perror("Connection failed");
        freeaddrinfo(result);
        close(sock);
        return -1;
    }

    printf("Connected to server at %s:%d\n", server_ip, PORT);
    freeaddrinfo(result);  // Free the address info structure

    // Create file request message
    char request[BUFFER_SIZE];
    snprintf(request, BUFFER_SIZE, "%s%s", FILE_REQUEST_PREFIX, filename);
    
    // Send file request to server
    write(sock, request, strlen(request));
    printf("File request sent: %s\n", request);
    
    // Create local file to save the downloaded content
    char local_filename[256];
    snprintf(local_filename, sizeof(local_filename), "downloaded_%s", filename);
    int file_fd = open(local_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    
    if (file_fd < 0) {
        perror("Cannot create local file");
        close(sock);
        return -1;
    }
    
    // Receive response from server
    int first_chunk = 1;
    int is_error = 0;
    ssize_t bytes_received;
    
    while ((bytes_received = read(sock, buffer, BUFFER_SIZE - 1)) > 0) {
        if (first_chunk) {
            buffer[bytes_received] = '\0';  // Null-terminate to check prefix
            
            if (strncmp(buffer, "ERROR:", 6) == 0) {
                // Error response from server
                printf("Server error: %s\n", buffer);
                is_error = 1;
                break;
            } else if (strncmp(buffer, "OK:", 3) == 0) {
                // Success response, write to file starting after "OK:"
                write(file_fd, buffer + 3, bytes_received - 3);
            } else {
                // No prefix, just write the data
                write(file_fd, buffer, bytes_received);
            }
            
            first_chunk = 0;
        } else {
            // Write subsequent chunks directly to the file
            write(file_fd, buffer, bytes_received);
        }
    }
    
    close(file_fd);
    
    if (!is_error) {
        printf("File downloaded successfully as '%s'\n", local_filename);
    } else {
        // Remove the empty file if there was an error
        unlink(local_filename);
    }

    // Close socket
    close(sock);
    return 0;
}