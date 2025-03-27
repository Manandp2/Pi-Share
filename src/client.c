#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include "common.h"

int main() {
    int sock;
    struct addrinfo hints, *result;
    char *message = "Hello, Server!";
    char buffer[BUFFER_SIZE] = {0};

    // Initialize address info struct
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;        // IPv4
    hints.ai_socktype = SOCK_STREAM;  // TCP

    // Get address info for the server
    int s = getaddrinfo(SERVER_IP, "8000", &hints, &result);
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

    printf("Connected to server at %s:%d\n", SERVER_IP, PORT);
    freeaddrinfo(result);  // Free the address info structure

    // Send message to server
    write(sock, message, strlen(message));
    printf("Message sent to server: %s\n", message);

    // Receive response from server
    int bytes_received = read(sock, buffer, BUFFER_SIZE - 1);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';  // Ensure null termination
        printf("Response from server: %s\n", buffer);
    } else {
        printf("No response or error receiving from server\n");
    }

    // Close socket
    close(sock);
    return 0;
}