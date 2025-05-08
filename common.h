/**
 * nonstop_networking
 * CS 341 - Spring 2025
 */
#pragma once
#include <stddef.h>
#include <sys/types.h>

extern const size_t list_request_size; //space for LIST\n
extern const size_t min_server_response_header_size; // space for OK\n
extern const size_t max_server_response_header_size; // space for ERROR\n
extern const size_t max_verb_size;

#define LOG(...)                      \
    do {                              \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n");        \
    } while (0);

typedef enum { GET, PUT, DELETE, LIST, V_UNKNOWN } verb;
