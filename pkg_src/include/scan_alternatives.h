#ifndef WIFI_SCAN_ALTERNATIVES_H
#define WIFI_SCAN_ALTERNATIVES_H

#include "wifi_scanner.h"
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>

// Use the constants from wifi_scanner.h
#define INTERFACE_NAME_LEN MAX_INTERFACE_NAME

// Alternative scan methods enumeration
typedef enum {
    WIFI_SCAN_METHOD_DIRECT,           // Direct synchronous scanning
    WIFI_SCAN_METHOD_THREADED,         // Threaded asynchronous scanning
    WIFI_SCAN_METHOD_PIPE,             // Inter-process communication via pipes
    WIFI_SCAN_METHOD_SIGNAL_BASED,     // Signal-based process coordination
    WIFI_SCAN_METHOD_ASYNC_CALLBACK,   // Callback-based asynchronous scanning
    WIFI_SCAN_METHOD_FORKED_SHM        // Legacy forked with shared memory (deprecated)
} wifi_scan_method_t;

// Scan result callback function type
typedef void (*wifi_scan_callback_t)(const char* interface, scan_result_t* results, int count, void* user_data);

// Thread-safe scan context structure
typedef struct {
    char interface[INTERFACE_NAME_LEN];
    scan_result_t results[MAX_SCAN_RESULTS];
    int result_count;
    wifi_scan_callback_t callback;
    void* user_data;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    volatile int scan_complete;
    volatile int scan_active;
    int scan_interval_ms;
    pthread_t thread_id;
    int scan_status;
} wifi_scan_context_t;

// Pipe-based scan communication structure
typedef struct {
    int pipe_fd[2];  // [0] = read, [1] = write
    pid_t child_pid;
    char interface[INTERFACE_NAME_LEN];
    volatile int scan_running;
} wifi_pipe_scan_context_t;

// Signal-based scan context
typedef struct {
    char interface[INTERFACE_NAME_LEN];
    scan_result_t* result_buffer;
    int* result_count_buffer;
    volatile sig_atomic_t scan_ready;
    volatile sig_atomic_t scan_error;
    pid_t scanner_pid;
} wifi_signal_scan_context_t;

// Function prototypes for alternative scan methods

// Direct synchronous scanning (no shared memory)
int wifi_scan_direct_sync(const char* interface, scan_result_t* results, int max_results);

// Threaded asynchronous scanning
int wifi_scan_threaded_async_start(wifi_scan_context_t* ctx, const char* interface, 
                                   wifi_scan_callback_t callback, void* user_data);
int wifi_scan_threaded_async_stop(wifi_scan_context_t* ctx);
int wifi_scan_threaded_continuous_start(wifi_scan_context_t* ctx, const char* interface, 
                                        int interval_ms, wifi_scan_callback_t callback, void* user_data);

// Pipe-based inter-process scanning
int wifi_scan_pipe_based_init(wifi_pipe_scan_context_t* ctx, const char* interface);
int wifi_scan_pipe_based_execute(wifi_pipe_scan_context_t* ctx, scan_result_t* results, int max_results);
int wifi_scan_pipe_based_cleanup(wifi_pipe_scan_context_t* ctx);

// Signal-based process coordination
int wifi_scan_signal_based_init(wifi_signal_scan_context_t* ctx, const char* interface);
int wifi_scan_signal_based_execute(wifi_signal_scan_context_t* ctx, scan_result_t* results, int max_results);
int wifi_scan_signal_based_cleanup(wifi_signal_scan_context_t* ctx);

// Callback-based asynchronous scanning
int wifi_scan_async_callback_start(const char* interface, wifi_scan_callback_t callback, void* user_data);

// Continuous scanning alternatives
int wifi_continuous_scan_threaded(const char* interface, int interval_ms, 
                                  wifi_scan_callback_t callback, void* user_data);
int wifi_continuous_scan_timer_based(const char* interface, int interval_ms, 
                                     wifi_scan_callback_t callback, void* user_data);

// Enhanced continuous scanning with different methods
void wifi_continuous_scan_loop_threaded(const char* interface_name, float delay_seconds);
void wifi_continuous_scan_loop_pipe(const char* interface_name, float delay_seconds);
void wifi_continuous_scan_loop_signal(const char* interface_name, float delay_seconds);

// Utility functions
void wifi_scan_context_init(wifi_scan_context_t* ctx);
void wifi_scan_context_destroy(wifi_scan_context_t* ctx);
int wifi_scan_timeout_handler(int timeout_seconds);

// Method selection and configuration
wifi_scan_method_t wifi_select_optimal_scan_method(const char* interface);
int wifi_configure_scan_method(wifi_scan_method_t method);

#endif // WIFI_SCAN_ALTERNATIVES_H