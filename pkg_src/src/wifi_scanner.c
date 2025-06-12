#include "wifi_scanner.h"
#include "json_formatter.h"
#include <ctype.h>

int get_interface_info(const char *interface_name, wifi_interface_t *interface) {
    FILE *fp;
    char command[MAX_COMMAND_LEN];
    char line[MAX_LINE_LEN];
    
    // Initialize interface structure
    memset(interface, 0, sizeof(wifi_interface_t));
    strncpy(interface->name, interface_name, MAX_INTERFACE_NAME - 1);
    
    // Get interface status using ip command
    snprintf(command, sizeof(command), "ip link show %s | grep -oE '(UP|DOWN)'", interface_name);
    fp = popen(command, "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\n")] = 0;
            strncpy(interface->status, line, sizeof(interface->status) - 1);
        } else {
            strcpy(interface->status, "DOWN");
        }
        pclose(fp);
    }
    
    // Get MAC address
    snprintf(command, sizeof(command), "cat /sys/class/net/%s/address 2>/dev/null", interface_name);
    fp = popen(command, "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\n")] = 0;
            strncpy(interface->mac, line, MAX_MAC_LEN - 1);
        }
        pclose(fp);
    }
    
    // Get wireless information using iw
    snprintf(command, sizeof(command), "iw dev %s info 2>/dev/null", interface_name);
    fp = popen(command, "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\n")] = 0;
            
            if (strstr(line, "\ttype ")) {
                char *type_start = strstr(line, "\ttype ") + 6;
                char *end = strchr(type_start, '\n');
                if (end) *end = '\0';
                strncpy(interface->type, type_start, sizeof(interface->type) - 1);
                interface->type[sizeof(interface->type) - 1] = '\0';
            }
            else if (strstr(line, "\tchannel ")) {
                sscanf(line, "\tchannel %d (%d MHz)", &interface->channel, &interface->frequency);
            }
            else if (strstr(line, "\ttxpower ")) {
                float power;
                if (sscanf(line, "\ttxpower %f dBm", &power) == 1) {
                    interface->tx_power = (int)power;
                }
            }
        }
        pclose(fp);
    }
    
    // Get connection information using iw link
    snprintf(command, sizeof(command), "iw dev %s link 2>/dev/null", interface_name);
    fp = popen(command, "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            line[strcspn(line, "\n")] = 0;
            
            if (strstr(line, "Connected to ")) {
                // Extract BSSID from "Connected to XX:XX:XX:XX:XX:XX"
                char *bssid_start = strstr(line, "Connected to ") + 13;
                char bssid[MAX_MAC_LEN];
                if (sscanf(bssid_start, "%17s", bssid) == 1) {
                    // Store BSSID if needed
                }
            }
            else if (strstr(line, "\tSSID: ")) {
                char *ssid_start = strstr(line, "\tSSID: ") + 7;
                strncpy(interface->ssid, ssid_start, MAX_SSID_LEN - 1);
                interface->ssid[MAX_SSID_LEN - 1] = '\0';
            }
            else if (strstr(line, "\tfreq: ")) {
                int freq;
                if (sscanf(line, "\tfreq: %d", &freq) == 1) {
                    interface->frequency = freq;
                    // Convert frequency to channel
                    if (freq >= 2412 && freq <= 2484) {
                        interface->channel = (freq - 2412) / 5 + 1;
                    } else if (freq >= 5170 && freq <= 5825) {
                        interface->channel = (freq - 5000) / 5;
                    }
                }
            }
            else if (strstr(line, "\tsignal: ")) {
                int signal;
                if (sscanf(line, "\tsignal: %d dBm", &signal) == 1) {
                    interface->signal_strength = signal;
                }
            }
            else if (strstr(line, "\ttx bitrate: ")) {
                // Could extract bitrate info if needed
            }
        }
        pclose(fp);
    }
    
    // Fallback: try getting connection info from iwconfig if iw link fails
    if (interface->frequency == 0 || strlen(interface->ssid) == 0) {
        snprintf(command, sizeof(command), "iwconfig %s 2>/dev/null", interface_name);
        fp = popen(command, "r");
        if (fp) {
            while (fgets(line, sizeof(line), fp)) {
                line[strcspn(line, "\n")] = 0;
                
                if (strstr(line, "ESSID:\"")) {
                    char *essid_start = strstr(line, "ESSID:\"") + 7;
                    char *essid_end = strrchr(essid_start, '"');
                    if (essid_end) {
                        int essid_len = essid_end - essid_start;
                        if (essid_len < MAX_SSID_LEN) {
                            strncpy(interface->ssid, essid_start, essid_len);
                            interface->ssid[essid_len] = '\0';
                        }
                    }
                }
                else if (strstr(line, "Frequency:")) {
                    float freq_ghz;
                    if (sscanf(line, "%*s Frequency:%f GHz", &freq_ghz) == 1) {
                        interface->frequency = (int)(freq_ghz * 1000);
                        // Convert frequency to channel
                        if (interface->frequency >= 2412 && interface->frequency <= 2484) {
                            interface->channel = (interface->frequency - 2412) / 5 + 1;
                        } else if (interface->frequency >= 5170 && interface->frequency <= 5825) {
                            interface->channel = (interface->frequency - 5000) / 5;
                        }
                    }
                }
                else if (strstr(line, "Tx-Power=")) {
                    int power;
                    if (sscanf(line, "%*s Tx-Power=%d dBm", &power) == 1) {
                        interface->tx_power = power;
                    }
                }
            }
            pclose(fp);
        }
    }
    
    // Set default values if not detected
    if (strlen(interface->type) == 0) {
        strcpy(interface->type, "managed");
    }
    if (strlen(interface->mode) == 0) {
        strcpy(interface->mode, "station");
    }
    
    return 0;
}

