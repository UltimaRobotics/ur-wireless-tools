#include "scan_alternatives.h"
#include <errno.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>

// Global variables for signal handling
static wifi_signal_scan_context_t* g_signal_ctx = NULL;

// Signal handler for scan completion
static void wifi_scan_signal_handler(int sig) {
    if (g_signal_ctx && sig == SIGUSR1) {
        g_signal_ctx->scan_ready = 1;
    } else if (g_signal_ctx && sig == SIGUSR2) {
        g_signal_ctx->scan_error = 1;
    }
}

// Direct synchronous scanning without shared memory
int wifi_scan_direct_sync(const char* interface, scan_result_t* results, int max_results) {
    if (!interface || !results) return -1;
    
    // Use the existing perform_scan function directly
    return perform_scan(interface, results, max_results);
}

// Initialize scan context
void wifi_scan_context_init(wifi_scan_context_t* ctx) {
    if (!ctx) return;
    
    memset(ctx, 0, sizeof(wifi_scan_context_t));
    pthread_mutex_init(&ctx->mutex, NULL);
    pthread_cond_init(&ctx->condition, NULL);
    ctx->scan_complete = 0;
    ctx->scan_active = 0;
    ctx->scan_status = 0;
}

// Destroy scan context
void wifi_scan_context_destroy(wifi_scan_context_t* ctx) {
    if (!ctx) return;
    
    if (ctx->scan_active) {
        wifi_scan_threaded_async_stop(ctx);
    }
    
    pthread_mutex_destroy(&ctx->mutex);
    pthread_cond_destroy(&ctx->condition);
    memset(ctx, 0, sizeof(wifi_scan_context_t));
}

// Thread function for asynchronous scanning
static void* wifi_scan_thread_worker(void* arg) {
    wifi_scan_context_t* ctx = (wifi_scan_context_t*)arg;
    
    while (ctx->scan_active) {
        pthread_mutex_lock(&ctx->mutex);
        
        // Perform the scan
        ctx->result_count = perform_scan(ctx->interface, ctx->results, MAX_SCAN_RESULTS);
        ctx->scan_status = (ctx->result_count > 0) ? 0 : -1;
        
        // Call callback if provided
        if (ctx->callback) {
            ctx->callback(ctx->interface, ctx->results, ctx->result_count, ctx->user_data);
        }
        
        ctx->scan_complete = 1;
        pthread_cond_signal(&ctx->condition);
        pthread_mutex_unlock(&ctx->mutex);
        
        // Wait for next scan interval (for continuous scanning)
        if (ctx->scan_interval_ms > 0 && ctx->scan_active) {
            usleep(ctx->scan_interval_ms * 1000);
            pthread_mutex_lock(&ctx->mutex);
            ctx->scan_complete = 0; // Reset for next iteration
            pthread_mutex_unlock(&ctx->mutex);
        } else {
            break; // Single scan mode
        }
    }
    
    return NULL;
}

// Start threaded asynchronous scanning
int wifi_scan_threaded_async_start(wifi_scan_context_t* ctx, const char* interface, 
                                   wifi_scan_callback_t callback, void* user_data) {
    if (!ctx || !interface) return -1;
    
    pthread_mutex_lock(&ctx->mutex);
    
    if (ctx->scan_active) {
        pthread_mutex_unlock(&ctx->mutex);
        return -1; // Already active
    }
    
    strncpy(ctx->interface, interface, INTERFACE_NAME_LEN - 1);
    ctx->interface[INTERFACE_NAME_LEN - 1] = '\0';
    ctx->callback = callback;
    ctx->user_data = user_data;
    ctx->scan_active = 1;
    ctx->scan_complete = 0;
    ctx->scan_interval_ms = 0; // Single scan
    
    int result = pthread_create(&ctx->thread_id, NULL, wifi_scan_thread_worker, ctx);
    pthread_mutex_unlock(&ctx->mutex);
    
    return result;
}

// Stop threaded asynchronous scanning
int wifi_scan_threaded_async_stop(wifi_scan_context_t* ctx) {
    if (!ctx) return -1;
    
    pthread_mutex_lock(&ctx->mutex);
    ctx->scan_active = 0;
    pthread_cond_signal(&ctx->condition);
    pthread_mutex_unlock(&ctx->mutex);
    
    if (ctx->thread_id) {
        pthread_join(ctx->thread_id, NULL);
        ctx->thread_id = 0;
    }
    
    return 0;
}

