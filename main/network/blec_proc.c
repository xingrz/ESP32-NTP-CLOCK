#include "common.h"

#include "tasks.h"
#include "blec.h"
#include "blec_proc.h"

#include "task_lcd.h"
#include "wlan.h"

#define TAG "blec_proc"

#define SETUP_MAGIC 0x0811

#define CMD_SETUP 0x01
#define CMD_ACK 0x11

typedef struct {
	uint16_t magic;
	uint16_t cmd;
	uint16_t len;
} req_header_t;

static uint8_t recv_buf[200];
static uint16_t recv_pos = 0;
static req_header_t *recv_hdr = (req_header_t *)&recv_buf;

static void ble_handle_cmd(uint16_t cmd, void *data, uint16_t len);

void
blec_recv(void *buf, uint16_t len)
{
	ESP_LOGI(TAG, "recv: %d, pos: %d", len, recv_pos);

	if (recv_pos + len > sizeof(recv_buf)) {
		return;
	}

	if (recv_pos == 0) {
#if 0
		uint8_t *b = (uint8_t *)buf;
		printf("recv:");
		for (int i = 0; i < len; i++) {
			printf(" %02x", b[i]);
		}
		printf("\n");
#endif
		req_header_t *hdr = (req_header_t *)buf;
		if (hdr->magic != SETUP_MAGIC) {
			ESP_LOGW(TAG, "Invalid magic: %04x", hdr->magic);
			return;
		}
		if (hdr->len > 200) {
			ESP_LOGW(TAG, "Invalid length: %d", hdr->len);
			return;
		}
	}

	memcpy(recv_buf + recv_pos, buf, len);
	recv_pos += len;

	if (recv_pos < recv_hdr->len) {
		return;  // continue
	}

	uint64_t crc = 0;
	for (uint16_t i = 0; i < recv_hdr->len - 1; i++) {
		crc += recv_buf[i];
	}
	if (recv_buf[recv_hdr->len - 1] != (crc & 0xff)) {
		ESP_LOGW(TAG, "Invalid crc");
		goto reset;
	}

	uint16_t hdr_size = sizeof(req_header_t);
	ble_handle_cmd(recv_hdr->cmd, recv_buf + hdr_size, recv_hdr->len - hdr_size - 1);

reset:
	recv_pos = 0;
}

static void
ble_send_ack()
{
	uint8_t buf[sizeof(req_header_t) + 1] = {0};
	req_header_t *hdr = (req_header_t *)buf;
	hdr->magic = SETUP_MAGIC;
	hdr->cmd = CMD_ACK;
	hdr->len = sizeof(buf);

	uint64_t crc = 0;
	for (uint16_t i = 0; i < hdr->len - 1; i++) {
		crc += buf[i];
	}
	buf[hdr->len - 1] = (crc & 0xff);

	blec_send(buf, sizeof(buf));
}

static void
ble_handle_cmd(uint16_t cmd, void *data, uint16_t len)
{
	switch (cmd) {
		case CMD_SETUP: {
			ble_send_ack();
			// lcd_show_loading();

			char ssid[WLAN_SSID_LEN];
			char password[WLAN_PASSWORD_LEN];

			uint8_t ssid_len = ((uint8_t *)data)[0];
			if (ssid_len > sizeof(ssid) - 1) break;

			uint8_t password_len = len - 1 - ssid_len;
			if (password_len > sizeof(password) - 1) break;

			memcpy(ssid, data + 1, ssid_len);
			ssid[ssid_len] = 0;

			memcpy(password, data + 1 + ssid_len, password_len);
			password[password_len] = 0;

			ESP_LOGI(TAG, "Setup Wi-Fi, ssid: %s, password: %s", ssid, password);
			wlan_setup(ssid, password);
			break;
		}

		default:
			break;
	}
}