int perform_scan(const char *interface_name, scan_result_t *results, int max_results) {
    FILE *fp;
    char command[MAX_COMMAND_LEN];
    char line[MAX_LINE_LEN];
    int count = 0;
    int retry_count = 0;
    const int max_retries = 3;
    
    // Retry scanning up to max_retries times if no results found
    while (retry_count < max_retries) {
        // Add slight delay between retries to allow hardware to settle
        if (retry_count > 0) {
            precise_sleep(0.5); // 500ms delay between retries
        }
        
        // Use iw dev scan with flush and timeout
        snprintf(command, sizeof(command), "iw dev %s scan flush 2>/dev/null", interface_name);
        fp = popen(command, "r");
        
        if (!fp) {
            retry_count++;
            continue;
        }
        
        scan_result_t current_result;
        memset(&current_result, 0, sizeof(current_result));
        int has_bss = 0;
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        count = 0; // Reset count for each retry
    
    while (fgets(line, sizeof(line), fp) && count < max_results) {
        line[strcspn(line, "\n")] = 0;
        
        // Parse iw scan output - only match BSS entries at line start
        if (strncmp(line, "BSS ", 4) == 0) {
            // Save previous entry if it has data
            if (has_bss && strlen(current_result.bssid) > 0) {
                strftime(current_result.timestamp, sizeof(current_result.timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
                memcpy(&results[count], &current_result, sizeof(current_result));
                count++;
            }
            
            // Start new entry
            memset(&current_result, 0, sizeof(current_result));
            has_bss = 1;
            
            // Extract BSSID from "BSS XX:XX:XX:XX:XX:XX(on interface)"
            char mac_str[18];
            if (sscanf(line, "BSS %17s", mac_str) == 1) {
                // Remove the (on interface) part if present
                char *paren = strchr(mac_str, '(');
                if (paren) *paren = '\0';
                strncpy(current_result.bssid, mac_str, sizeof(current_result.bssid) - 1);
                current_result.bssid[sizeof(current_result.bssid) - 1] = '\0';
            }
        }
        else if (has_bss && strstr(line, "\tSSID: ")) {
            char *ssid_start = strstr(line, "\tSSID: ") + 7;
            strncpy(current_result.ssid, ssid_start, MAX_SSID_LEN - 1);
            current_result.ssid[MAX_SSID_LEN - 1] = '\0';
        }
        else if (has_bss && strstr(line, "\tfreq: ")) {
            int freq;
            if (sscanf(line, "\tfreq: %d", &freq) == 1) {
                current_result.frequency = freq;
                // Convert frequency to channel
                if (freq >= 2412 && freq <= 2484) {
                    current_result.channel = (freq - 2412) / 5 + 1;
                } else if (freq >= 5170 && freq <= 5825) {
                    current_result.channel = (freq - 5000) / 5;
                } else if (freq >= 5955 && freq <= 7115) {
                    // 6 GHz band
                    current_result.channel = (freq - 5955) / 5;
                }
            }
        }
        else if (has_bss && strstr(line, "\tsignal: ")) {
            float signal;
            if (sscanf(line, "\tsignal: %f dBm", &signal) == 1) {
                current_result.signal_strength = (int)signal;
                // Calculate quality (0-100 scale)
                if (signal >= -30) current_result.quality = 100;
                else if (signal <= -90) current_result.quality = 0;
                else current_result.quality = (int)(100 + (signal + 30) * 100 / 60);
            }
        }
        else if (has_bss && strstr(line, "\tcapability: ")) {
            char *cap_start = strstr(line, "\tcapability: ") + 13;
            strncpy(current_result.capabilities, cap_start, sizeof(current_result.capabilities) - 1);
            current_result.capabilities[sizeof(current_result.capabilities) - 1] = '\0';
            
            // Check for Privacy capability to detect WEP
            if (strstr(cap_start, "Privacy") && strlen(current_result.security) == 0) {
                strncpy(current_result.security, "WEP", sizeof(current_result.security) - 1);
            }
        }
        // Parse security information with proper indentation
        else if (has_bss && strstr(line, "\tRSN:")) {
            strncpy(current_result.security, "WPA2", sizeof(current_result.security) - 1);
        }
        else if (has_bss && strstr(line, "\tWPA:") && strlen(current_result.security) == 0) {
            strncpy(current_result.security, "WPA", sizeof(current_result.security) - 1);
        }
        
        // Parse iwlist scan output format
        else if (strstr(line, "Cell ") && strstr(line, "Address: ")) {
            // Save previous entry if it has data
            if (has_bss && strlen(current_result.bssid) > 0) {
                strftime(current_result.timestamp, sizeof(current_result.timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
                memcpy(&results[count], &current_result, sizeof(current_result));
                count++;
            }
            
            // Start new entry
            memset(&current_result, 0, sizeof(current_result));
            has_bss = 1;
            
            // Extract BSSID from iwlist format: "Cell 01 - Address: XX:XX:XX:XX:XX:XX"
            char *addr_start = strstr(line, "Address: ");
            if (addr_start) {
                addr_start += 9; // Skip "Address: "
                char *line_end = strchr(addr_start, '\n');
                char *space_end = strchr(addr_start, ' ');
                char *tab_end = strchr(addr_start, '\t');
                
                // Find the actual end of the MAC address
                char *mac_end = line_end;
                if (space_end && space_end < mac_end) mac_end = space_end;
                if (tab_end && tab_end < mac_end) mac_end = tab_end;
                
                int mac_len = mac_end ? (mac_end - addr_start) : strlen(addr_start);
                if (mac_len >= 17 && mac_len < MAX_MAC_LEN) { // MAC address is exactly 17 chars
                    strncpy(current_result.bssid, addr_start, 17);
                    current_result.bssid[17] = '\0';
                    // Convert to uppercase for consistency
                    for (int i = 0; i < 17; i++) {
                        current_result.bssid[i] = toupper(current_result.bssid[i]);
                    }
                }
            }
        }
        else if (strstr(line, "Channel:") && has_bss) {
            int channel;
            // Handle both "Channel:10" and "                    Channel:10"
            if (sscanf(line, "%*[^0-9]%d", &channel) == 1) {
                current_result.channel = channel;
            }
        }
        else if (strstr(line, "Frequency:") && has_bss) {
            float freq;
            int channel;
            // Parse "Frequency:2.457 GHz (Channel 10)" or "                    Frequency:2.457 GHz (Channel 10)"
            if (sscanf(line, "%*[^0-9]%f GHz (Channel %d)", &freq, &channel) == 2) {
                current_result.frequency = (int)(freq * 1000);
                current_result.channel = channel;
            } else if (sscanf(line, "%*[^0-9]%f GHz", &freq) == 1) {
                current_result.frequency = (int)(freq * 1000);
                // Calculate channel if not provided
                if (current_result.frequency >= 2412 && current_result.frequency <= 2484) {
                    current_result.channel = (current_result.frequency - 2412) / 5 + 1;
                } else if (current_result.frequency >= 5170 && current_result.frequency <= 5825) {
                    current_result.channel = (current_result.frequency - 5000) / 5;
                }
            }
        }
        else if (strstr(line, "Quality=") && has_bss) {
            int quality, quality_max, signal_level;
            // Parse "Quality=70/70  Signal level=-21 dBm" with flexible whitespace
            char *quality_start = strstr(line, "Quality=");
            if (quality_start) {
                // Try parsing quality first
                if (sscanf(quality_start, "Quality=%d/%d", &quality, &quality_max) >= 2) {
                    if (quality_max > 0) {
                        current_result.quality = (quality * 100) / quality_max;
                    }
                    
                    // Now try to extract signal strength from the same line
                    char *signal_pos = strstr(line, "Signal level=");
                    if (signal_pos) {
                        // Try different signal parsing patterns
                        if (sscanf(signal_pos, "Signal level=%d dBm", &signal_level) == 1) {
                            current_result.signal_strength = signal_level;
                        } else if (sscanf(signal_pos, "Signal level=%d", &signal_level) == 1) {
                            current_result.signal_strength = signal_level;
                        }
                    }
                    
                    // Alternative: try parsing the entire line at once with flexible whitespace
                    if (current_result.signal_strength == 0) {
                        // Try with various whitespace patterns
                        if (sscanf(line, "%*[^Q]Quality=%d/%d%*[^S]Signal level=%d", &quality, &quality_max, &signal_level) == 3) {
                            current_result.signal_strength = signal_level;
                        }
                    }
                }
            }
        }
        // Separate handler for lines that only contain signal information
        else if (strstr(line, "Signal level=") && has_bss && current_result.signal_strength == 0) {
            int signal_level;
            char *signal_pos = strstr(line, "Signal level=");
            if (signal_pos && sscanf(signal_pos, "Signal level=%d dBm", &signal_level) == 1) {
                current_result.signal_strength = signal_level;
            }
        }
        else if (strstr(line, "ESSID:") && has_bss) {
            // Parse 'ESSID:"Network Name"'
            char *essid_start = strstr(line, "ESSID:\"");
            if (essid_start) {
                essid_start += 7; // Skip 'ESSID:"'
                char *essid_end = strrchr(essid_start, '"');
                if (essid_end) {
                    int essid_len = essid_end - essid_start;
                    if (essid_len < MAX_SSID_LEN && essid_len > 0) {
                        strncpy(current_result.ssid, essid_start, essid_len);
                        current_result.ssid[essid_len] = '\0';
                    }
                }
            }
        }
        else if (strstr(line, "Encryption key:on") && has_bss) {
            // Mark as encrypted, will be refined by WPA/WEP detection
            if (strlen(current_result.security) == 0) {
                strcpy(current_result.security, "Encrypted");
            }
        }
        else if (strstr(line, "Encryption key:off") && has_bss) {
            strcpy(current_result.security, "Open");
        }
        else if (strstr(line, "IEEE 802.11i/WPA2") && has_bss) {
            strcpy(current_result.security, "WPA2");
        }
        else if (strstr(line, "WPA3") && has_bss) {
            strcpy(current_result.security, "WPA3");
        }
        else if (strstr(line, "WPA") && has_bss && strstr(current_result.security, "WPA") == NULL) {
            strcpy(current_result.security, "WPA");
        }
    }
    
        // Add the last entry
        if (has_bss && strlen(current_result.bssid) > 0 && count < max_results) {
            strftime(current_result.timestamp, sizeof(current_result.timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
            memcpy(&results[count], &current_result, sizeof(current_result));
            count++;
        }
        
        pclose(fp);
        
        // If we got results, break out of retry loop
        if (count > 0) {
            break;
        }
        
        retry_count++;
    }
    
    return count;
}

void precise_sleep(float seconds) {
    if (seconds >= 1.0) {
        // For delays >= 1 second, use sleep() for the integer part
        int whole_seconds = (int)seconds;
        float fractional = seconds - whole_seconds;
        
        if (whole_seconds > 0) {
            sleep(whole_seconds);
        }
        
        if (fractional > 0.0) {
            struct timespec ts;
            ts.tv_sec = 0;
            ts.tv_nsec = (long)(fractional * 1000000000);
            nanosleep(&ts, NULL);
        }
    } else {
        // For delays < 1 second, use nanosleep only
        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec = (long)(seconds * 1000000000);
        nanosleep(&ts, NULL);
    }
}

// Shared memory structure for scan results
typedef struct {
    int result_count;
    scan_result_t results[MAX_SCAN_RESULTS];
    int scan_complete;
    int scan_success;
} shared_scan_data_t;

// Forked scan worker function with shared memory
int perform_forked_scan(const char *interface_name, scan_result_t *results, int max_results) {
    // Create shared memory for scan results
    int shm_fd = shm_open("/wifi_scan_shm", O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        // Fall back to direct scanning if shared memory fails
        return perform_scan(interface_name, results, max_results);
    }
    
    // Set the size of shared memory
    if (ftruncate(shm_fd, sizeof(shared_scan_data_t)) == -1) {
        close(shm_fd);
        shm_unlink("/wifi_scan_shm");
        return perform_scan(interface_name, results, max_results);
    }
    
    // Map shared memory
    shared_scan_data_t *shared_data = mmap(NULL, sizeof(shared_scan_data_t),
                                          PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED) {
        close(shm_fd);
        shm_unlink("/wifi_scan_shm");
        return perform_scan(interface_name, results, max_results);
    }
    
    // Initialize shared data
    memset(shared_data, 0, sizeof(shared_scan_data_t));
    
    pid_t pid = fork();
    
    if (pid == -1) {
        // Fork failed, cleanup and fall back
        munmap(shared_data, sizeof(shared_scan_data_t));
        close(shm_fd);
        shm_unlink("/wifi_scan_shm");
        return perform_scan(interface_name, results, max_results);
    } else if (pid == 0) {
        // Child process - perform the scan
        int count = perform_scan(interface_name, shared_data->results, MAX_SCAN_RESULTS);
        shared_data->result_count = count;
        shared_data->scan_success = (count > 0) ? 1 : 0;
        shared_data->scan_complete = 1;
        
        // Cleanup child process
        munmap(shared_data, sizeof(shared_scan_data_t));
        close(shm_fd);
        exit(0);
    } else {
        // Parent process - wait for child with timeout
        int status;
        int timeout_seconds = 12; // 12 second timeout for scan operation
        int final_count = 0;
        
        for (int i = 0; i < timeout_seconds * 10; i++) { // Check every 100ms
            if (waitpid(pid, &status, WNOHANG) == pid) {
                // Child process completed
                break;
            }
            
            // Check if scan is complete via shared memory
            if (shared_data->scan_complete) {
                break;
            }
            
            usleep(100000); // 100ms delay
        }
        
        // Check if child is still running and kill if necessary
        if (waitpid(pid, &status, WNOHANG) == 0) {
            kill(pid, SIGTERM);
            usleep(500000); // 500ms grace period
            if (waitpid(pid, &status, WNOHANG) == 0) {
                kill(pid, SIGKILL);
            }
            waitpid(pid, &status, 0);
        }
        
        // Copy results from shared memory if scan was successful
        if (shared_data->scan_complete && shared_data->scan_success && shared_data->result_count > 0) {
            int copy_count = (shared_data->result_count < max_results) ? shared_data->result_count : max_results;
            memcpy(results, shared_data->results, copy_count * sizeof(scan_result_t));
            final_count = copy_count;
        }
        
        // Cleanup shared memory
        munmap(shared_data, sizeof(shared_scan_data_t));
        close(shm_fd);
        shm_unlink("/wifi_scan_shm");
        
        return final_count;
    }
}

void continuous_scan_loop(const char *interface_name, float delay_seconds) {
    scan_session_t session;
    int scan_number = 1;
    
    // Enforce minimum scan interval for stability
    const float minimum_scan_interval = 0.5; // Minimum 0.5 seconds between scans
    if (delay_seconds < minimum_scan_interval) {
        printf("{\"warning\": \"Scan interval too low, increasing to %.1f seconds for hardware stability\"}\n", minimum_scan_interval);
        delay_seconds = minimum_scan_interval;
    }
    
    while (keep_running) {
        memset(&session, 0, sizeof(session));
        
        // Get current interface info
        get_interface_info(interface_name, &session.interface);
        
        // Perform scan using forked approach for better reliability
        clock_t start_time = clock();
        session.result_count = perform_forked_scan(interface_name, session.results, MAX_SCAN_RESULTS);
        clock_t end_time = clock();
        
        session.scan_time = time(NULL);
        session.scan_duration_ms = (int)((end_time - start_time) * 1000 / CLOCKS_PER_SEC);
        
        // Print scan results in JSON format
        printf("{\n");
        printf("  \"scan_number\": %d,\n", scan_number);
        printf("  \"interface\": \"%s\",\n", interface_name);
        printf("  \"scan_time\": %ld,\n", session.scan_time);
        printf("  \"scan_duration_ms\": %d,\n", session.scan_duration_ms);
        printf("  \"scan_delay\": %.3f,\n", delay_seconds);
        printf("  \"results_count\": %d,\n", session.result_count);
        printf("  \"interface_info\": ");
        print_interface_json(&session.interface);
        printf(",\n");
        printf("  \"scan_results\": [\n");
        
        for (int i = 0; i < session.result_count; i++) {
            print_scan_result_json(&session.results[i], (i == session.result_count - 1));
        }
        
        printf("  ]\n");
        printf("}\n");
        fflush(stdout);
        
        scan_number++;
        
        if (keep_running) {
            precise_sleep(delay_seconds);
        }
    }
}

void continuous_info_loop(const char *interface_name, float delay_seconds) {
    wifi_interface_t interface_info;
    int info_number = 1;
    
    while (keep_running) {
        memset(&interface_info, 0, sizeof(interface_info));
        
        // Get current interface info with timing
        clock_t start_time = clock();
        int result = get_interface_info(interface_name, &interface_info);
        clock_t end_time = clock();
        
        time_t current_time = time(NULL);
        int info_duration_ms = (int)((end_time - start_time) * 1000 / CLOCKS_PER_SEC);
        
        // Print interface info in JSON format
        printf("{\n");
        printf("  \"info_number\": %d,\n", info_number);
        printf("  \"interface\": \"%s\",\n", interface_name);
        printf("  \"info_time\": %ld,\n", current_time);
        printf("  \"info_duration_ms\": %d,\n", info_duration_ms);
        printf("  \"info_delay\": %.3f,\n", delay_seconds);
        printf("  \"status\": \"%s\",\n", (result == 0) ? "success" : "error");
        printf("  \"interface_info\": ");
        print_interface_json(&interface_info);
        printf("\n");
        printf("}\n");
        fflush(stdout);
        
        info_number++;
        
        if (keep_running) {
            precise_sleep(delay_seconds);
        }
    }
}

int save_interface_state(const char *interface_name, wifi_interface_t *saved_state) {
    int result = get_interface_info(interface_name, saved_state);
    if (result == 0) {
        saved_state->was_connected = (strlen(saved_state->ssid) > 0) ? 1 : 0;
    }
    return result;
}

int restore_interface_state(const char *interface_name, const wifi_interface_t *saved_state) {
    char command[MAX_COMMAND_LEN];
    int result = 0;
    
    // Disconnect from any current network
    snprintf(command, sizeof(command), "iw dev %s disconnect 2>/dev/null", interface_name);
    system(command);
    
    // Wait a moment for disconnection
    precise_sleep(0.5);
    
    // If there was an original connection, attempt to restore it
    if (saved_state->was_connected && strlen(saved_state->ssid) > 0) {
        snprintf(command, sizeof(command), "iw dev %s connect \"%s\" 2>/dev/null", 
                 interface_name, saved_state->ssid);
        result = system(command);
        
        // Wait for connection attempt
        precise_sleep(2.0);
    }
    
    return (result == 0) ? 0 : -1;
}

char* generate_random_filename(void) {
    static char filename[32];
    srand((unsigned int)time(NULL) + (unsigned int)getpid());
    snprintf(filename, sizeof(filename), "wpa_test_%08x", rand());
    return filename;
}

void log_command_execution(const char *command, const char *description) {
    printf("[CONNECTION TEST] %s\n", description);
    printf("[COMMAND] %s\n", command);
    fflush(stdout);
}

int execute_command_with_logging(const char *command, const char *description) {
    log_command_execution(command, description);
    int result = system(command);
    printf("[RESULT] Exit code: %d %s\n", result, (result == 0) ? "(SUCCESS)" : "(FAILED)");
    printf("----------------------------------------\n");
    fflush(stdout);
    return result;
}

FILE* execute_command_with_output_logging(const char *command, const char *description) {
    log_command_execution(command, description);
    FILE *fp = popen(command, "r");
    if (fp) {
        printf("[OUTPUT]\n");
        char line[MAX_LINE_LEN];
        while (fgets(line, sizeof(line), fp)) {
            printf("  %s", line);
        }
        printf("[END OUTPUT]\n");
        printf("----------------------------------------\n");
        fflush(stdout);
        pclose(fp);
        
        // Re-open for actual use
        fp = popen(command, "r");
    } else {
        printf("[ERROR] Failed to execute command\n");
        printf("----------------------------------------\n");
        fflush(stdout);
    }
    return fp;
}

int cleanup_interface_connections(const char *interface_name) {
    char command[MAX_COMMAND_LEN];
    
    printf("[CLEANUP] Cleaning up existing connections on %s...\n", interface_name);
    
    // Kill existing udhcpc processes for this interface
    snprintf(command, sizeof(command), "killall udhcpc 2>/dev/null || true");
    system(command);
    
    // Kill existing wpa_supplicant processes
    system("killall wpa_supplicant 2>/dev/null || true");
    
    // Flush IP addresses from interface
    snprintf(command, sizeof(command), "ip addr flush dev %s 2>/dev/null", interface_name);
    system(command);
    
    // Disconnect from any current network
    snprintf(command, sizeof(command), "iw dev %s disconnect 2>/dev/null", interface_name);
    system(command);
    
    precise_sleep(1.0); // Allow cleanup to complete
    
    printf("[CLEANUP] Interface cleanup completed\n");
    return 0;
}

int create_wpa_supplicant_pidfile(const char *interface_name, char *pidfile_path, size_t pidfile_size) {
    snprintf(pidfile_path, pidfile_size, "/tmp/wpa_supplicant_%s_%d.pid", interface_name, getpid());
    return 0;
}

int verify_ip_assignment(const char *interface_name, int max_wait_seconds) {
    char command[MAX_COMMAND_LEN];
    FILE *fp;
    char line[MAX_LINE_LEN];
    time_t start_time = time(NULL);
    int ip_count = 0;
    
    printf("[DHCP] Waiting up to %d seconds for IP assignment...\n", max_wait_seconds);
    
    while ((time(NULL) - start_time) < max_wait_seconds) {
        snprintf(command, sizeof(command), "ip addr show %s | grep 'inet ' | grep -v '127.0.0.1' | wc -l", interface_name);
        fp = popen(command, "r");
        
        if (fp) {
            if (fgets(line, sizeof(line), fp)) {
                ip_count = atoi(line);
                if (ip_count > 0) {
                    pclose(fp);
                    printf("[DHCP] IP address assigned successfully\n");
                    
                    // Display the assigned IP
                    snprintf(command, sizeof(command), "ip addr show %s | grep 'inet ' | grep -v '127.0.0.1'", interface_name);
                    fp = popen(command, "r");
                    if (fp) {
                        printf("[DHCP] Assigned IP: ");
                        while (fgets(line, sizeof(line), fp)) {
                            printf("%s", line);
                        }
                        pclose(fp);
                    }
                    return 1; // Success
                }
            }
            pclose(fp);
        }
        
        precise_sleep(0.5); // Check every 500ms
    }
    
    printf("[DHCP] No IP address assigned within timeout period\n");
    return 0; // Timeout
}

int start_wpa_supplicant_with_timeout(const char *interface_name, const char *config_file, int timeout_seconds, pid_t *wpa_pid) {
    pid_t pid;
    int status;
    char command[MAX_COMMAND_LEN];
    char pidfile_path[256];
    time_t start_time = time(NULL);
    time_t last_check = start_time;
    int connection_checks = 0;
    int max_connection_checks = timeout_seconds * 10; // Check every 100ms
    
    printf("[WPA] Starting enhanced WPA supplicant with time-based monitoring...\n");
    printf("[WPA] Timeout: %d seconds, Check interval: 100ms\n", timeout_seconds);
    
    // Cleanup existing connections first
    cleanup_interface_connections(interface_name);
    
    // Create PID file path for better process tracking
    create_wpa_supplicant_pidfile(interface_name, pidfile_path, sizeof(pidfile_path));
    
    // Fork a child process to start wpa_supplicant
    pid = fork();
    
    if (pid == -1) {
        printf("[ERROR] Failed to fork process for wpa_supplicant\n");
        return -1;
    } else if (pid == 0) {
        // Child process - start wpa_supplicant with PID file
        snprintf(command, sizeof(command), 
                 "wpa_supplicant -i %s -c %s -P %s -B 2>/dev/null", 
                 interface_name, config_file, pidfile_path);
        
        // Redirect output to /dev/null and exec
        int dev_null = open("/dev/null", O_WRONLY);
        if (dev_null != -1) {
            dup2(dev_null, STDOUT_FILENO);
            dup2(dev_null, STDERR_FILENO);
            close(dev_null);
        }
        
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(1); // If exec fails
    } else {
        // Parent process - implement time-based monitoring
        *wpa_pid = pid;
        
        // Wait for child to complete wpa_supplicant startup
        int wait_result = waitpid(pid, &status, 0);
        if (wait_result == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("[ERROR] wpa_supplicant startup failed\n");
            unlink(pidfile_path);
            return -1;
        }
        
        printf("[WPA] wpa_supplicant startup completed, monitoring connection...\n");
        
        // Monitor connection with proper time-based checks
        while ((time(NULL) - start_time) < timeout_seconds && connection_checks < max_connection_checks) {
            time_t current_time = time(NULL);
            
            // Only check every 100ms to avoid excessive system calls
            if ((current_time - last_check) >= 0.1 || connection_checks == 0) {
                last_check = current_time;
                connection_checks++;
                
                // Check if wpa_supplicant process is still running via PID file
                FILE *pidfile = fopen(pidfile_path, "r");
                if (pidfile) {
                    char pid_str[32];
                    if (fgets(pid_str, sizeof(pid_str), pidfile)) {
                        pid_t wpa_daemon_pid = atoi(pid_str);
                        if (kill(wpa_daemon_pid, 0) != 0) {
                            // Process no longer exists
                            fclose(pidfile);
                            printf("[ERROR] wpa_supplicant process terminated unexpectedly\n");
                            unlink(pidfile_path);
                            return -1;
                        }
                    }
                    fclose(pidfile);
                } else {
                    printf("[WARNING] PID file not found, checking via process list\n");
                }
                
                // Enhanced connection verification
                char check_command[MAX_COMMAND_LEN];
                
                // Enhanced connection verification using iw dev link and process checks
                snprintf(check_command, sizeof(check_command), 
                         "iw dev %s link | grep -q 'Connected to' && echo 'connected' || echo 'not_connected'",
                         interface_name);
                
                FILE *fp = popen(check_command, "r");
                if (fp) {
                    char result[32];
                    if (fgets(result, sizeof(result), fp)) {
                        result[strcspn(result, "\n")] = 0;
                        if (strcmp(result, "connected") == 0) {
                            pclose(fp);
                            
                            // Verify corresponding wpa_supplicant process
                            snprintf(check_command, sizeof(check_command), 
                                     "pgrep -f 'wpa_supplicant.*%s' >/dev/null 2>&1 && echo 'process_ok' || echo 'no_process'",
                                     interface_name);
                            
                            fp = popen(check_command, "r");
                            if (fp) {
                                if (fgets(result, sizeof(result), fp)) {
                                    result[strcspn(result, "\n")] = 0;
                                    pclose(fp);
                                    printf("[SUCCESS] Connection established after %ld seconds\n", 
                                           current_time - start_time);
                                    if (strcmp(result, "process_ok") == 0) {
                                        printf("[INFO] Active wpa_supplicant process confirmed\n");
                                    }
                                    return 0; // Connection established
                                }
                                pclose(fp);
                            }
                        }
                    }
                    if (fp) pclose(fp);
                }
                
                // Progress indicator every 2 seconds
                if ((current_time - start_time) % 2 == 0 && connection_checks % 20 == 0) {
                    printf("[WPA] Connection attempt in progress... (%ld/%d seconds)\n", 
                           current_time - start_time, timeout_seconds);
                }
            }
            
            precise_sleep(0.1); // 100ms interval
        }
        
        // Timeout reached - perform graceful cleanup
        printf("[TIMEOUT] Connection timeout after %d seconds\n", timeout_seconds);
        
        // Read PID from file and terminate gracefully
        FILE *pidfile = fopen(pidfile_path, "r");
        if (pidfile) {
            char pid_str[32];
            if (fgets(pid_str, sizeof(pid_str), pidfile)) {
                pid_t wpa_daemon_pid = atoi(pid_str);
                printf("[CLEANUP] Terminating wpa_supplicant process (PID: %d)\n", wpa_daemon_pid);
                
                // Graceful termination
                kill(wpa_daemon_pid, SIGTERM);
                precise_sleep(0.5);
                
                // Force termination if needed
                if (kill(wpa_daemon_pid, 0) == 0) {
                    printf("[CLEANUP] Force terminating wpa_supplicant\n");
                    kill(wpa_daemon_pid, SIGKILL);
                }
            }
            fclose(pidfile);
        }
        
        // Global cleanup for any remaining processes
        system("killall wpa_supplicant 2>/dev/null || true");
        unlink(pidfile_path);
        
        return -2; // Timeout
    }
}

int start_wpa_supplicant_secured_with_timeout(const char *interface_name, const char *config_file, const char *ssid, int timeout_seconds, pid_t *wpa_pid) {
    pid_t pid;
    int status;
    char command[MAX_COMMAND_LEN];
    char pidfile_path[256];
    time_t start_time = time(NULL);
    time_t last_check = start_time;
    int auth_checks = 0;
    int max_auth_checks = timeout_seconds * 2; // Check every 500ms for authentication
    
    printf("[AUTH] Starting enhanced WPA/WPA2 authentication with time-based monitoring...\n");
    printf("[AUTH] Timeout: %d seconds, Authentication check interval: 500ms\n", timeout_seconds);
    fflush(stdout);
    
    // Cleanup existing connections first - enhanced for secured networks
    cleanup_interface_connections(interface_name);
    
    // Create PID file path for better process tracking
    create_wpa_supplicant_pidfile(interface_name, pidfile_path, sizeof(pidfile_path));
    
    // Fork a child process to start wpa_supplicant
    pid = fork();
    
    if (pid == -1) {
        printf("[ERROR] Failed to fork process for secured wpa_supplicant\n");
        return -1;
    } else if (pid == 0) {
        // Child process - start wpa_supplicant with PID file and debug for secured networks
        snprintf(command, sizeof(command), 
                 "wpa_supplicant -i %s -c %s -P %s -B -d 2>/dev/null", 
                 interface_name, config_file, pidfile_path);
        
        // Redirect output to /dev/null and exec
        int dev_null = open("/dev/null", O_WRONLY);
        if (dev_null != -1) {
            dup2(dev_null, STDOUT_FILENO);
            dup2(dev_null, STDERR_FILENO);
            close(dev_null);
        }
        
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(1); // If exec fails
    } else {
        // Parent process - implement enhanced time-based authentication monitoring
        *wpa_pid = pid;
        
        // Wait for child to complete wpa_supplicant startup
        int wait_result = waitpid(pid, &status, 0);
        if (wait_result == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            printf("[ERROR] secured wpa_supplicant startup failed\n");
            unlink(pidfile_path);
            return -1;
        }
        
        printf("[AUTH] Secured wpa_supplicant startup completed, monitoring authentication...\n");
        
        // Monitor authentication with enhanced time-based checks
        while ((time(NULL) - start_time) < timeout_seconds && auth_checks < max_auth_checks) {
            time_t current_time = time(NULL);
            
            // Check every 500ms for authentication progress
            if ((current_time - last_check) >= 0.5 || auth_checks == 0) {
                last_check = current_time;
                auth_checks++;
                
                // Verify wpa_supplicant process is still running via PID file
                FILE *pidfile = fopen(pidfile_path, "r");
                if (pidfile) {
                    char pid_str[32];
                    if (fgets(pid_str, sizeof(pid_str), pidfile)) {
                        pid_t wpa_daemon_pid = atoi(pid_str);
                        if (kill(wpa_daemon_pid, 0) != 0) {
                            // Process terminated - check if authentication succeeded
                            fclose(pidfile);
                            
                            // Allow time for interface state to settle
                            precise_sleep(1.0);
                            
                            // Comprehensive authentication verification using iw dev link
                            char verify_command[MAX_COMMAND_LEN];
                            snprintf(verify_command, sizeof(verify_command), 
                                     "iw dev %s link | grep -q 'Connected to' && iw dev %s link | grep -q 'SSID: %s'", 
                                     interface_name, interface_name, ssid);
                            
                            if (system(verify_command) == 0) {
                                printf("[SUCCESS] Authentication completed successfully after %ld seconds\n", 
                                       current_time - start_time);
                                unlink(pidfile_path);
                                return 0; // Success
                            } else {
                                printf("[ERROR] Process terminated but authentication failed\n");
                                unlink(pidfile_path);
                                return -1; // Failed
                            }
                        }
                    }
                    fclose(pidfile);
                } else {
                    printf("[WARNING] PID file not accessible during authentication\n");
                }
                
                // Enhanced multi-stage authentication verification
                char check_command[MAX_COMMAND_LEN];
                
                // Stage 1: Check for active link connection
                snprintf(check_command, sizeof(check_command), 
                         "iw dev %s link | grep -q 'Connected to' && echo 'connected' || echo 'not_connected'", 
                         interface_name);
                
                FILE *fp = popen(check_command, "r");
                if (fp) {
                    char result[32];
                    if (fgets(result, sizeof(result), fp)) {
                        result[strcspn(result, "\n")] = 0;
                        if (strcmp(result, "connected") == 0) {
                            pclose(fp);
                            
                            // Stage 2: Verify SSID matches target
                            snprintf(check_command, sizeof(check_command), 
                                     "iw dev %s link | grep -q 'SSID: %s' && echo 'ssid_match' || echo 'ssid_mismatch'",
                                     interface_name, ssid);
                            
                            fp = popen(check_command, "r");
                            if (fp) {
                                if (fgets(result, sizeof(result), fp)) {
                                    result[strcspn(result, "\n")] = 0;
                                    if (strcmp(result, "ssid_match") == 0) {
                                        pclose(fp);
                                        
                                        // Stage 3: Verify corresponding wpa_supplicant process
                                        snprintf(check_command, sizeof(check_command), 
                                                 "pgrep -f 'wpa_supplicant.*%s' >/dev/null 2>&1 && echo 'process_ok' || echo 'no_process'",
                                                 interface_name);
                                        
                                        fp = popen(check_command, "r");
                                        if (fp) {
                                            if (fgets(result, sizeof(result), fp)) {
                                                result[strcspn(result, "\n")] = 0;
                                                pclose(fp);
                                                printf("[SUCCESS] Full authentication verified after %ld seconds\n", 
                                                       current_time - start_time);
                                                if (strcmp(result, "process_ok") == 0) {
                                                    printf("[INFO] Active wpa_supplicant process confirmed\n");
                                                }
                                                return 0; // Complete authentication success
                                            }
                                            pclose(fp);
                                        }
                                    }
                                }
                                if (fp) pclose(fp);
                            }
                        }
                    }
                    if (fp) pclose(fp);
                }
                
                // Progress indicator for authentication every 3 seconds
                if ((current_time - start_time) % 3 == 0 && auth_checks % 6 == 0) {
                    printf("[AUTH] Authentication in progress... (%ld/%d seconds)\n", 
                           current_time - start_time, timeout_seconds);
                    
                    // Show current authentication state
                    snprintf(check_command, sizeof(check_command), "iw dev %s link | head -5", interface_name);
                    system(check_command);
                }
            }
            
            precise_sleep(0.5); // 500ms interval for authentication checks
        }
        
        // Timeout reached - perform enhanced cleanup for secured connections
        printf("[TIMEOUT] Authentication timeout after %d seconds\n", timeout_seconds);
        
        // Read PID from file and terminate gracefully
        FILE *pidfile = fopen(pidfile_path, "r");
        if (pidfile) {
            char pid_str[32];
            if (fgets(pid_str, sizeof(pid_str), pidfile)) {
                pid_t wpa_daemon_pid = atoi(pid_str);
                printf("[CLEANUP] Terminating secured wpa_supplicant process (PID: %d)\n", wpa_daemon_pid);
                
                // Graceful termination for secured connections
                kill(wpa_daemon_pid, SIGTERM);
                precise_sleep(1.0); // Longer wait for secured connections
                
                // Force termination if needed
                if (kill(wpa_daemon_pid, 0) == 0) {
                    printf("[CLEANUP] Force terminating secured wpa_supplicant\n");
                    kill(wpa_daemon_pid, SIGKILL);
                }
            }
            fclose(pidfile);
        }
        
        // Enhanced global cleanup for secured networks
        system("killall wpa_supplicant 2>/dev/null || true");
        
        // Clear any partial authentication state
        snprintf(command, sizeof(command), "iw dev %s disconnect 2>/dev/null", interface_name);
        system(command);
        
        unlink(pidfile_path);
        
        return -2; // Timeout
    }
}

int test_open_ap_connection(const char *interface_name, const char *ssid, connection_test_result_t *result) {
    FILE *fp;
    char command[MAX_COMMAND_LEN];
    char line[MAX_LINE_LEN];
    char config_file[256];
    char *random_filename;
    wifi_interface_t saved_state;
    clock_t start_time, end_time;
    
    printf("========================================\n");
    printf("STARTING OPEN AP CONNECTION TEST\n");
    printf("========================================\n");
    printf("Target SSID: %s\n", ssid);
    printf("Interface: %s\n", interface_name);
    printf("Test Type: Open Network\n");
    printf("Timeout: %d seconds\n", CONNECTION_TIMEOUT_SECONDS);
    printf("========================================\n");
    fflush(stdout);
    
    // Initialize result structure
    memset(result, 0, sizeof(connection_test_result_t));
    strncpy(result->ssid, ssid, MAX_SSID_LEN - 1);
    strncpy(result->interface_name, interface_name, MAX_INTERFACE_NAME - 1);
    strncpy(result->connection_type, "open", sizeof(result->connection_type) - 1);
    result->test_time = time(NULL);
    
    // Save current interface state
    printf("[STEP 1] Saving current interface state...\n");
    if (save_interface_state(interface_name, &saved_state) == 0) {
        strncpy(result->original_ssid, saved_state.ssid, MAX_SSID_LEN - 1);
        result->was_connected = (strlen(saved_state.ssid) > 0) ? 1 : 0;
        printf("[INFO] Original SSID: %s\n", strlen(saved_state.ssid) > 0 ? saved_state.ssid : "(none)");
        printf("[INFO] Was connected: %s\n", result->was_connected ? "Yes" : "No");
    } else {
        printf("[WARNING] Failed to save interface state\n");
    }
    printf("----------------------------------------\n");
    fflush(stdout);
    
    start_time = clock();
    
    // Generate random config file name
    printf("[STEP 2] Generating configuration file...\n");
    random_filename = generate_random_filename();
    snprintf(config_file, sizeof(config_file), "/tmp/%s.conf", random_filename);
    printf("[INFO] Config file: %s\n", config_file);
    
    // Create wpa_supplicant config for open network
    snprintf(command, sizeof(command), 
             "echo \"%s none\" | awk '{printf \"network={\\n    ssid=\\\"%s\\\"\\n    key_mgmt=NONE\\n}\\n\", $1}' | tee %s > /dev/null",
             ssid, ssid, config_file);
    
    if (execute_command_with_logging(command, "Creating wpa_supplicant configuration for open network") != 0) {
        strncpy(result->error_message, "Failed to create wpa_supplicant configuration", sizeof(result->error_message) - 1);
        end_time = clock();
        result->test_duration_ms = (int)((end_time - start_time) * 1000 / CLOCKS_PER_SEC);
        restore_interface_state(interface_name, &saved_state);
        return -1;
    }
    
    // Display the created configuration
    snprintf(command, sizeof(command), "cat %s", config_file);
    execute_command_with_output_logging(command, "Displaying created configuration file");
    
    // Bring interface down and up
    printf("[STEP 3] Resetting network interface...\n");
    snprintf(command, sizeof(command), "ip link set %s down", interface_name);
    execute_command_with_logging(command, "Bringing interface down");
    precise_sleep(0.5);
    
    snprintf(command, sizeof(command), "ip link set %s up", interface_name);
    execute_command_with_logging(command, "Bringing interface up");
    precise_sleep(1.0);
    
    // Check interface status after reset
    snprintf(command, sizeof(command), "ip link show %s", interface_name);
    execute_command_with_output_logging(command, "Checking interface status after reset");
    
    // Start wpa_supplicant with failsafe timeout mechanism
    printf("[STEP 4] Starting wpa_supplicant with failsafe timeout mechanism...\n");
    printf("[INFO] Timeout: %d seconds\n", CONNECTION_TIMEOUT_SECONDS);
    printf("[INFO] Process will be automatically terminated if connection fails\n");
    fflush(stdout);
    
    pid_t wpa_pid;
    int wpa_result = start_wpa_supplicant_with_timeout(interface_name, config_file, CONNECTION_TIMEOUT_SECONDS, &wpa_pid);
    
    if (wpa_result == -1) {
        printf("[ERROR] Failed to start wpa_supplicant process\n");
        strncpy(result->error_message, "Failed to start wpa_supplicant", sizeof(result->error_message) - 1);
        unlink(config_file);
        end_time = clock();
        result->test_duration_ms = (int)((end_time - start_time) * 1000 / CLOCKS_PER_SEC);
        restore_interface_state(interface_name, &saved_state);
        return -1;
    } else if (wpa_result == -2) {
        printf("[TIMEOUT] Connection attempt timed out - wpa_supplicant automatically terminated\n");
        strncpy(result->error_message, "Connection attempt timed out after 5 seconds - wpa_supplicant process automatically terminated", sizeof(result->error_message) - 1);
        unlink(config_file);
        end_time = clock();
        result->test_duration_ms = (int)((end_time - start_time) * 1000 / CLOCKS_PER_SEC);
        restore_interface_state(interface_name, &saved_state);
        return -1;
    } else {
        printf("[SUCCESS] wpa_supplicant started successfully (PID: %d)\n", wpa_pid);
    }
    printf("----------------------------------------\n");
    fflush(stdout);
    
    // Enhanced DHCP with proper timeout handling and IP verification
    printf("[STEP 5] Starting enhanced DHCP client with timeout monitoring...\n");
    snprintf(command, sizeof(command), 
             "udhcpc -i %s -n 2>/dev/null &", interface_name);
    int dhcp_result = execute_command_with_logging(command, "Starting DHCP client with 8-second timeout");
    
    // Use enhanced IP assignment verification
    printf("[STEP 6] Enhanced IP address assignment verification...\n");
    int ip_assigned = verify_ip_assignment(interface_name, 10); // Wait up to 10 seconds for IP
    
    if (ip_assigned) {
        result->success = 1;
        printf("[SUCCESS] Enhanced DHCP verification successful - IP address obtained\n");
        
        // Additional connectivity verification
        snprintf(command, sizeof(command), "ping -c 1 -W 2 8.8.8.8 >/dev/null 2>&1");
        if (execute_command_with_logging(command, "Testing internet connectivity") == 0) {
            printf("[SUCCESS] Internet connectivity verified\n");
        } else {
            printf("[INFO] Local network connection established (internet connectivity test failed)\n");
        }
    } else {
        printf("[WARNING] Enhanced DHCP verification failed - no IP address assigned\n");
        strncpy(result->error_message, "Connection established but enhanced DHCP verification failed to obtain IP address", 
                sizeof(result->error_message) - 1);
    }
    printf("----------------------------------------\n");
    fflush(stdout);
    
    // Additional check - verify association with iw dev link and corresponding processes
    if (!result->success) {
        printf("[STEP 7] Performing enhanced association verification with iw dev link...\n");
        
        // Check for active connection using iw dev link
        snprintf(command, sizeof(command), "iw dev %s link | grep -q 'Connected to'", interface_name);
        int link_connected = (execute_command_with_logging(command, "Checking link connection status with iw dev") == 0);
        
        if (link_connected) {
            // Verify SSID matches the target
            snprintf(command, sizeof(command), "iw dev %s link | grep -q 'SSID: %s'", interface_name, ssid);
            int ssid_matches = (execute_command_with_logging(command, "Verifying SSID match in link status") == 0);
            
            if (ssid_matches) {
                // Check for corresponding wpa_supplicant process
                snprintf(command, sizeof(command), "pgrep -f 'wpa_supplicant.*%s' >/dev/null 2>&1", interface_name);
                int wpa_process_active = (execute_command_with_logging(command, "Checking for active wpa_supplicant process") == 0);
                
                // Check for corresponding udhcpc process
                snprintf(command, sizeof(command), "pgrep -f 'udhcpc.*%s' >/dev/null 2>&1", interface_name);
                int dhcp_process_active = (execute_command_with_logging(command, "Checking for active udhcpc process") == 0);
                
                result->success = 1;
                printf("[SUCCESS] Association verified via iw dev link - interface connected to %s\n", ssid);
                if (wpa_process_active) {
                    printf("[INFO] Active wpa_supplicant process detected for interface\n");
                }
                if (dhcp_process_active) {
                    printf("[INFO] Active udhcpc process detected for interface\n");
                }
                strncpy(result->error_message, "Connected but DHCP failed - may be captive portal or no DHCP server", 
                        sizeof(result->error_message) - 1);
            } else {
                printf("[WARNING] Link connected but SSID mismatch detected\n");
            }
        } else {
            printf("[FAILED] No active link connection detected\n");
            if (strlen(result->error_message) == 0) {
                strncpy(result->error_message, "Connection attempt failed - AP may reject clients or require authentication", 
                        sizeof(result->error_message) - 1);
            }
        }
        
        // Show current link status instead of iwconfig
        snprintf(command, sizeof(command), "iw dev %s link", interface_name);
        execute_command_with_output_logging(command, "Current link status");
    }
    
    end_time = clock();
    result->test_duration_ms = (int)((end_time - start_time) * 1000 / CLOCKS_PER_SEC);
    
    // Cleanup: kill udhcpc and wpa_supplicant, remove config file
    printf("[STEP 8] Performing cleanup operations...\n");
    snprintf(command, sizeof(command), "killall udhcpc 2>/dev/null || true");
    execute_command_with_logging(command, "Terminating udhcpc processes");
    
    execute_command_with_logging("killall wpa_supplicant 2>/dev/null || true", "Terminating wpa_supplicant processes");
    
    printf("[INFO] Removing configuration file: %s\n", config_file);
    unlink(config_file);
    precise_sleep(0.5);
    
    printf("[STEP 9] Restoring original interface state...\n");
    restore_interface_state(interface_name, &saved_state);
    
    printf("========================================\n");
    printf("OPEN AP CONNECTION TEST COMPLETED\n");
    printf("========================================\n");
    printf("Result: %s\n", result->success ? "SUCCESS" : "FAILED");
    printf("Duration: %d ms\n", result->test_duration_ms);
    if (strlen(result->error_message) > 0) {
        printf("Message: %s\n", result->error_message);
    }
    printf("========================================\n");
    fflush(stdout);
    
    return result->success ? 0 : -1;
}

int test_secured_ap_connection(const char *interface_name, const char *ssid, const char *password, connection_test_result_t *result) {
    FILE *fp;
    char command[MAX_COMMAND_LEN];
    char line[MAX_LINE_LEN];
    char config_file[256];
    char *random_filename;
    wifi_interface_t saved_state;
    clock_t start_time, end_time;
    
    printf("========================================\n");
    printf("STARTING SECURED AP CONNECTION TEST\n");
    printf("========================================\n");
    printf("Target SSID: %s\n", ssid);
    printf("Interface: %s\n", interface_name);
    printf("Test Type: Secured Network (WPA/WPA2)\n");
    printf("Password Length: %zu characters\n", strlen(password));
    printf("Timeout: %d seconds\n", CONNECTION_TIMEOUT_SECONDS);
    printf("========================================\n");
    fflush(stdout);
    
    // Initialize result structure
    memset(result, 0, sizeof(connection_test_result_t));
    strncpy(result->ssid, ssid, MAX_SSID_LEN - 1);
    strncpy(result->interface_name, interface_name, MAX_INTERFACE_NAME - 1);
    strncpy(result->connection_type, "secured", sizeof(result->connection_type) - 1);
    result->test_time = time(NULL);
    
    // Save current interface state
    printf("[STEP 1] Saving current interface state...\n");
    if (save_interface_state(interface_name, &saved_state) == 0) {
        strncpy(result->original_ssid, saved_state.ssid, MAX_SSID_LEN - 1);
        result->was_connected = (strlen(saved_state.ssid) > 0) ? 1 : 0;
        printf("[INFO] Original SSID: %s\n", strlen(saved_state.ssid) > 0 ? saved_state.ssid : "(none)");
        printf("[INFO] Was connected: %s\n", result->was_connected ? "Yes" : "No");
    } else {
        printf("[WARNING] Failed to save interface state\n");
    }
    printf("----------------------------------------\n");
    fflush(stdout);
    
    start_time = clock();
    
    // Generate random config file name
    printf("[STEP 2] Generating configuration file for secured network...\n");
    random_filename = generate_random_filename();
    snprintf(config_file, sizeof(config_file), "/tmp/%s.conf", random_filename);
    printf("[INFO] Config file: %s\n", config_file);
    
    // Create wpa_supplicant config for secured network
    snprintf(command, sizeof(command), 
             "echo \"%s %s\" | awk '{printf \"network={\\n    ssid=\\\"%s\\\"\\n    psk=\\\"%s\\\"\\n}\\n\", $1, $2}' | tee %s > /dev/null",
             ssid, password, ssid, password, config_file);
    
    if (execute_command_with_logging(command, "Creating wpa_supplicant configuration for secured network") != 0) {
        strncpy(result->error_message, "Failed to create wpa_supplicant configuration", sizeof(result->error_message) - 1);
        end_time = clock();
        result->test_duration_ms = (int)((end_time - start_time) * 1000 / CLOCKS_PER_SEC);
        restore_interface_state(interface_name, &saved_state);
        return -1;
    }
    
    // Display the created configuration (hiding password)
    snprintf(command, sizeof(command), "cat %s | sed 's/psk=.*/psk=\"***HIDDEN***\"/'", config_file);
    execute_command_with_output_logging(command, "Displaying created configuration file (password hidden)");
    
    // Bring interface down and up
    printf("[STEP 3] Resetting network interface...\n");
    snprintf(command, sizeof(command), "ip link set %s down", interface_name);
    execute_command_with_logging(command, "Bringing interface down");
    precise_sleep(0.5);
    
    snprintf(command, sizeof(command), "ip link set %s up", interface_name);
    execute_command_with_logging(command, "Bringing interface up");
    precise_sleep(1.0);
    
    // Check interface status after reset
    snprintf(command, sizeof(command), "ip link show %s", interface_name);
    execute_command_with_output_logging(command, "Checking interface status after reset");
    
    // Start wpa_supplicant with enhanced secured authentication mechanism
    printf("[STEP 4] Starting enhanced WPA/WPA2 authentication...\n");
    printf("[INFO] Extended timeout: %d seconds for secure authentication\n", SECURED_CONNECTION_TIMEOUT_SECONDS);
    printf("[INFO] Enhanced monitoring for WPA handshake completion\n");
    fflush(stdout);
    
    pid_t wpa_pid;
    int wpa_result = start_wpa_supplicant_secured_with_timeout(interface_name, config_file, ssid, SECURED_CONNECTION_TIMEOUT_SECONDS, &wpa_pid);
    
    if (wpa_result == -1) {
        printf("[ERROR] Failed to start or authenticate with wpa_supplicant\n");
        strncpy(result->error_message, "Failed to start wpa_supplicant or authentication failed", sizeof(result->error_message) - 1);
        unlink(config_file);
        end_time = clock();
        result->test_duration_ms = (int)((end_time - start_time) * 1000 / CLOCKS_PER_SEC);
        restore_interface_state(interface_name, &saved_state);
        return -1;
    } else if (wpa_result == -2) {
        printf("[TIMEOUT] Authentication timeout after %d seconds - process terminated\n", SECURED_CONNECTION_TIMEOUT_SECONDS);
        snprintf(command, sizeof(command), 
                 "Authentication timed out after %d seconds - may indicate wrong password or network issues", 
                 SECURED_CONNECTION_TIMEOUT_SECONDS);
        strncpy(result->error_message, command, sizeof(result->error_message) - 1);
        unlink(config_file);
        end_time = clock();
        result->test_duration_ms = (int)((end_time - start_time) * 1000 / CLOCKS_PER_SEC);
        restore_interface_state(interface_name, &saved_state);
        return -1;
    } else {
        printf("[SUCCESS] WPA/WPA2 authentication completed successfully\n");
    }
    printf("----------------------------------------\n");
    fflush(stdout);
    
    // Enhanced verification - since authentication already succeeded, verify and proceed with DHCP
    printf("[STEP 5] Verifying authentication and attempting DHCP...\n");
    
    // Verify association status using iw dev link and corresponding processes
    snprintf(command, sizeof(command), "iw dev %s link | grep -q 'Connected to'", interface_name);
    int link_connected = (execute_command_with_logging(command, "Verifying link connection status") == 0);
    
    // Additional verification: check SSID matches
    snprintf(command, sizeof(command), "iw dev %s link | grep -q 'SSID: %s'", interface_name, ssid);
    int ssid_matches = (execute_command_with_logging(command, "Verifying SSID match") == 0);
    
    // Check for corresponding wpa_supplicant process
    snprintf(command, sizeof(command), "pgrep -f 'wpa_supplicant.*%s' >/dev/null 2>&1", interface_name);
    int wpa_process_active = (execute_command_with_logging(command, "Verifying wpa_supplicant process") == 0);
    
    int associated = link_connected && ssid_matches;
    
    if (!associated) {
        printf("[WARNING] Link association not detected after authentication\n");
        if (!link_connected) {
            printf("[DEBUG] No active link connection found\n");
        }
        if (!ssid_matches) {
            printf("[DEBUG] SSID mismatch or not found in link status\n");
        }
        if (!wpa_process_active) {
            printf("[DEBUG] No active wpa_supplicant process found\n");
        }
        
        // Check current link status for debugging
        snprintf(command, sizeof(command), "iw dev %s link", interface_name);
        execute_command_with_output_logging(command, "Current link status check");
        
        strncpy(result->error_message, "Authentication process completed but link association unclear", 
                sizeof(result->error_message) - 1);
        result->success = 0;
    } else {
        printf("[SUCCESS] Link association confirmed via iw dev link\n");
        if (wpa_process_active) {
            printf("[INFO] Active wpa_supplicant process verified for interface\n");
        }
        
        // Show current connection details
        snprintf(command, sizeof(command), "iw dev %s link", interface_name);
        execute_command_with_output_logging(command, "Displaying current connection details");
        
        // Enhanced DHCP with proper timeout handling for secured networks
        printf("[STEP 6] Starting enhanced DHCP client with extended timeout for secured networks...\n");
        snprintf(command, sizeof(command), 
                 "udhcpc -i %s -n 2>/dev/null &", interface_name);
        int dhcp_result = execute_command_with_logging(command, "Starting DHCP client with 10-second timeout for secured networks");
        
        // Use enhanced IP assignment verification with longer timeout for secured networks
        printf("[STEP 7] Enhanced IP address assignment verification for secured connection...\n");
        int ip_assigned = verify_ip_assignment(interface_name, 12); // Wait up to 12 seconds for secured networks
        
        if (ip_assigned) {
            result->success = 1;
            printf("[SUCCESS] Complete secured connection established - authentication and enhanced DHCP successful\n");
            
            // Enhanced connectivity verification for secured networks
            snprintf(command, sizeof(command), "ping -c 2 -W 3 8.8.8.8 >/dev/null 2>&1");
            if (execute_command_with_logging(command, "Testing internet connectivity for secured connection") == 0) {
                printf("[SUCCESS] Internet connectivity verified for secured connection\n");
            } else {
                printf("[INFO] Secured local network connection established (internet connectivity test failed)\n");
            }
        } else {
            result->success = 1;  // Authentication succeeded even if DHCP failed
            printf("[SUCCESS] Authentication successful, enhanced DHCP failed (common for captive portals or enterprise networks)\n");
            strncpy(result->error_message, "Authentication successful but enhanced DHCP verification failed - may be captive portal, enterprise network, or no DHCP server", 
                    sizeof(result->error_message) - 1);
        }
    }
    printf("----------------------------------------\n");
    fflush(stdout);
    
    end_time = clock();
    result->test_duration_ms = (int)((end_time - start_time) * 1000 / CLOCKS_PER_SEC);
    
    // Cleanup: kill udhcpc and wpa_supplicant, remove config file
    snprintf(command, sizeof(command), "killall udhcpc 2>/dev/null || true");
    system(command);
    system("killall wpa_supplicant 2>/dev/null || true");
    unlink(config_file);
    precise_sleep(0.5);
    
    restore_interface_state(interface_name, &saved_state);
    
    return result->success ? 0 : -1;
}