// Start continuous threaded scanning
int wifi_scan_threaded_continuous_start(wifi_scan_context_t* ctx, const char* interface, 
                                        int interval_ms, wifi_scan_callback_t callback, void* user_data) {
    if (!ctx || !interface || interval_ms <= 0) return -1;
    
    pthread_mutex_lock(&ctx->mutex);
    
    if (ctx->scan_active) {
        pthread_mutex_unlock(&ctx->mutex);
        return -1; // Already active
    }
    
    strncpy(ctx->interface, interface, INTERFACE_NAME_LEN - 1);
    ctx->interface[INTERFACE_NAME_LEN - 1] = '\0';
    ctx->callback = callback;
    ctx->user_data = user_data;
    ctx->scan_active = 1;
    ctx->scan_complete = 0;
    ctx->scan_interval_ms = interval_ms;
    
    int result = pthread_create(&ctx->thread_id, NULL, wifi_scan_thread_worker, ctx);
    pthread_mutex_unlock(&ctx->mutex);
    
    return result;
}

// Initialize pipe-based scanning
int wifi_scan_pipe_based_init(wifi_pipe_scan_context_t* ctx, const char* interface) {
    if (!ctx || !interface) return -1;
    
    memset(ctx, 0, sizeof(wifi_pipe_scan_context_t));
    strncpy(ctx->interface, interface, INTERFACE_NAME_LEN - 1);
    ctx->interface[INTERFACE_NAME_LEN - 1] = '\0';
    
    if (pipe(ctx->pipe_fd) == -1) {
        return -1;
    }
    
    // Set pipes to non-blocking mode
    fcntl(ctx->pipe_fd[0], F_SETFL, O_NONBLOCK);
    fcntl(ctx->pipe_fd[1], F_SETFL, O_NONBLOCK);
    
    return 0;
}

// Execute pipe-based scanning
int wifi_scan_pipe_based_execute(wifi_pipe_scan_context_t* ctx, scan_result_t* results, int max_results) {
    if (!ctx || !results) return -1;
    
    ctx->child_pid = fork();
    
    if (ctx->child_pid == -1) {
        return -1; // Fork failed
    } else if (ctx->child_pid == 0) {
        // Child process - perform scan and write to pipe
        close(ctx->pipe_fd[0]); // Close read end
        
        scan_result_t child_results[MAX_SCAN_RESULTS];
        int scan_count = perform_scan(ctx->interface, child_results, MAX_SCAN_RESULTS);
        
        // Write scan count first
        write(ctx->pipe_fd[1], &scan_count, sizeof(int));
        
        // Write results if any
        if (scan_count > 0) {
            size_t results_size = scan_count * sizeof(scan_result_t);
            write(ctx->pipe_fd[1], child_results, results_size);
        }
        
        close(ctx->pipe_fd[1]);
        exit(scan_count > 0 ? 0 : 1);
    } else {
        // Parent process - read from pipe
        close(ctx->pipe_fd[1]); // Close write end
        
        struct pollfd pfd;
        pfd.fd = ctx->pipe_fd[0];
        pfd.events = POLLIN;
        
        // Wait for data with timeout (15 seconds)
        int poll_result = poll(&pfd, 1, 15000);
        
        if (poll_result > 0 && (pfd.revents & POLLIN)) {
            int scan_count = 0;
            ssize_t bytes_read = read(ctx->pipe_fd[0], &scan_count, sizeof(int));
            
            if (bytes_read == sizeof(int) && scan_count > 0) {
                // Limit to max_results
                int count_to_read = (scan_count > max_results) ? max_results : scan_count;
                size_t results_size = count_to_read * sizeof(scan_result_t);
                
                ssize_t results_read = read(ctx->pipe_fd[0], results, results_size);
                if (results_read == (ssize_t)results_size) {
                    close(ctx->pipe_fd[0]);
                    
                    // Wait for child to complete
                    int status;
                    waitpid(ctx->child_pid, &status, 0);
                    
                    return count_to_read;
                }
            } else if (bytes_read == sizeof(int) && scan_count == 0) {
                // Scan completed but no results
                close(ctx->pipe_fd[0]);
                int status;
                waitpid(ctx->child_pid, &status, 0);
                return 0;
            }
        }
        
        // Timeout or error - kill child and cleanup
        kill(ctx->child_pid, SIGTERM);
        usleep(500000); // 500ms grace period
        if (waitpid(ctx->child_pid, NULL, WNOHANG) == 0) {
            kill(ctx->child_pid, SIGKILL);
            waitpid(ctx->child_pid, NULL, 0);
        }
        close(ctx->pipe_fd[0]);
        
        return -1;
    }
}

