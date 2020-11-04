#include "task_btn.h"

static const char *TAG = "task_btn";

#define PIN_KEY_USER GPIO_NUM_0

static xQueueHandle btn_q = NULL;
static bool qrcode_shown = false;

extern const uint8_t pic_qrcode_start[] asm("_binary_pic_qrcode_jpg_start");

static void
drv_gpio_event(void *arg)
{
	uint32_t num = (uint32_t)arg;
	xQueueSendFromISR(btn_q, &num, NULL);
}

static void
drv_gpio_init()
{
	gpio_config_t input = {
			.intr_type = GPIO_INTR_NEGEDGE,
			.pin_bit_mask = (1UL << PIN_KEY_USER),
			.mode = GPIO_MODE_INPUT,
			.pull_up_en = GPIO_PULLUP_ENABLE,
	};

	gpio_config(&input);

	gpio_install_isr_service(0);
	gpio_isr_handler_add(PIN_KEY_USER, drv_gpio_event, (void *)PIN_KEY_USER);
}

void
btn_proc_task(void *arg)
{
	btn_q = xQueueCreate(3, sizeof(uint32_t));

	drv_gpio_init();

	xEventGroupSetBits((EventGroupHandle_t)arg, BOOT_TASK_BTN);

	uint32_t num;
	int val;
	while (1) {
		if (xQueueReceive(btn_q, &num, portMAX_DELAY) != pdPASS) {
			goto next;
		}

		val = gpio_get_level(num);
		ESP_LOGV(TAG, "GPIO: %d=%d", num, val);
		if (val) goto next;

		if (qrcode_shown) {
			lcd_clear_fg();
		} else {
			lcd_draw_fg(&pic_qrcode_start);
		}
		qrcode_shown = !qrcode_shown;

	next:
		vTaskDelay(10);
	}

	vTaskDelete(NULL);
}
