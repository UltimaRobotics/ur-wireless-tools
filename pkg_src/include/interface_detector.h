#ifndef INTERFACE_DETECTOR_H
#define INTERFACE_DETECTOR_H

#include "wifi_scanner.h"

// Interface detection functions
int scan_network_interfaces(wifi_interface_t *interfaces, int max_interfaces);
int is_wireless_interface(const char *interface_name);
int get_interface_details(const char *interface_name, wifi_interface_t *interface);
int check_interface_capabilities(const char *interface_name);
char* get_best_wifi_interface(wifi_interface_t *interfaces, int interface_count);

#endif // INTERFACE_DETECTOR_H