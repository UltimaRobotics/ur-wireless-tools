#ifndef WIFI_SCANNER_H
#define WIFI_SCANNER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>

#define MAX_INTERFACES 16
#define MAX_INTERFACE_NAME 16
#define MAX_SCAN_RESULTS 256
#define MAX_COMMAND_LEN 512
#define MAX_LINE_LEN 1024
#define MAX_SSID_LEN 64
#define MAX_MAC_LEN 18
#define CONNECTION_TIMEOUT_SECONDS 5
#define SECURED_CONNECTION_TIMEOUT_SECONDS 10

// Structure to hold interface information
typedef struct {
    char name[MAX_INTERFACE_NAME];
    char type[32];
    char status[32];
    char mac[MAX_MAC_LEN];
    int frequency;
    int channel;
    char ssid[MAX_SSID_LEN];
    int signal_strength;
    int tx_power;
    char mode[32];
    int was_connected;
} wifi_interface_t;

// Structure to hold scan result
typedef struct {
    char bssid[MAX_MAC_LEN];
    char ssid[MAX_SSID_LEN];
    int frequency;
    int channel;
    int signal_strength;
    char security[64];
    char capabilities[128];
    int quality;
    char timestamp[32];
} scan_result_t;

// Structure to hold scan session data
typedef struct {
    wifi_interface_t interface;
    scan_result_t results[MAX_SCAN_RESULTS];
    int result_count;
    time_t scan_time;
    int scan_duration_ms;
} scan_session_t;

// Structure to hold connection test result
typedef struct {
    char ssid[MAX_SSID_LEN];
    char interface_name[MAX_INTERFACE_NAME];
    int success;
    char error_message[256];
    char connection_type[32];
    time_t test_time;
    int test_duration_ms;
    char original_ssid[MAX_SSID_LEN];
    char original_bssid[MAX_MAC_LEN];
    int was_connected;
} connection_test_result_t;

// Global variables
extern volatile int keep_running;
extern float scan_delay;

// Function declarations
int detect_wifi_interfaces(wifi_interface_t *interfaces, int max_interfaces);
int get_interface_info(const char *interface_name, wifi_interface_t *interface);
int perform_scan(const char *interface_name, scan_result_t *results, int max_results);
int perform_forked_scan(const char *interface_name, scan_result_t *results, int max_results);
void continuous_scan_loop(const char *interface_name, float delay_seconds);
void continuous_info_loop(const char *interface_name, float delay_seconds);
int test_open_ap_connection(const char *interface_name, const char *ssid, connection_test_result_t *result);
int test_secured_ap_connection(const char *interface_name, const char *ssid, const char *password, connection_test_result_t *result);
int start_wpa_supplicant_with_timeout(const char *interface_name, const char *config_file, int timeout_seconds, pid_t *wpa_pid);
int start_wpa_supplicant_secured_with_timeout(const char *interface_name, const char *config_file, const char *ssid, int timeout_seconds, pid_t *wpa_pid);
int save_interface_state(const char *interface_name, wifi_interface_t *saved_state);
int restore_interface_state(const char *interface_name, const wifi_interface_t *saved_state);
char* generate_random_filename(void);
void log_command_execution(const char *command, const char *description);
int execute_command_with_logging(const char *command, const char *description);
FILE* execute_command_with_output_logging(const char *command, const char *description);
void signal_handler(int sig);
void print_usage(const char *program_name);
void precise_sleep(float seconds);

#endif // WIFI_SCANNER_H