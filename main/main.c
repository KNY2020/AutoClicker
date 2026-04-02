/**
 * @file  main.c
 * @brief ESP32-C3 手机自动连点设备 — 应用入口
 *
 * 功能概述：
 *   - 上电初始化各模块（Config / BLE HID / ClickEngine / LED）
 *   - 两个按键：START/STOP（GPIO3）、MODE 切换（GPIO4）
 *   - 按键通过 GPIO 中断 + FreeRTOS 队列处理，完全非阻塞
 *   - 所有状态通过 LED 指示
 *
 * 硬件连接：
 *   GPIO3  — BTN_START (按下=GND，内部上拉)
 *   GPIO4  — BTN_MODE  (按下=GND，内部上拉)
 *   GPIO8  — LED_RUN   (绿，运行状态)
 *   GPIO9  — LED_BLE   (蓝，BLE 状态)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>

#include "config_manager.h"
#include "ble_hid.h"
#include "click_engine.h"
#include "status_led.h"

static const char *TAG = "MAIN";

/* ─── 引脚定义 ──────────────────────────────────────── */
#define PIN_BTN_START   GPIO_NUM_3
#define PIN_BTN_MODE    GPIO_NUM_4
#define PIN_LED_RUN     GPIO_NUM_8
#define PIN_LED_BLE     GPIO_NUM_9

/* ─── 按键事件 ──────────────────────────────────────── */
typedef enum {
    BTN_EVT_START = 0,
    BTN_EVT_MODE  = 1,
} btn_event_t;

static QueueHandle_t s_btn_queue;

/* 按键防抖：记录上次触发时间 */
static volatile uint32_t s_last_isr_ms[2] = {0, 0};

/* ─── 全局设备配置 ───────────────────────────────────── */
static device_config_t s_cfg;

/* ═══════════════════════════════════════════════════════
 *  调试：检查按键状态
 * ══════════════════════════════════════════════════════ */
static void debug_button_status(void)
{
    int start_level = gpio_get_level(PIN_BTN_START);
    int mode_level = gpio_get_level(PIN_BTN_MODE);

    ESP_LOGI(TAG, "Button status: GPIO%d(START)=%d, GPIO%d(MODE)=%d",
             PIN_BTN_START, start_level, PIN_BTN_MODE, mode_level);
}

/* ═══════════════════════════════════════════════════════
 *  GPIO 中断服务程序（ISR）
 * ══════════════════════════════════════════════════════ */
static void IRAM_ATTR btn_isr_handler(void *arg)
{
    btn_event_t evt = (btn_event_t)(uintptr_t)arg;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    /* 硬件防抖：200ms */
    if (now_ms - s_last_isr_ms[evt] < 200) {
        return;
    }
    s_last_isr_ms[evt] = now_ms;

    BaseType_t higher_woken = pdFALSE;
    xQueueSendFromISR(s_btn_queue, &evt, &higher_woken);
    portYIELD_FROM_ISR(higher_woken);
}

/* ═══════════════════════════════════════════════════════
 *  按键初始化
 * ══════════════════════════════════════════════════════ */
static void btn_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_BTN_START) | (1ULL << PIN_BTN_MODE),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,  /* 下降沿触发（按下） */
    };
    gpio_config(&io);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(PIN_BTN_START, btn_isr_handler, (void *)BTN_EVT_START);
    gpio_isr_handler_add(PIN_BTN_MODE,  btn_isr_handler, (void *)BTN_EVT_MODE);
    ESP_LOGI(TAG, "Buttons initialized (GPIO%d=START, GPIO%d=MODE)",
             PIN_BTN_START, PIN_BTN_MODE);
}

/* ═══════════════════════════════════════════════════════
 *  BLE 连接/断开回调
 * ══════════════════════════════════════════════════════ */
static void on_ble_connect(void)
{
    ESP_LOGI(TAG, "BLE Connected");
    status_led_set_mode(LED_MODE_BLE_CONNECTED);
}

