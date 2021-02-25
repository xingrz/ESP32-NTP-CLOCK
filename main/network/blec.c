#include "common.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "blec.h"
#include "blec_proc.h"

#define TAG "blec"

static const char *blec_ssid = "realEarth";

/* 4147bece-5856-479e-8743-951816886beb */
static const ble_uuid128_t blec_uuid = BLE_UUID128_INIT(0xeb, 0x6b, 0x88, 0x16, 0x18, 0x95, 0x43,
		0x87, 0x9e, 0x47, 0x56, 0x58, 0xce, 0xbe, 0x47, 0x41);

/* 90b0dfd8-8095-49cd-8b17-d94be0b427a3 */
static const ble_uuid128_t blec_char_uuid = BLE_UUID128_INIT(0xa3, 0x27, 0xb4, 0xe0, 0x4b, 0xd9,
		0x17, 0x8b, 0xcd, 0x49, 0x95, 0x80, 0xd8, 0xdf, 0xb0, 0x90);

static uint8_t blec_mfg_data[] = {
		0x7E,
		0x10,  // mfg id
		0x19, 0x96, 0x11,
		0x08,  // mfg data
};

static uint8_t own_addr_type;

static uint16_t blec_conn_handle;
static uint16_t blec_val_handle;

static bool blec_sync_ready = false;
static bool blec_advertising = false;

void ble_store_config_init(void);

static void blec_advertise(void);
static void blec_host_task(void *param);
static void blec_on_reset(int reason);
static void blec_on_sync(void);
static void blec_on_gatt_register(struct ble_gatt_register_ctxt *ctxt, void *arg);
static int blec_on_gap_event(struct ble_gap_event *event, void *arg);
static int blec_on_access(
		uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);

static const struct ble_gatt_chr_def characteristics[] = {
		{
				.uuid = &blec_char_uuid.u,
				.access_cb = blec_on_access,
				.val_handle = &blec_val_handle,
				.flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
		},
		{0},
};

static const struct ble_gatt_svc_def services[] = {
		{
				.type = BLE_GATT_SVC_TYPE_PRIMARY,
				.uuid = &blec_uuid.u,
				.characteristics = characteristics,
		},
		{0},
};

int
blec_init(void)
{
	int rc;

	ESP_ERROR_CHECK(esp_nimble_hci_and_controller_init());
	nimble_port_init();

	ble_hs_cfg.reset_cb = blec_on_reset;
	ble_hs_cfg.sync_cb = blec_on_sync;
	ble_hs_cfg.gatts_register_cb = blec_on_gatt_register;
	ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
	ble_hs_cfg.sm_sc = 0;

	ble_svc_gap_init();
	ble_svc_gatt_init();

	rc = ble_gatts_count_cfg(services);
	assert(rc == 0);

	rc = ble_gatts_add_svcs(services);
	assert(rc == 0);

	rc = ble_svc_gap_device_name_set(blec_ssid);
	assert(rc == 0);

	ble_store_config_init();

	nimble_port_freertos_init(blec_host_task);

	return 0;
}

void
blec_send(void *buf, uint16_t len)
{
	struct os_mbuf *om = ble_hs_mbuf_from_flat(buf, len);
	int rc = ble_gattc_notify_custom(blec_conn_handle, blec_val_handle, om);
	assert(rc == 0);
}

void
blec_adv_start()
{
	blec_advertising = true;
	blec_advertise();
}

void
blec_adv_stop()
{
	blec_advertising = false;
	ble_gap_adv_stop();
}

static void
blec_advertise(void)
{
	int rc;

	if (!blec_sync_ready || !blec_advertising) {
		return;
	}

	const char *name = ble_svc_gap_device_name();

	/**
	 *  Set the advertisement data included in our advertisements:
	 *     o Flags (indicates advertisement type and other general info).
	 *     o Advertising tx power.
	 *     o Device name.
	 *     o 16-bit service UUIDs (alert notifications).
	 */
	struct ble_hs_adv_fields fields = {
			/* Advertise two flags:
			 *     o Discoverability in forthcoming advertisement (general)
			 *     o BLE-only (BR/EDR unsupported).
			 */
			.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,

			/* Indicate that the TX power level field should be included; have the
			 * stack fill this value automatically.  This is done by assigning the
			 * special value BLE_HS_ADV_TX_PWR_LVL_AUTO.
			 */
			.tx_pwr_lvl_is_present = 1,
			.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO,

			.mfg_data = blec_mfg_data,
			.mfg_data_len = sizeof(blec_mfg_data),

			.name = (uint8_t *)name,
			.name_len = strlen(name),
			.name_is_complete = 1,
	};

	rc = ble_gap_adv_set_fields(&fields);
	if (rc != 0) {
		ESP_LOGE(TAG, "error setting advertisement data; rc=%d", rc);
		return;
	}

	/* Begin advertising. */
	struct ble_gap_adv_params adv_params = {
			.conn_mode = BLE_GAP_CONN_MODE_UND,
			.disc_mode = BLE_GAP_DISC_MODE_GEN,
	};

	rc = ble_gap_adv_start(
			own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, blec_on_gap_event, NULL);
	if (rc != 0) {
		ESP_LOGE(TAG, "error enabling advertisement; rc=%d", rc);
		return;
	}
}

