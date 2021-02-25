#ifndef __REALEARTH_WLAN__
#define __REALEARTH_WLAN__

#include "esp_err.h"

#define WLAN_SSID_LEN 33
#define WLAN_PASSWORD_LEN 64

esp_err_t wlan_init(void);
esp_err_t wlan_connect();
esp_err_t wlan_set(const char *ssid, const char *password);
esp_err_t wlan_reset(void);
bool wlan_configured(char *ssid);
void wlan_setup(char *ssid, char *password);

#endif  // __REALEARTH_WLAN__
