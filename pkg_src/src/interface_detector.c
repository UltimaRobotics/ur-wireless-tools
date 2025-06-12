#include "interface_detector.h"

int detect_wifi_interfaces(wifi_interface_t *interfaces, int max_interfaces) {
    return scan_network_interfaces(interfaces, max_interfaces);
}

int scan_network_interfaces(wifi_interface_t *interfaces, int max_interfaces) {
    FILE *fp;
    char line[MAX_LINE_LEN];
    int count = 0;
    
    // Try ip command first
    fp = popen("ip link show 2>/dev/null | grep -E '^[0-9]+:' | awk '{print $2}' | sed 's/:$//'", "r");
    
    if (!fp) {
        // Fallback: try reading from /proc/net/dev
        fp = popen("cat /proc/net/dev 2>/dev/null | tail -n +3 | awk -F: '{print $1}' | tr -d ' '", "r");
    }
    
    if (!fp) {
        // Fallback: try ls /sys/class/net/
        fp = popen("ls /sys/class/net/ 2>/dev/null", "r");
    }
    
    if (!fp) {
        // Last resort: try ifconfig
        fp = popen("ifconfig -a 2>/dev/null | grep -E '^[a-zA-Z0-9]+' | awk '{print $1}' | sed 's/:$//'", "r");
    }
    
    if (!fp) {
        return 0;
    }
    
    while (fgets(line, sizeof(line), fp) && count < max_interfaces) {
        line[strcspn(line, "\n")] = 0; // Remove newline
        
        if (strlen(line) > 0 && is_wireless_interface(line)) {
            strncpy(interfaces[count].name, line, MAX_INTERFACE_NAME - 1);
            interfaces[count].name[MAX_INTERFACE_NAME - 1] = '\0';
            
            // Get detailed information for this interface
            if (get_interface_details(line, &interfaces[count]) == 0) {
                count++;
            }
        }
    }
    
    pclose(fp);
    return count;
}

int is_wireless_interface(const char *interface_name) {
    char command[MAX_COMMAND_LEN];
    char path[256];
    
    // Check if wireless directory exists in sysfs
    snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", interface_name);
    if (access(path, F_OK) == 0) {
        return 1;
    }
    
    // Alternative: check using iw command
    snprintf(command, sizeof(command), "iw dev %s info >/dev/null 2>&1", interface_name);
    if (system(command) == 0) {
        return 1;
    }
    
    // Alternative: check using iwconfig
    snprintf(command, sizeof(command), "iwconfig %s >/dev/null 2>&1", interface_name);
    if (system(command) == 0) {
        return 1;
    }
    
    return 0;
}

int get_interface_details(const char *interface_name, wifi_interface_t *interface) {
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
            
            if (strstr(line, "type ")) {
                char *type_start = strstr(line, "type ") + 5;
                strncpy(interface->type, type_start, sizeof(interface->type) - 1);
            }
            else if (strstr(line, "channel ")) {
                sscanf(line, "%*s channel %d (%d MHz)", &interface->channel, &interface->frequency);
            }
            else if (strstr(line, "txpower ")) {
                float power;
                if (sscanf(line, "%*s txpower %f dBm", &power) == 1) {
                    interface->tx_power = (int)power;
                }
            }
        }
        pclose(fp);
    }
    
    // Get SSID if connected
    snprintf(command, sizeof(command), "iw dev %s link 2>/dev/null | grep SSID", interface_name);
    fp = popen(command, "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            char *ssid_start = strstr(line, "SSID: ");
            if (ssid_start) {
                ssid_start += 6;
                strncpy(interface->ssid, ssid_start, MAX_SSID_LEN - 1);
                interface->ssid[strcspn(interface->ssid, "\n")] = 0;
            }
        }
        pclose(fp);
    }
    
    // Get signal strength if connected
    snprintf(command, sizeof(command), "iw dev %s link 2>/dev/null | grep signal", interface_name);
    fp = popen(command, "r");
    if (fp) {
        if (fgets(line, sizeof(line), fp)) {
            int signal;
            if (sscanf(line, "%*s signal: %d dBm", &signal) == 1) {
                interface->signal_strength = signal;
            }
        }
        pclose(fp);
    }
    
    // Set default type if not detected
    if (strlen(interface->type) == 0) {
        strcpy(interface->type, "managed");
    }
    
    return 0;
}

int check_interface_capabilities(const char *interface_name) {
    char command[MAX_COMMAND_LEN];
    
    // Check if interface can scan
    snprintf(command, sizeof(command), "iw dev %s scan trigger test >/dev/null 2>&1", interface_name);
    return (system(command) == 0) ? 1 : 0;
}

char* get_best_wifi_interface(wifi_interface_t *interfaces, int interface_count) {
    static char best_interface[MAX_INTERFACE_NAME];
    int best_score = -1;
    
    for (int i = 0; i < interface_count; i++) {
        int score = 0;
        
        // Prefer interfaces that are UP
        if (strcmp(interfaces[i].status, "UP") == 0) {
            score += 10;
        }
        
        // Prefer managed mode
        if (strcmp(interfaces[i].type, "managed") == 0) {
            score += 5;
        }
        
        // Prefer interfaces with good signal if connected
        if (interfaces[i].signal_strength > -70 && interfaces[i].signal_strength < 0) {
            score += 3;
        }
        
        // Prefer interfaces with known MAC
        if (strlen(interfaces[i].mac) > 0) {
            score += 1;
        }
        
        if (score > best_score) {
            best_score = score;
            strncpy(best_interface, interfaces[i].name, MAX_INTERFACE_NAME - 1);
            best_interface[MAX_INTERFACE_NAME - 1] = '\0';
        }
    }
    
    return (best_score >= 0) ? best_interface : NULL;
}