#include <stdint.h>
#include <time.h>

#define MAX_NAME_LEN        32
#define MAX_TYPE_LEN        32
#define MAX_STATUS_LEN      32
#define MAX_MAC_LEN         18   // MAC addresses are 17 chars + null terminator
#define MAX_SSID_LEN        64
#define MAX_PASS_LEN        32 
#define MAX_MODE_LEN        32
#define MAX_SECURITY_LEN    16
#define MAX_CAPABILITIES_LEN 128
#define MAX_TIMESTAMP_LEN   32
#define MAX_SCAN_RESULTS    20   // Maximum number of scan results to store

typedef struct {
    char bssid[MAX_MAC_LEN];         // BSSID (MAC of access point)
    char ssid[MAX_SSID_LEN];         // Network name
    int frequency;                   // Frequency in MHz
    int channel;                     // Channel number
    int signal_strength;             // Signal strength (dBm)
    int quality;                     // Quality percentage (0-100)
    char security[MAX_SECURITY_LEN]; // Security type (e.g., "WPA2")
    char capabilities[MAX_CAPABILITIES_LEN]; // Capabilities string
    char timestamp[MAX_TIMESTAMP_LEN]; // Timestamp of scan
} WifiScanResult;

typedef struct {
    time_t scan_time;                // Unix timestamp of scan
    int scan_duration_ms;            // Duration of scan in milliseconds
    int results_count;               // Number of scan results
} WifiScanInfo;

typedef struct {
    char name[MAX_NAME_LEN];         // Interface name (e.g., "wlan0")
    char type[MAX_TYPE_LEN];         // Interface type (e.g., "managed")
    char status[MAX_STATUS_LEN];     // Status (e.g., "UP", "DOWN")
    char mac_address[MAX_MAC_LEN];   // MAC address of interface
    int frequency;                   // Current frequency in MHz
    int channel;                     // Current channel
    char ssid[MAX_SSID_LEN];         // Connected SSID (empty if not connected)
    int signal_strength;             // Current signal strength (dBm)
    int tx_power;                    // Transmit power (dBm)
    char mode[MAX_MODE_LEN];         // Mode (e.g., "station", "AP")
} WifiInterfaceInfo;

extern WifiInterfaceInfo * interface_g_info;

typedef struct {
    WifiInterfaceInfo interface;     // Interface details
    WifiScanInfo scan_info;          // Scan metadata
    WifiScanResult scan_results[MAX_SCAN_RESULTS]; // Array of scan results
} WifiData;

typedef struct {
    WifiInterfaceInfo wifi_interfaces[10];
    int count;
} WifiInterfacesData;

#include <stdbool.h>

#define MAX_INTERFACE_NAME_LEN 32
#define MAX_STATUS_LEN 32
#define MAX_ACTION_LEN 32
#define MAX_MESSAGE_LEN 128

typedef struct {
    char status[MAX_STATUS_LEN];         // e.g., "setting_interface_down"
    char interface[MAX_INTERFACE_NAME_LEN];  // e.g., "wlan0"
} InterfaceStatusRequest;

typedef struct {
    char status[MAX_STATUS_LEN];         // e.g., "success"
    char action[MAX_ACTION_LEN];        // e.g., "interface_down"
    char interface[MAX_INTERFACE_NAME_LEN];  // e.g., "wlan0"
    char message[MAX_MESSAGE_LEN];      // e.g., "Interface set down successfully"
} InterfaceStatusResponse;

#define MAX_INTERFACE_LEN 32
#define MAX_CONN_TYPE_LEN 32
#define MAX_ERROR_MSG_LEN 256
#define MAX_STATUS_MSG_LEN 256

typedef struct {
    char ssid[MAX_SSID_LEN];                  // Network SSID
    char interface[MAX_INTERFACE_LEN];        // Interface name (e.g., "wlan0")
    char connection_type[MAX_CONN_TYPE_LEN];  // "secured" or "open"
    bool success;                             // Test result
    time_t test_time;                         // Unix timestamp
    int test_duration_ms;                     // Test duration in milliseconds
    bool was_previously_connected;            // Was connected before test
    char original_ssid[MAX_SSID_LEN];         // Original SSID (if was connected)
    char error_message[MAX_ERROR_MSG_LEN];    // Error message if failed
} ConnectionTestResult;

typedef struct {
    ConnectionTestResult connection_test;     // Test details
    char status[16];                          // "success" or "failed"
    char message[MAX_STATUS_MSG_LEN];         // Status message
} ConnectionTestResponse;

typedef enum {
    REQUEST_OPEN_CONNECTION_CHECK,
    REQUEST_SECURED_CONNECTION_CHECK,
    REQUEST_DELETE_SAVED_CONNECTION,
    REQUEST_UPDATE_SAVED_CONNECTION,
    REQUEST_LIST_SAVED_CONNECTION,
    REQUEST_SETUP_WIRELESS_AP,
}ur_wireless_tools_cmd_type_t;

typedef struct {
    ur_wireless_tools_cmd_type_t cmd_type;
    char ssid[MAX_SSID_LEN];
    char pass[MAX_PASS_LEN];
    char interface_name[MAX_INTERFACE_LEN]; // for single interface socs with dualband support automtically the setup lookup for the unique wireless inteface and sign it up in global 
} wireless_tools_request;

void lookup_interface_configuration(char* json);