static void
blec_host_task(void *arg)
{
	ESP_LOGI(TAG, "BLE host task started");
	nimble_port_run();
	nimble_port_freertos_deinit();
}

static void
blec_on_reset(int reason)
{
	ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

static void
blec_on_sync(void)
{
	int rc;

	rc = ble_hs_util_ensure_addr(0);
	assert(rc == 0);

	/* Figure out address to use while advertising (no privacy for now) */
	rc = ble_hs_id_infer_auto(0, &own_addr_type);
	if (rc != 0) {
		ESP_LOGE(TAG, "error determining address type; rc=%d", rc);
		return;
	}

	uint8_t addr[6] = {0};
	rc = ble_hs_id_copy_addr(own_addr_type, addr, NULL);
	ESP_LOGI(TAG, "Device Address: %02X:%02X:%02X:%02X:%02X:%02X", addr[0], addr[1], addr[2],
			addr[3], addr[4], addr[5]);

	blec_sync_ready = true;
	blec_advertise();
}

static void
blec_on_gatt_register(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
	char buf[BLE_UUID_STR_LEN];
	switch (ctxt->op) {
		case BLE_GATT_REGISTER_OP_SVC:
			ESP_LOGI(TAG, "registered service %s with handle=%d",
					ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf), ctxt->svc.handle);
			break;
		case BLE_GATT_REGISTER_OP_CHR:
			ESP_LOGI(TAG, "registering characteristic %s with def_handle=%d val_handle=%d",
					ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf), ctxt->chr.def_handle,
					ctxt->chr.val_handle);
			break;
		case BLE_GATT_REGISTER_OP_DSC:
			ESP_LOGI(TAG, "registering descriptor %s with handle=%d",
					ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf), ctxt->dsc.handle);
			break;
		default:
			break;
	}
}

static int
blec_on_gap_event(struct ble_gap_event *event, void *arg)
{
	struct ble_gap_conn_desc desc;
	int rc;

	switch (event->type) {
		case BLE_GAP_EVENT_CONNECT:
			ESP_LOGI(TAG, "connection %s; status=%d",
					event->connect.status == 0 ? "established" : "failed", event->connect.status);
			if (event->connect.status != 0) {
				blec_advertise();
			}
			blec_conn_handle = event->connect.conn_handle;
			return 0;

		case BLE_GAP_EVENT_DISCONNECT:
			ESP_LOGI(TAG, "disconnect; reason=%d", event->disconnect.reason);
			blec_advertise();
			return 0;

		case BLE_GAP_EVENT_CONN_UPDATE:
			ESP_LOGI(TAG, "connection updated; status=%d", event->conn_update.status);
			return 0;

		case BLE_GAP_EVENT_ADV_COMPLETE:
			ESP_LOGI(TAG, "advertise complete; reason=%d", event->adv_complete.reason);
			blec_advertise();
			return 0;

		case BLE_GAP_EVENT_ENC_CHANGE:
			ESP_LOGI(TAG, "encryption change event; status=%d", event->enc_change.status);
			return 0;

		case BLE_GAP_EVENT_SUBSCRIBE:
			ESP_LOGI(TAG,
					"subscribe event; conn_handle=%d attr_handle=%d "
					"reason=%d prevn=%d curn=%d previ=%d curi=%d",
					event->subscribe.conn_handle, event->subscribe.attr_handle,
					event->subscribe.reason, event->subscribe.prev_notify,
					event->subscribe.cur_notify, event->subscribe.prev_indicate,
					event->subscribe.cur_indicate);
			return 0;

		case BLE_GAP_EVENT_MTU:
			ESP_LOGI(TAG, "mtu update event; conn_handle=%d cid=%d mtu=%d", event->mtu.conn_handle,
					event->mtu.channel_id, event->mtu.value);
			return 0;

		case BLE_GAP_EVENT_REPEAT_PAIRING:
			/* We already have a bond with the peer, but it is attempting to
			 * establish a new secure link.  This app sacrifices security for
			 * convenience: just throw away the old bond and accept the new link.
			 */

			/* Delete the old bond. */
			rc = ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc);
			assert(rc == 0);
			ble_store_util_delete_peer(&desc.peer_id_addr);

			/* Return BLE_GAP_REPEAT_PAIRING_RETRY to indicate that the host should
			 * continue with the pairing operation.
			 */
			return BLE_GAP_REPEAT_PAIRING_RETRY;

		case BLE_GAP_EVENT_PASSKEY_ACTION:
			ESP_LOGI(TAG, "PASSKEY_ACTION_EVENT not supported");
			return 0;
	}

	return 0;
}

static int
blec_on_access(
		uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
	int rc;
	uint16_t write_len = 0;
	char write_buf[200];

	switch (ctxt->op) {
		case BLE_GATT_ACCESS_OP_WRITE_CHR:
			rc = ble_hs_mbuf_to_flat(ctxt->om, &write_buf, sizeof(write_buf), &write_len);
			ESP_LOGI(TAG, "BLE write (%d)", write_len);
			blec_recv(write_buf, write_len);
			return rc;

		default:
			return BLE_ATT_ERR_UNLIKELY;
	}
}
