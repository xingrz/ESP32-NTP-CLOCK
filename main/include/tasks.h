#ifndef __REALEARTH_TASKS__
#define __REALEARTH_TASKS__

#define LV_PROC_STACK_SIZE (16 * 1024)
#define UI_PROC_STACK_SIZE (16 * 1024)
#define LCD_PROC_STACK_SIZE (6 * 1024)
#define BTN_PROC_STACK_SIZE (2 * 1024)
#define WLAN_PROC_STACK_SIZE (4 * 1024)
#define NTP_PROC_STACK_SIZE (2 * 1024)
#define EARTH_PROC_STACK_SIZE (8 * 1024)

void lv_proc_task(void *arg);
void ui_proc_task(void *arg);
void lcd_proc_task(void *arg);
void btn_proc_task(void *arg);
void wlan_proc_task(void *arg);
void ntp_proc_task(void *arg);
void earth_proc_task(void *arg);

#endif  // __REALEARTH_TASKS__
