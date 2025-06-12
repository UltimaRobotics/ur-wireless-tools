#include "wifi_scanner.h"
#include "interface_detector.h"
#include "json_formatter.h"
#include "scan_alternatives.h"

// Global variables
volatile int keep_running = 1;
float scan_delay = 5.0; // Default 5 seconds

void signal_handler(int sig) {
    (void)sig; // Suppress unused parameter warning
    keep_running = 0;
    printf("\n{\"status\": \"stopped\", \"message\": \"Scan interrupted by user\"}\n");
}

void print_usage(const char *program_name) {
    printf("{\n");
    printf("  \"usage\": {\n");
    printf("    \"program\": \"%s\",\n", program_name);
    printf("    \"commands\": [\n");
    printf("      {\n");
    printf("        \"command\": \"--list-interfaces\",\n");
    printf("        \"description\": \"List all available WiFi interfaces\"\n");
    printf("      },\n");
    printf("      {\n");
    printf("        \"command\": \"--scan [interface]\",\n");
    printf("        \"description\": \"Perform single scan on specified interface (auto-detect if not specified)\"\n");
    printf("      },\n");
    printf("      {\n");
    printf("        \"command\": \"--continuous [interface] [delay]\",\n");
    printf("        \"description\": \"Continuous scan with specified delay in seconds (default: 5.0, minimum: 0.1)\"\n");
    printf("      },\n");
    printf("      {\n");
    printf("        \"command\": \"--info [interface]\",\n");
    printf("        \"description\": \"Get detailed information about interface\"\n");
    printf("      },\n");
    printf("      {\n");
    printf("        \"command\": \"--continuous-info [interface] [delay]\",\n");
    printf("        \"description\": \"Continuous interface monitoring with specified delay in seconds (default: 5.0, minimum: 0.1)\"\n");
    printf("      },\n");
    printf("      {\n");
    printf("        \"command\": \"--open-ap-connect-verification <interface> <ssid>\",\n");
    printf("        \"description\": \"Test connection to open AP without persistent connection\"\n");
    printf("      },\n");
    printf("      {\n");
    printf("        \"command\": \"--secured-ap-connect-verification <interface> <ssid> <password>\",\n");
    printf("        \"description\": \"Test connection to secured AP with credentials without persistent connection\"\n");
    printf("      },\n");
    printf("      {\n");
    printf("        \"command\": \"--interface-down <interface>\",\n");
    printf("        \"description\": \"Set the specified interface down using 'ip link set <interface> down'\"\n");
    printf("      },\n");
    printf("      {\n");
    printf("        \"command\": \"--interface-up <interface>\",\n");
    printf("        \"description\": \"Set the specified interface up using 'ip link set <interface> up'\"\n");
    printf("      },\n");
    printf("      {\n");
    printf("        \"command\": \"--help\",\n");
    printf("        \"description\": \"Show this help message\"\n");
    printf("      }\n");
    printf("    ]\n");
    printf("  }\n");
    printf("}\n");
}

