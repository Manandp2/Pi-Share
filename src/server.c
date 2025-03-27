#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "common.h"

#define FILE_REQUEST_PREFIX "GET_FILE:"
#define MAX_FILENAME_SIZE 256

int main() {
    int server_fd, client_fd;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options to reuse address and port
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

    // Configure server address
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;  // Accept connections from any IP
    address.sin_port = htons(PORT);

    // Bind socket to the specified port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen for incoming connections (backlog of 5)
    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);
    
    while(1) {
        printf("Waiting for connection...\n");
        
        // Accept a connection
        if ((client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Accept failed");
            continue;
        }

        // Get client IP
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(address.sin_addr), client_ip, INET_ADDRSTRLEN);
        printf("Connection accepted from %s:%d\n", client_ip, ntohs(address.sin_port));

        // Read client message
        int bytes_read = read(client_fd, buffer, BUFFER_SIZE - 1);
        if (bytes_read <= 0) {
            close(client_fd);
            continue;
        }
        
        buffer[bytes_read] = '\0';  // Ensure null termination
        printf("Received request: %s\n", buffer);
        
        // Check if it's a file request
        if (strncmp(buffer, FILE_REQUEST_PREFIX, strlen(FILE_REQUEST_PREFIX)) == 0) {
            char filename[MAX_FILENAME_SIZE];
            strcpy(filename, buffer + strlen(FILE_REQUEST_PREFIX));
            
            printf("File requested: %s\n", filename);
            
            // Open the requested file
            int file_fd = open(filename, O_RDONLY);
            if (file_fd < 0) {
                char error_msg[BUFFER_SIZE];
                snprintf(error_msg, BUFFER_SIZE, "ERROR: File '%s' not found or cannot be opened", filename);
                write(client_fd, error_msg, strlen(error_msg));
                printf("%s\n", error_msg);
            } else {
                // Send file contents
                ssize_t bytes_read;
                char file_buffer[BUFFER_SIZE];
                
                // First send an OK message
                const char *ok_msg = "OK:";
                write(client_fd, ok_msg, strlen(ok_msg));
                
                // Then send the file contents
                while ((bytes_read = read(file_fd, file_buffer, BUFFER_SIZE)) > 0) {
                    write(client_fd, file_buffer, bytes_read);
                }
                
                close(file_fd);
                printf("File '%s' sent successfully\n", filename);
            }
        } else {
            // Handle as a normal message
            printf("Message from client: %s\n", buffer);
            
            // Send a simple response
            const char *response = "Hello, Client! Please use 'GET_FILE:filename' to request a file.";
            write(client_fd, response, strlen(response));
        }

        // Close the client connection
        close(client_fd);
    }

    // Close the server socket (won't reach here with the infinite loop)
    close(server_fd);
    return 0;
}