// Cleanup pipe-based scanning
int wifi_scan_pipe_based_cleanup(wifi_pipe_scan_context_t* ctx) {
    if (!ctx) return -1;
    
    if (ctx->scan_running && ctx->child_pid > 0) {
        kill(ctx->child_pid, SIGTERM);
        waitpid(ctx->child_pid, NULL, 0);
    }
    
    if (ctx->pipe_fd[0] > 0) close(ctx->pipe_fd[0]);
    if (ctx->pipe_fd[1] > 0) close(ctx->pipe_fd[1]);
    
    memset(ctx, 0, sizeof(wifi_pipe_scan_context_t));
    return 0;
}

// Initialize signal-based scanning
int wifi_scan_signal_based_init(wifi_signal_scan_context_t* ctx, const char* interface) {
    if (!ctx || !interface) return -1;
    
    memset(ctx, 0, sizeof(wifi_signal_scan_context_t));
    strncpy(ctx->interface, interface, INTERFACE_NAME_LEN - 1);
    ctx->interface[INTERFACE_NAME_LEN - 1] = '\0';
    
    // Allocate shared result buffer using mmap
    ctx->result_buffer = (scan_result_t*)mmap(NULL, MAX_SCAN_RESULTS * sizeof(scan_result_t),
                                              PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (ctx->result_buffer == MAP_FAILED) return -1;
    
    ctx->result_count_buffer = (int*)mmap(NULL, sizeof(int),
                                          PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (ctx->result_count_buffer == MAP_FAILED) {
        munmap(ctx->result_buffer, MAX_SCAN_RESULTS * sizeof(scan_result_t));
        return -1;
    }
    
    // Set up signal handlers
    signal(SIGUSR1, wifi_scan_signal_handler);
    signal(SIGUSR2, wifi_scan_signal_handler);
    
    g_signal_ctx = ctx;
    
    return 0;
}

// Execute signal-based scanning
int wifi_scan_signal_based_execute(wifi_signal_scan_context_t* ctx, scan_result_t* results, int max_results) {
    if (!ctx || !results) return -1;
    
    ctx->scan_ready = 0;
    ctx->scan_error = 0;
    *ctx->result_count_buffer = 0;
    
    ctx->scanner_pid = fork();
    
    if (ctx->scanner_pid == -1) {
        return -1;
    } else if (ctx->scanner_pid == 0) {
        // Child process - perform scan
        int scan_count = perform_scan(ctx->interface, ctx->result_buffer, MAX_SCAN_RESULTS);
        *ctx->result_count_buffer = scan_count;
        
        // Signal parent based on result
        if (scan_count >= 0) {
            kill(getppid(), SIGUSR1); // Success (even if 0 results)
        } else {
            kill(getppid(), SIGUSR2); // Error
        }
        
        exit(scan_count >= 0 ? 0 : 1);
    } else {
        // Parent process - wait for signal
        time_t start_time = time(NULL);
        
        while (!ctx->scan_ready && !ctx->scan_error) {
            usleep(100000); // 100ms
            
            // Timeout after 15 seconds
            if (time(NULL) - start_time > 15) {
                kill(ctx->scanner_pid, SIGTERM);
                usleep(500000); // 500ms grace period
                if (waitpid(ctx->scanner_pid, NULL, WNOHANG) == 0) {
                    kill(ctx->scanner_pid, SIGKILL);
                    waitpid(ctx->scanner_pid, NULL, 0);
                }
                return -1;
            }
        }
        
        // Copy results
        int result_count = 0;
        if (ctx->scan_ready && *ctx->result_count_buffer >= 0) {
            result_count = *ctx->result_count_buffer;
            if (result_count > max_results) result_count = max_results;
            
            if (result_count > 0) {
                memcpy(results, ctx->result_buffer, result_count * sizeof(scan_result_t));
            }
        }
        
        // Wait for child to complete
        int status;
        waitpid(ctx->scanner_pid, &status, 0);
        
        return ctx->scan_ready ? result_count : -1;
    }
}

// Cleanup signal-based scanning
int wifi_scan_signal_based_cleanup(wifi_signal_scan_context_t* ctx) {
    if (!ctx) return -1;
    
    if (ctx->scanner_pid > 0) {
        kill(ctx->scanner_pid, SIGTERM);
        waitpid(ctx->scanner_pid, NULL, 0);
    }
    
    if (ctx->result_buffer && ctx->result_buffer != MAP_FAILED) {
        munmap(ctx->result_buffer, MAX_SCAN_RESULTS * sizeof(scan_result_t));
        ctx->result_buffer = NULL;
    }
    
    if (ctx->result_count_buffer && ctx->result_count_buffer != MAP_FAILED) {
        munmap(ctx->result_count_buffer, sizeof(int));
        ctx->result_count_buffer = NULL;
    }
    
    // Reset signal handlers
    signal(SIGUSR1, SIG_DFL);
    signal(SIGUSR2, SIG_DFL);
    
    g_signal_ctx = NULL;
    memset(ctx, 0, sizeof(wifi_signal_scan_context_t));
    
    return 0;
}

// Enhanced continuous scanning with threading
void wifi_continuous_scan_loop_threaded(const char* interface_name, float delay_seconds) {
    wifi_scan_context_t ctx;
    wifi_scan_context_init(&ctx);
    
    int interval_ms = (int)(delay_seconds * 1000);
    const int minimum_interval_ms = 500; // 0.5 seconds minimum
    
    if (interval_ms < minimum_interval_ms) {
        printf("{\"warning\": \"Scan interval too low, increasing to %.1f seconds for hardware stability\"}\n", 
               minimum_interval_ms / 1000.0);
        interval_ms = minimum_interval_ms;
    }
    
    // Callback function for continuous scanning
    auto void scan_callback(const char* interface, scan_result_t* results, int count, void* user_data) {
        static int scan_number = 1;
        
        printf("{\n");
        printf("  \"scan_number\": %d,\n", scan_number++);
        printf("  \"interface\": \"%s\",\n", interface);
        printf("  \"scan_time\": %ld,\n", time(NULL));
        printf("  \"scan_method\": \"threaded\",\n");
        printf("  \"scan_delay\": %.3f,\n", delay_seconds);
        printf("  \"results_count\": %d,\n", count);
        printf("  \"scan_results\": [\n");
        
        for (int i = 0; i < count; i++) {
            printf("    {\n");
            printf("      \"ssid\": \"%s\",\n", results[i].ssid);
            printf("      \"bssid\": \"%s\",\n", results[i].bssid);
            printf("      \"frequency\": %d,\n", results[i].frequency);
            printf("      \"signal_strength\": %d,\n", results[i].signal_strength);
            printf("      \"quality\": %d,\n", results[i].quality);
            printf("      \"security\": \"%s\"\n", results[i].security);
            printf("    }%s\n", (i < count - 1) ? "," : "");
        }
        
        printf("  ]\n");
        printf("}\n");
        fflush(stdout);
    }
    
    wifi_scan_threaded_continuous_start(&ctx, interface_name, interval_ms, scan_callback, NULL);
    
    // Keep running until interrupted
    while (keep_running) {
        usleep(100000); // 100ms
    }
    
    wifi_scan_threaded_async_stop(&ctx);
    wifi_scan_context_destroy(&ctx);
}

// Enhanced continuous scanning with pipes
void wifi_continuous_scan_loop_pipe(const char* interface_name, float delay_seconds) {
    const float minimum_scan_interval = 0.5;
    if (delay_seconds < minimum_scan_interval) {
        printf("{\"warning\": \"Scan interval too low, increasing to %.1f seconds for hardware stability\"}\n", minimum_scan_interval);
        delay_seconds = minimum_scan_interval;
    }
    
    int scan_number = 1;
    
    while (keep_running) {
        wifi_pipe_scan_context_t ctx;
        
        if (wifi_scan_pipe_based_init(&ctx, interface_name) == 0) {
            scan_result_t results[MAX_SCAN_RESULTS];
            clock_t start_time = clock();
            
            int scan_count = wifi_scan_pipe_based_execute(&ctx, results, MAX_SCAN_RESULTS);
            
            clock_t end_time = clock();
            int scan_duration_ms = (int)((end_time - start_time) * 1000 / CLOCKS_PER_SEC);
            
            printf("{\n");
            printf("  \"scan_number\": %d,\n", scan_number++);
            printf("  \"interface\": \"%s\",\n", interface_name);
            printf("  \"scan_time\": %ld,\n", time(NULL));
            printf("  \"scan_method\": \"pipe-based\",\n");
            printf("  \"scan_duration_ms\": %d,\n", scan_duration_ms);
            printf("  \"scan_delay\": %.3f,\n", delay_seconds);
            printf("  \"results_count\": %d,\n", scan_count);
            printf("  \"scan_results\": [\n");
            
            for (int i = 0; i < scan_count; i++) {
                printf("    {\n");
                printf("      \"ssid\": \"%s\",\n", results[i].ssid);
                printf("      \"bssid\": \"%s\",\n", results[i].bssid);
                printf("      \"frequency\": %d,\n", results[i].frequency);
                printf("      \"signal_strength\": %d,\n", results[i].signal_strength);
                printf("      \"quality\": %d,\n", results[i].quality);
                printf("      \"encryption\": \"%s\"\n", results[i].security);
                printf("    }%s\n", (i < scan_count - 1) ? "," : "");
            }
            
            printf("  ]\n");
            printf("}\n");
            fflush(stdout);
            
            wifi_scan_pipe_based_cleanup(&ctx);
        }
        
        if (keep_running) {
            precise_sleep(delay_seconds);
        }
    }
}

// Enhanced continuous scanning with signals
void wifi_continuous_scan_loop_signal(const char* interface_name, float delay_seconds) {
    const float minimum_scan_interval = 0.5;
    if (delay_seconds < minimum_scan_interval) {
        printf("{\"warning\": \"Scan interval too low, increasing to %.1f seconds for hardware stability\"}\n", minimum_scan_interval);
        delay_seconds = minimum_scan_interval;
    }
    
    int scan_number = 1;
    
    while (keep_running) {
        wifi_signal_scan_context_t ctx;
        
        if (wifi_scan_signal_based_init(&ctx, interface_name) == 0) {
            scan_result_t results[MAX_SCAN_RESULTS];
            clock_t start_time = clock();
            
            int scan_count = wifi_scan_signal_based_execute(&ctx, results, MAX_SCAN_RESULTS);
            
            clock_t end_time = clock();
            int scan_duration_ms = (int)((end_time - start_time) * 1000 / CLOCKS_PER_SEC);
            
            printf("{\n");
            printf("  \"scan_number\": %d,\n", scan_number++);
            printf("  \"interface\": \"%s\",\n", interface_name);
            printf("  \"scan_time\": %ld,\n", time(NULL));
            printf("  \"scan_method\": \"signal-based\",\n");
            printf("  \"scan_duration_ms\": %d,\n", scan_duration_ms);
            printf("  \"scan_delay\": %.3f,\n", delay_seconds);
            printf("  \"results_count\": %d,\n", scan_count);
            printf("  \"scan_results\": [\n");
            
            for (int i = 0; i < scan_count; i++) {
                printf("    {\n");
                printf("      \"ssid\": \"%s\",\n", results[i].ssid);
                printf("      \"bssid\": \"%s\",\n", results[i].bssid);
                printf("      \"frequency\": %d,\n", results[i].frequency);
                printf("      \"signal_strength\": %d,\n", results[i].signal_strength);
                printf("      \"quality\": %d,\n", results[i].quality);
                printf("      \"encryption\": \"%s\"\n", results[i].security);
                printf("    }%s\n", (i < scan_count - 1) ? "," : "");
            }
            
            printf("  ]\n");
            printf("}\n");
            fflush(stdout);
            
            wifi_scan_signal_based_cleanup(&ctx);
        }
        
        if (keep_running) {
            precise_sleep(delay_seconds);
        }
    }
}

// Select optimal scan method based on system capabilities
wifi_scan_method_t wifi_select_optimal_scan_method(const char* interface) {
    // For now, default to threaded method as it's most reliable
    return WIFI_SCAN_METHOD_THREADED;
}

// Configure scan method
int wifi_configure_scan_method(wifi_scan_method_t method) {
    // Configuration can be added here for different methods
    return 0;
}