int main(int argc, char *argv[]) {
    wifi_interface_t interfaces[MAX_INTERFACES];
    int interface_count;
    char *selected_interface = NULL;
    
    // Set up signal handler
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    // Detect available WiFi interfaces
    interface_count = detect_wifi_interfaces(interfaces, MAX_INTERFACES);
    
    if (interface_count == 0) {
        printf("{\"error\": \"No WiFi interfaces found\", \"message\": \"Please ensure wireless interfaces are available\"}\n");
        return 1;
    }
    
    // Parse command line arguments
    if (strcmp(argv[1], "--list-interfaces") == 0) {
        printf("{\n");
        printf("  \"wifi_interfaces\": [\n");
        for (int i = 0; i < interface_count; i++) {
            print_interface_json(&interfaces[i]);
            if (i < interface_count - 1) {
                printf(",");
            }
            printf("\n");
        }
        printf("  ],\n");
        printf("  \"count\": %d\n", interface_count);
        printf("}\n");
        return 0;
    }
    
    else if (strcmp(argv[1], "--scan") == 0) {
        if (argc >= 3) {
            selected_interface = argv[2];
        } else {
            selected_interface = get_best_wifi_interface(interfaces, interface_count);
        }
        
        if (!selected_interface) {
            printf("{\"error\": \"No suitable WiFi interface found\"}\n");
            return 1;
        }
        
        scan_session_t session;
        memset(&session, 0, sizeof(session));
        
        // Get interface info
        get_interface_info(selected_interface, &session.interface);
        
        // Perform scan using forked approach
        clock_t start_time = clock();
        session.result_count = perform_forked_scan(selected_interface, session.results, MAX_SCAN_RESULTS);
        clock_t end_time = clock();
        
        session.scan_time = time(NULL);
        session.scan_duration_ms = (int)((end_time - start_time) * 1000 / CLOCKS_PER_SEC);
        
        print_scan_results_json(&session);
        return 0;
    }
    
    else if (strcmp(argv[1], "--continuous") == 0) {
        if (argc >= 3) {
            selected_interface = argv[2];
        } else {
            selected_interface = get_best_wifi_interface(interfaces, interface_count);
        }
        
        if (argc >= 4) {
            scan_delay = atof(argv[3]);
            if (scan_delay < 0.1) scan_delay = 5.0;
        }
        
        if (!selected_interface) {
            printf("{\"error\": \"No suitable WiFi interface found\"}\n");
            return 1;
        }
        
        printf("{\"status\": \"starting\", \"interface\": \"%s\", \"scan_delay\": %.3f}\n", 
               selected_interface, scan_delay);
        fflush(stdout);
        
        continuous_scan_loop(selected_interface, scan_delay);
        return 0;
    }
    
    else if (strcmp(argv[1], "--info") == 0) {
        if (argc >= 3) {
            selected_interface = argv[2];
        } else {
            selected_interface = get_best_wifi_interface(interfaces, interface_count);
        }
        
        if (!selected_interface) {
            printf("{\"error\": \"No suitable WiFi interface found\"}\n");
            return 1;
        }
        
        wifi_interface_t interface_info;
        if (get_interface_info(selected_interface, &interface_info) == 0) {
            printf("{\n");
            printf("  \"interface_info\": ");
            print_interface_json(&interface_info);
            printf("\n}\n");
        } else {
            printf("{\"error\": \"Failed to get interface information\", \"interface\": \"%s\"}\n", 
                   selected_interface);
            return 1;
        }
        return 0;
    }
    
    else if (strcmp(argv[1], "--continuous-info") == 0) {
        if (argc >= 3) {
            selected_interface = argv[2];
        } else {
            selected_interface = get_best_wifi_interface(interfaces, interface_count);
        }
        
        if (argc >= 4) {
            scan_delay = atof(argv[3]);
            if (scan_delay < 0.1) scan_delay = 5.0;
        }
        
        if (!selected_interface) {
            printf("{\"error\": \"No suitable WiFi interface found\"}\n");
            return 1;
        }
        
        printf("{\"status\": \"starting\", \"interface\": \"%s\", \"info_delay\": %.3f}\n", 
               selected_interface, scan_delay);
        fflush(stdout);
        
        continuous_info_loop(selected_interface, scan_delay);
        return 0;
    }
    
    else if (strcmp(argv[1], "--open-ap-connect-verification") == 0) {
        if (argc < 4) {
            printf("{\"error\": \"Missing required arguments\", \"usage\": \"--open-ap-connect-verification <interface> <ssid>\"}\n");
            return 1;
        }
        
        const char *interface = argv[2];
        const char *ssid = argv[3];
        connection_test_result_t result;
        
        // Test open AP connection
        test_open_ap_connection(interface, ssid, &result);
        print_connection_test_json(&result);
        
        return result.success ? 0 : 1;
    }
    
    else if (strcmp(argv[1], "--secured-ap-connect-verification") == 0) {
        if (argc < 5) {
            printf("{\"error\": \"Missing required arguments\", \"usage\": \"--secured-ap-connect-verification <interface> <ssid> <password>\"}\n");
            return 1;
        }
        
        const char *interface = argv[2];
        const char *ssid = argv[3];
        const char *password = argv[4];
        connection_test_result_t result;
        
        // Test secured AP connection
        test_secured_ap_connection(interface, ssid, password, &result);
        print_connection_test_json(&result);
        
        return result.success ? 0 : 1;
    }
    
    else if (strcmp(argv[1], "--interface-down") == 0) {
        if (argc < 3) {
            printf("{\"error\": \"Missing interface argument\", \"usage\": \"--interface-down <interface>\"}\n");
            return 1;
        }
        
        const char *interface = argv[2];
        char command[MAX_COMMAND_LEN];
        
        // Validate interface exists
        int interface_exists = 0;
        for (int i = 0; i < interface_count; i++) {
            if (strcmp(interfaces[i].name, interface) == 0) {
                interface_exists = 1;
                break;
            }
        }
        
        if (!interface_exists) {
            printf("{\"error\": \"Interface not found\", \"interface\": \"%s\", \"available_interfaces\": [", interface);
            for (int i = 0; i < interface_count; i++) {
                printf("\"%s\"", interfaces[i].name);
                if (i < interface_count - 1) printf(", ");
            }
            printf("]}\n");
            return 1;
        }
        
        printf("{\"status\": \"setting_interface_down\", \"interface\": \"%s\"}\n", interface);
        fflush(stdout);
        
        snprintf(command, sizeof(command), "ip link set %s down", interface);
        int result = system(command);
        
        if (result == 0) {
            printf("{\"status\": \"success\", \"action\": \"interface_down\", \"interface\": \"%s\", \"message\": \"Interface set down successfully\"}\n", interface);
        } else {
            printf("{\"status\": \"failed\", \"action\": \"interface_down\", \"interface\": \"%s\", \"error\": \"Failed to set interface down - check permissions\", \"exit_code\": %d}\n", interface, result);
            return 1;
        }
        return 0;
    }
    
    else if (strcmp(argv[1], "--interface-up") == 0) {
        if (argc < 3) {
            printf("{\"error\": \"Missing interface argument\", \"usage\": \"--interface-up <interface>\"}\n");
            return 1;
        }
        
        const char *interface = argv[2];
        char command[MAX_COMMAND_LEN];
        
        // Validate interface exists
        int interface_exists = 0;
        for (int i = 0; i < interface_count; i++) {
            if (strcmp(interfaces[i].name, interface) == 0) {
                interface_exists = 1;
                break;
            }
        }
        
        if (!interface_exists) {
            printf("{\"error\": \"Interface not found\", \"interface\": \"%s\", \"available_interfaces\": [", interface);
            for (int i = 0; i < interface_count; i++) {
                printf("\"%s\"", interfaces[i].name);
                if (i < interface_count - 1) printf(", ");
            }
            printf("]}\n");
            return 1;
        }
        
        printf("{\"status\": \"setting_interface_up\", \"interface\": \"%s\"}\n", interface);
        fflush(stdout);
        
        snprintf(command, sizeof(command), "ip link set %s up", interface);
        int result = system(command);
        
        if (result == 0) {
            printf("{\"status\": \"success\", \"action\": \"interface_up\", \"interface\": \"%s\", \"message\": \"Interface set up successfully\"}\n", interface);
        } else {
            printf("{\"status\": \"failed\", \"action\": \"interface_up\", \"interface\": \"%s\", \"error\": \"Failed to set interface up - check permissions\", \"exit_code\": %d}\n", interface, result);
            return 1;
        }
        return 0;
    }
    
    // New scan alternatives that replace shared memory files
    else if (strcmp(argv[1], "--scan-threaded") == 0) {
        if (argc >= 3) {
            selected_interface = argv[2];
        } else {
            selected_interface = get_best_wifi_interface(interfaces, interface_count);
        }
        
        if (!selected_interface) {
            printf("{\"error\": \"No suitable WiFi interface found\"}\n");
            return 1;
        }
        
        scan_result_t results[MAX_SCAN_RESULTS];
        int result_count = wifi_scan_direct_sync(selected_interface, results, MAX_SCAN_RESULTS);
        
        printf("{\n");
        printf("  \"scan_method\": \"threaded\",\n");
        printf("  \"interface\": \"%s\",\n", selected_interface);
        printf("  \"scan_time\": %ld,\n", time(NULL));
        printf("  \"results_count\": %d,\n", result_count);
        printf("  \"scan_results\": [\n");
        
        for (int i = 0; i < result_count; i++) {
            printf("    {\n");
            printf("      \"ssid\": \"%s\",\n", results[i].ssid);
            printf("      \"bssid\": \"%s\",\n", results[i].bssid);
            printf("      \"frequency\": %d,\n", results[i].frequency);
            printf("      \"signal_strength\": %d,\n", results[i].signal_strength);
            printf("      \"quality\": %d,\n", results[i].quality);
            printf("      \"encryption\": \"%s\"\n", results[i].security);
            printf("    }%s\n", (i < result_count - 1) ? "," : "");
        }
        printf("  ]\n");
        printf("}\n");
        return 0;
    }
    
    else if (strcmp(argv[1], "--scan-pipe") == 0) {
        if (argc >= 3) {
            selected_interface = argv[2];
        } else {
            selected_interface = get_best_wifi_interface(interfaces, interface_count);
        }
        
        if (!selected_interface) {
            printf("{\"error\": \"No suitable WiFi interface found\"}\n");
            return 1;
        }
        
        wifi_pipe_scan_context_t ctx;
        scan_result_t results[MAX_SCAN_RESULTS];
        
        if (wifi_scan_pipe_based_init(&ctx, selected_interface) == 0) {
            int result_count = wifi_scan_pipe_based_execute(&ctx, results, MAX_SCAN_RESULTS);
            
            printf("{\n");
            printf("  \"scan_method\": \"pipe-based\",\n");
            printf("  \"interface\": \"%s\",\n", selected_interface);
            printf("  \"scan_time\": %ld,\n", time(NULL));
            printf("  \"results_count\": %d,\n", result_count);
            printf("  \"scan_results\": [\n");
            
            for (int i = 0; i < result_count; i++) {
                printf("    {\n");
                printf("      \"ssid\": \"%s\",\n", results[i].ssid);
                printf("      \"bssid\": \"%s\",\n", results[i].bssid);
                printf("      \"frequency\": %d,\n", results[i].frequency);
                printf("      \"signal_strength\": %d,\n", results[i].signal_strength);
                printf("      \"quality\": %d,\n", results[i].quality);
                printf("      \"encryption\": \"%s\"\n", results[i].security);
                printf("    }%s\n", (i < result_count - 1) ? "," : "");
            }
            printf("  ]\n");
            printf("}\n");
            
            wifi_scan_pipe_based_cleanup(&ctx);
        } else {
            printf("{\"error\": \"Failed to initialize pipe-based scanning\"}\n");
            return 1;
        }
        return 0;
    }
    
    else if (strcmp(argv[1], "--scan-signal") == 0) {
        if (argc >= 3) {
            selected_interface = argv[2];
        } else {
            selected_interface = get_best_wifi_interface(interfaces, interface_count);
        }
        
        if (!selected_interface) {
            printf("{\"error\": \"No suitable WiFi interface found\"}\n");
            return 1;
        }
        
        wifi_signal_scan_context_t ctx;
        scan_result_t results[MAX_SCAN_RESULTS];
        
        if (wifi_scan_signal_based_init(&ctx, selected_interface) == 0) {
            int result_count = wifi_scan_signal_based_execute(&ctx, results, MAX_SCAN_RESULTS);
            
            printf("{\n");
            printf("  \"scan_method\": \"signal-based\",\n");
            printf("  \"interface\": \"%s\",\n", selected_interface);
            printf("  \"scan_time\": %ld,\n", time(NULL));
            printf("  \"results_count\": %d,\n", result_count);
            printf("  \"scan_results\": [\n");
            
            for (int i = 0; i < result_count; i++) {
                printf("    {\n");
                printf("      \"ssid\": \"%s\",\n", results[i].ssid);
                printf("      \"bssid\": \"%s\",\n", results[i].bssid);
                printf("      \"frequency\": %d,\n", results[i].frequency);
                printf("      \"signal_strength\": %d,\n", results[i].signal_strength);
                printf("      \"quality\": %d,\n", results[i].quality);
                printf("      \"encryption\": \"%s\"\n", results[i].security);
                printf("    }%s\n", (i < result_count - 1) ? "," : "");
            }
            printf("  ]\n");
            printf("}\n");
            
            wifi_scan_signal_based_cleanup(&ctx);
        } else {
            printf("{\"error\": \"Failed to initialize signal-based scanning\"}\n");
            return 1;
        }
        return 0;
    }
    
    else if (strcmp(argv[1], "--continuous-threaded") == 0) {
        if (argc >= 3) {
            selected_interface = argv[2];
        } else {
            selected_interface = get_best_wifi_interface(interfaces, interface_count);
        }
        
        if (argc >= 4) {
            scan_delay = atof(argv[3]);
            if (scan_delay < 0.1) scan_delay = 5.0;
        }
        
        if (!selected_interface) {
            printf("{\"error\": \"No suitable WiFi interface found\"}\n");
            return 1;
        }
        
        printf("{\"status\": \"starting\", \"method\": \"threaded\", \"interface\": \"%s\", \"scan_delay\": %.3f}\n", 
               selected_interface, scan_delay);
        fflush(stdout);
        
        wifi_continuous_scan_loop_threaded(selected_interface, scan_delay);
        return 0;
    }
    
    else if (strcmp(argv[1], "--continuous-pipe") == 0) {
        if (argc >= 3) {
            selected_interface = argv[2];
        } else {
            selected_interface = get_best_wifi_interface(interfaces, interface_count);
        }
        
        if (argc >= 4) {
            scan_delay = atof(argv[3]);
            if (scan_delay < 0.1) scan_delay = 5.0;
        }
        
        if (!selected_interface) {
            printf("{\"error\": \"No suitable WiFi interface found\"}\n");
            return 1;
        }
        
        printf("{\"status\": \"starting\", \"method\": \"pipe-based\", \"interface\": \"%s\", \"scan_delay\": %.3f}\n", 
               selected_interface, scan_delay);
        fflush(stdout);
        
        wifi_continuous_scan_loop_pipe(selected_interface, scan_delay);
        return 0;
    }
    
    else if (strcmp(argv[1], "--continuous-signal") == 0) {
        if (argc >= 3) {
            selected_interface = argv[2];
        } else {
            selected_interface = get_best_wifi_interface(interfaces, interface_count);
        }
        
        if (argc >= 4) {
            scan_delay = atof(argv[3]);
            if (scan_delay < 0.1) scan_delay = 5.0;
        }
        
        if (!selected_interface) {
            printf("{\"error\": \"No suitable WiFi interface found\"}\n");
            return 1;
        }
        
        printf("{\"status\": \"starting\", \"method\": \"signal-based\", \"interface\": \"%s\", \"scan_delay\": %.3f}\n", 
               selected_interface, scan_delay);
        fflush(stdout);
        
        wifi_continuous_scan_loop_signal(selected_interface, scan_delay);
        return 0;
    }
    
    else if (strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    else {
        printf("{\"error\": \"Unknown command\", \"command\": \"%s\"}\n", argv[1]);
        print_usage(argv[0]);
        return 1;
    }
}