/**
 * nonstop_networking
 * CS 341 - Spring 2025
 */
#include "common.h"

const size_t list_request_size = 5; // space for LIST\n
const size_t min_server_response_header_size = 3; // space for OK\n
const size_t max_server_response_header_size = 6; // space for ERROR\n
const size_t max_verb_size = 6; // space for DELETE