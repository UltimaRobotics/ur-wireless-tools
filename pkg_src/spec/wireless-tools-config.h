#include <stdint.h>
#include <time.h>
#include <ur-wireless-tools.h>

#define UR_WIRELESS_TOOLS_CONFIG_PATH "/etc/ultima-stack/ur-wireless-tools/config.json"
#define UR_MAXIMUM_SAVED_NETWORKS 16
typedef enum{
    PHY_STATUS_STA,
    PHY_STATUS_AP
}phy_status;

typedef enum{
    ENABLED_WIFI,
    DISABLED_WIFI
}active_wifi;

typedef struct {
    char ssid[MAX_SSID_LEN];
    char security[MAX_SECURITY_LEN];
    char pass[MAX_PASS_LEN];
    char bssid[MAX_MAC_LEN];
    int frequency;
    char interface[MAX_INTERFACE_NAME_LEN];
}network_connection_template_t;

typedef struct{
    phy_status status;
    active_wifi activated;
    int num_registered_interfaces;
    network_connection_template_t* registered_interfaces;
    network_connection_template_t wireless_ap_last_config;
}wireless_registered_data_t;

extern wireless_registered_data_t registered_data_g_config;
extern char* network_g_file_path;

typedef enum{
    CONFIG_RECREATED,
    SUCCESFUL_CONFIG_LOADED,
    NETWORK_FOUND,
    NO_NETWORK_FOUND,
    CONFIG_NOT_EXISTANT,
    CONFIG_NOT_VALID
}config_management_error_t;

typedef struct{
    config_management_error_t result;
    char* result_message;
}config_cmd_result;

typedef enum {
    AUTOMATED_ACTION_ADD,
    AUTOMATED_ACTION_DELETE,
    AUTOMATED_ACTION_UPDATE
}config_action_type_t;

config_cmd_result load_wireless_config();
bool perform_network_check(network_connection_template_t current_network_template);

config_cmd_result perform_config_action(wireless_tools_request request);
