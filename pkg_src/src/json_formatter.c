#include "json_formatter.h"

char* escape_json_string(const char *str) {
    static char escaped[512];
    int j = 0;
    
    for (int i = 0; str[i] && j < sizeof(escaped) - 2; i++) {
        switch (str[i]) {
            case '"':
                escaped[j++] = '\\';
                escaped[j++] = '"';
                break;
            case '\\':
                escaped[j++] = '\\';
                escaped[j++] = '\\';
                break;
            case '\n':
                escaped[j++] = '\\';
                escaped[j++] = 'n';
                break;
            case '\r':
                escaped[j++] = '\\';
                escaped[j++] = 'r';
                break;
            case '\t':
                escaped[j++] = '\\';
                escaped[j++] = 't';
                break;
            default:
                escaped[j++] = str[i];
                break;
        }
    }
    escaped[j] = '\0';
    return escaped;
}

void print_interface_json(const wifi_interface_t *interface) {
    printf("    {\n");
    printf("      \"name\": \"%s\",\n", escape_json_string(interface->name));
    printf("      \"type\": \"%s\",\n", escape_json_string(interface->type));
    printf("      \"status\": \"%s\",\n", escape_json_string(interface->status));
    printf("      \"mac_address\": \"%s\",\n", escape_json_string(interface->mac));
    printf("      \"frequency\": %d,\n", interface->frequency);
    printf("      \"channel\": %d,\n", interface->channel);
    printf("      \"ssid\": \"%s\",\n", escape_json_string(interface->ssid));
    printf("      \"signal_strength\": %d,\n", interface->signal_strength);
    printf("      \"tx_power\": %d,\n", interface->tx_power);
    printf("      \"mode\": \"%s\"\n", escape_json_string(interface->mode));
    printf("    }");
}

void print_scan_result_json(const scan_result_t *result, int is_last) {
    printf("    {\n");
    printf("      \"bssid\": \"%s\",\n", escape_json_string(result->bssid));
    printf("      \"ssid\": \"%s\",\n", escape_json_string(result->ssid));
    printf("      \"frequency\": %d,\n", result->frequency);
    printf("      \"channel\": %d,\n", result->channel);
    printf("      \"signal_strength\": %d,\n", result->signal_strength);
    printf("      \"quality\": %d,\n", result->quality);
    printf("      \"security\": \"%s\",\n", escape_json_string(result->security));
    printf("      \"capabilities\": \"%s\",\n", escape_json_string(result->capabilities));
    printf("      \"timestamp\": \"%s\"\n", escape_json_string(result->timestamp));
    printf("    }");
    if (!is_last) {
        printf(",");
    }
    printf("\n");
}

void print_scan_results_json(const scan_session_t *session) {
    printf("{\n");
    printf("  \"interface\": {\n");
    printf("    \"name\": \"%s\",\n", escape_json_string(session->interface.name));
    printf("    \"type\": \"%s\",\n", escape_json_string(session->interface.type));
    printf("    \"status\": \"%s\",\n", escape_json_string(session->interface.status));
    printf("    \"mac_address\": \"%s\",\n", escape_json_string(session->interface.mac));
    printf("    \"frequency\": %d,\n", session->interface.frequency);
    printf("    \"channel\": %d,\n", session->interface.channel);
    printf("    \"ssid\": \"%s\",\n", escape_json_string(session->interface.ssid));
    printf("    \"signal_strength\": %d,\n", session->interface.signal_strength);
    printf("    \"tx_power\": %d,\n", session->interface.tx_power);
    printf("    \"mode\": \"%s\"\n", escape_json_string(session->interface.mode));
    printf("  },\n");
    printf("  \"scan_info\": {\n");
    printf("    \"scan_time\": %ld,\n", session->scan_time);
    printf("    \"scan_duration_ms\": %d,\n", session->scan_duration_ms);
    printf("    \"results_count\": %d\n", session->result_count);
    printf("  },\n");
    printf("  \"scan_results\": [\n");
    
    for (int i = 0; i < session->result_count; i++) {
        print_scan_result_json(&session->results[i], (i == session->result_count - 1));
    }
    
    printf("  ]\n");
    printf("}\n");
}

void print_continuous_scan_json(const char *interface_name, const scan_session_t *session) {
    printf("{\n");
    printf("  \"interface\": \"%s\",\n", escape_json_string(interface_name));
    printf("  \"scan_time\": %ld,\n", session->scan_time);
    printf("  \"scan_duration_ms\": %d,\n", session->scan_duration_ms);
    printf("  \"results_count\": %d,\n", session->result_count);
    printf("  \"interface_info\": ");
    print_interface_json(&session->interface);
    printf(",\n");
    printf("  \"scan_results\": [\n");
    
    for (int i = 0; i < session->result_count; i++) {
        print_scan_result_json(&session->results[i], (i == session->result_count - 1));
    }
    
    printf("  ]\n");
    printf("}\n");
}

void print_connection_test_json(const connection_test_result_t *result) {
    printf("{\n");
    printf("  \"connection_test\": {\n");
    printf("    \"ssid\": \"%s\",\n", escape_json_string(result->ssid));
    printf("    \"interface\": \"%s\",\n", escape_json_string(result->interface_name));
    printf("    \"connection_type\": \"%s\",\n", escape_json_string(result->connection_type));
    printf("    \"success\": %s,\n", result->success ? "true" : "false");
    printf("    \"test_time\": %ld,\n", result->test_time);
    printf("    \"test_duration_ms\": %d,\n", result->test_duration_ms);
    printf("    \"was_previously_connected\": %s,\n", result->was_connected ? "true" : "false");
    printf("    \"original_ssid\": \"%s\"\n", escape_json_string(result->original_ssid));
    
    if (!result->success && strlen(result->error_message) > 0) {
        printf(",\n    \"error_message\": \"%s\"\n", escape_json_string(result->error_message));
    }
    
    printf("  },\n");
    printf("  \"status\": \"%s\",\n", result->success ? "success" : "failed");
    printf("  \"message\": \"%s\"\n", result->success ? 
           "Connection test completed successfully - interface restored to original state" :
           "Connection test failed - interface restored to original state");
    printf("}\n");
}