static void on_ble_disconnect(void)
{
    ESP_LOGI(TAG, "BLE Disconnected");
    /* 断开时停止点击 */
    if (click_engine_is_running()) {
        click_engine_stop();
    }
    status_led_set_mode(LED_MODE_BLE_SEARCHING);
}

/* ═══════════════════════════════════════════════════════
 *  主任务：处理按键事件
 * ══════════════════════════════════════════════════════ */
static void main_task(void *arg)
{
    (void)arg;
    btn_event_t evt;

    ESP_LOGI(TAG, "Main task started, waiting for events...");

    /* 初始调试：检查按键状态 */
    debug_button_status();

    while (1) {
        if (xQueueReceive(s_btn_queue, &evt, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "Button event received from ISR: %d (GPIO%d)",
                     evt, (evt == BTN_EVT_START) ? PIN_BTN_START : PIN_BTN_MODE);

            switch (evt) {

            /* ── START/STOP 按键 ── */
            case BTN_EVT_START:
                if (!ble_hid_is_connected()) {
                    ESP_LOGW(TAG, "BLE not connected, ignoring START");
                    status_led_blink_notify(1); /* 快速单闪提示未连接 */
                    break;
                }
                if (click_engine_is_running()) {
                    click_engine_stop();
                    status_led_set_mode(LED_MODE_BLE_CONNECTED);
                    ESP_LOGI(TAG, "Stopped. Total clicks: %lu",
                             (unsigned long)click_engine_get_count());
                } else {
                    click_engine_start();
                    status_led_set_mode(LED_MODE_CLICKING);
                    ESP_LOGI(TAG, "Started clicking.");
                }
                break;

            /* ── MODE 切换按键 ── */
            case BTN_EVT_MODE: {
                click_mode_t new_mode = click_engine_cycle_mode();
                /* 同步到配置并保存 */
                s_cfg.click.mode = new_mode;
                config_manager_save(&s_cfg);
                /* LED 闪烁次数 = 模式编号 + 1，直观提示 */
                status_led_blink_notify((uint8_t)(new_mode + 1));
                ESP_LOGI(TAG, "Mode changed to: %s",
                         click_engine_mode_str(new_mode));
                break;
            }

            default:
                break;
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════
 *  app_main
 * ══════════════════════════════════════════════════════ */
void app_main(void)
{
    ESP_LOGI(TAG, "╔══════════════════════════════════╗");
    ESP_LOGI(TAG, "║  ESP32-C3 Auto Clicker  v1.0     ║");
    ESP_LOGI(TAG, "╚══════════════════════════════════╝");

    /* 1. 加载配置（NVS） */
    ESP_ERROR_CHECK(config_manager_init(&s_cfg));
    config_manager_print(&s_cfg);

    /* 2. 初始化 LED */
    status_led_config_t led_cfg = {
        .pin_ble     = PIN_LED_BLE,
        .pin_run     = PIN_LED_RUN,
        .active_high = true,
    };
    ESP_ERROR_CHECK(status_led_init(&led_cfg));
    status_led_set_mode(LED_MODE_BLE_SEARCHING);

    /* 3. 初始化 BLE HID */
    ble_hid_config_t ble_cfg = {
        .device_name   = s_cfg.device_name,
        .on_connect    = on_ble_connect,
        .on_disconnect = on_ble_disconnect,
    };
    ESP_ERROR_CHECK(ble_hid_init(&ble_cfg));

    /* 4. 初始化点击引擎 */
    ESP_ERROR_CHECK(click_engine_init(&s_cfg.click));

    /* 5. 初始化按键 */
    s_btn_queue = xQueueCreate(8, sizeof(btn_event_t));
    configASSERT(s_btn_queue);
    btn_init();

    /* 6. 启动主事件任务 */
    xTaskCreate(main_task, "main_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Init complete. Advertising as \"%s\"", s_cfg.device_name);
    ESP_LOGI(TAG, "Press START button to begin clicking after BLE connect.");
}
