#ifndef JSON_FORMATTER_H
#define JSON_FORMATTER_H

#include "wifi_scanner.h"

// JSON formatting functions
void print_interface_json(const wifi_interface_t *interface);
void print_scan_results_json(const scan_session_t *session);
void print_scan_result_json(const scan_result_t *result, int is_last);
void print_continuous_scan_json(const char *interface_name, const scan_session_t *session);
void print_connection_test_json(const connection_test_result_t *result);
char* escape_json_string(const char *str);

#endif // JSON_FORMATTER_H