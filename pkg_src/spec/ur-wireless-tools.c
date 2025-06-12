#include "ur-wireless-tools.h"
#include "cJSON.h"

static const char *interface_name_templates[] = {"wlan*", "wlp47s*", NULL};

int is_wifi_interface(const char *name) {
    for (int i = 0; interface_name_templates[i] != NULL; i++) {
        if (fnmatch(interface_name_templates[i], name, 0) == 0) {
            return 1;
        }
    }
    return 0;
}

void lookup_interface_configuration(const char *json, WifiInterfacesData *result) {
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            fprintf(stderr, "JSON error before: %s\n", error_ptr);
        }
        result->count = 0;
        return;
    }

    cJSON *interfaces = cJSON_GetObjectItemCaseSensitive(root, "wifi_interfaces");
    if (!cJSON_IsArray(interfaces)) {
        fprintf(stderr, "Invalid JSON format: wifi_interfaces not found or not an array\n");
        cJSON_Delete(root);
        result->count = 0;
        return;
    }

    result->count = 0;
    cJSON *interface;
    
    cJSON_ArrayForEach(interface, interfaces) {
        if (result->count >= 10) break; // Prevent overflow

        cJSON *name_item = cJSON_GetObjectItemCaseSensitive(interface, "name");
        if (!cJSON_IsString(name_item) || (name_item->valuestring == NULL)) {
            continue;
        }

        const char *name = name_item->valuestring;
        if (!is_wifi_interface(name)) continue;

        WifiInterfaceInfo *info = &result->wifi_interfaces[result->count];

        // Copy basic fields
        strncpy(info->name, name, MAX_NAME_LEN);
        
        cJSON *type_item = cJSON_GetObjectItemCaseSensitive(interface, "type");
        strncpy(info->type, cJSON_IsString(type_item) ? type_item->valuestring : "", MAX_TYPE_LEN);
        
        cJSON *status_item = cJSON_GetObjectItemCaseSensitive(interface, "status");
        strncpy(info->status, cJSON_IsString(status_item) ? status_item->valuestring : "", MAX_STATUS_LEN);
        
        cJSON *mac_item = cJSON_GetObjectItemCaseSensitive(interface, "mac_address");
        strncpy(info->mac_address, cJSON_IsString(mac_item) ? mac_item->valuestring : "", MAX_MAC_LEN);
        
        // Copy numeric fields
        cJSON *freq_item = cJSON_GetObjectItemCaseSensitive(interface, "frequency");
        info->frequency = cJSON_IsNumber(freq_item) ? freq_item->valueint : 0;
        
        cJSON *channel_item = cJSON_GetObjectItemCaseSensitive(interface, "channel");
        info->channel = cJSON_IsNumber(channel_item) ? channel_item->valueint : 0;
        
        cJSON *signal_item = cJSON_GetObjectItemCaseSensitive(interface, "signal_strength");
        info->signal_strength = cJSON_IsNumber(signal_item) ? signal_item->valueint : 0;
        
        cJSON *tx_item = cJSON_GetObjectItemCaseSensitive(interface, "tx_power");
        info->tx_power = cJSON_IsNumber(tx_item) ? tx_item->valueint : 0;
        
        // Copy string fields with potential NULL values
        cJSON *ssid_item = cJSON_GetObjectItemCaseSensitive(interface, "ssid");
        strncpy(info->ssid, cJSON_IsString(ssid_item) ? ssid_item->valuestring : "", MAX_SSID_LEN);
        
        cJSON *mode_item = cJSON_GetObjectItemCaseSensitive(interface, "mode");
        strncpy(info->mode, cJSON_IsString(mode_item) ? mode_item->valuestring : "", MAX_MODE_LEN);

        result->count++;
    }

    cJSON_Delete(root);
}