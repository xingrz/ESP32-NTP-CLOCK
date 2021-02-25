#include <time.h>
#include <sys/time.h>

#include "common.h"
#include "esp_sntp.h"

#include "ntp.h"

#define TAG "ntp"

static const char *NTP_SERVER = "ntp.aliyun.com";

static void
time_sync_notification_cb(struct timeval *tv)
{
	struct tm timeinfo;
	char strftime_buf[64];
	localtime_r(&tv->tv_sec, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI(TAG, "Time synced to: %s", strftime_buf);
}

void
ntp_init(void)
{
	ESP_LOGI(TAG, "NTP init");

	setenv("TZ", "CST-8", 1);
	tzset();

	sntp_setoperatingmode(SNTP_OPMODE_POLL);
	sntp_setservername(0, NTP_SERVER);
	sntp_set_time_sync_notification_cb(time_sync_notification_cb);
	sntp_init();

	time_t now;
	struct tm timeinfo;
	char strftime_buf[64];
	time(&now);
	localtime_r(&now, &timeinfo);
	strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
	ESP_LOGI(TAG, "Current time: %s", strftime_buf);
}
