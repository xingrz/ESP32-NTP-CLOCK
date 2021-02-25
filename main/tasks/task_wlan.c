#include "common.h"
#include "freertos/queue.h"

#include "tasks.h"
#include "task_lcd.h"
#include "wlan.h"

#define TAG "task_wlan"

static xQueueHandle wlan_q = NULL;

typedef enum {
	e_wlan_set = 1,
} wlan_msg_e;

typedef struct {
	wlan_msg_e what;
	char ssid[WLAN_SSID_LEN];
	char password[WLAN_PASSWORD_LEN];
} wlan_msg_t;

static char wlan_ssid[WLAN_SSID_LEN] = {0};

void
wlan_proc_task(void *arg)
{
	wlan_q = xQueueCreate(3, sizeof(wlan_msg_t));

	ESP_LOGI(TAG, "Init Wi-Fi...");
	wlan_init();

	if (wlan_configured(wlan_ssid)) {
		// lcd_show_loading();
		if (wlan_connect() == ESP_OK) {
			ESP_LOGI(TAG, "Wi-Fi connected");
		} else {
			// lcd_show_offline();
			ESP_LOGI(TAG, "Wi-Fi not connectable");
		}
	} else {
		// lcd_show_offline();
		ESP_LOGI(TAG, "Wi-Fi not set");
	}

	wlan_msg_t msg = {0};
	while (1) {
		if (xQueueReceive(wlan_q, &msg, portMAX_DELAY) != pdPASS) {
			goto next;
		}

		switch (msg.what) {
			case e_wlan_set:
				ESP_LOGI(TAG, "Connecting to Wi-Fi \"%s\"...", msg.ssid);
				wlan_set(msg.ssid, msg.password);
				if (wlan_connect() == ESP_OK) {
					ESP_LOGI(TAG, "Settings stored");
					vTaskDelay(3 * 1000 / portTICK_PERIOD_MS);
					esp_restart();
				} else {
					// lcd_show_qrcode();
					ESP_LOGI(TAG, "Setting up failed");
					memset(wlan_ssid, 0, sizeof(wlan_ssid));
				}
				break;
		}

	next:
		vTaskDelay(10);
	}

	vTaskDelete(NULL);
}

void
wlan_setup(char *ssid, char *password)
{
	if (strcmp(wlan_ssid, ssid) == 0) {
		ESP_LOGI(TAG, "Setting up %s, ignored", ssid);
		return;
	}
	strcpy(wlan_ssid, ssid);

	wlan_msg_t msg = {.what = e_wlan_set};
	strcpy(msg.ssid, ssid);
	strcpy(msg.password, password);
	xQueueSend(wlan_q, &msg, 0);
}
