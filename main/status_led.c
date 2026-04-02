/**
 * @file  status_led.c
 * @brief 状态 LED 非阻塞实现（esp_timer + GPIO）
 */

#include "status_led.h"

#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <string.h>

static const char *TAG = "LED";

/* ─── 内部状态 ──────────────────────────────────────── */
static struct {
    status_led_config_t cfg;
    led_mode_t          mode;
    led_mode_t          restore_mode;  /* notify 结束后恢复的模式 */
    bool                ble_state;
    bool                run_state;
    int8_t              notify_count;  /* 剩余闪烁次数，-1=无效 */
    esp_timer_handle_t  timer;
} s_led;

/* ─── GPIO 写封装 ───────────────────────────────────── */
static inline void led_set(uint8_t pin, bool on)
{
    gpio_set_level(pin, s_led.cfg.active_high ? on : !on);
}

/* ─── 定时器回调（10ms tick） ─────────────────────── */
static void led_timer_cb(void *arg)
{
    static uint32_t tick = 0;
    tick++;

    /* 优先处理临时闪烁通知 */
    if (s_led.notify_count > 0) {
        bool on = (s_led.notify_count % 2 == 0); /* 偶数次=亮 */
        led_set(s_led.cfg.pin_run, on);
        led_set(s_led.cfg.pin_ble, on);
        if (tick % 8 == 0) { /* 每 80ms 切换 */
            s_led.notify_count--;
            if (s_led.notify_count == 0) {
                /* 恢复正常模式 */
                s_led.mode = s_led.restore_mode;
                s_led.notify_count = -1;
            }
        }
        return;
    }

    switch (s_led.mode) {
    case LED_MODE_OFF:
        led_set(s_led.cfg.pin_ble, false);
        led_set(s_led.cfg.pin_run, false);
        break;

    case LED_MODE_BLE_SEARCHING:
        /* 蓝 LED 500ms 周期慢闪 */
        if (tick % 50 == 0) {
            s_led.ble_state = !s_led.ble_state;
            led_set(s_led.cfg.pin_ble, s_led.ble_state);
        }
        led_set(s_led.cfg.pin_run, false);
        break;

    case LED_MODE_BLE_CONNECTED:
        led_set(s_led.cfg.pin_ble, true);
        led_set(s_led.cfg.pin_run, false);
        break;

    case LED_MODE_CLICKING:
        led_set(s_led.cfg.pin_ble, true);
        /* 绿 LED 200ms 周期快闪 */
        if (tick % 10 == 0) {
            s_led.run_state = !s_led.run_state;
            led_set(s_led.cfg.pin_run, s_led.run_state);
        }
        break;

    case LED_MODE_ERROR:
        /* 双 LED 100ms 快闪 */
        if (tick % 10 == 0) {
            s_led.run_state = !s_led.run_state;
            led_set(s_led.cfg.pin_ble, s_led.run_state);
            led_set(s_led.cfg.pin_run, s_led.run_state);
        }
        break;
    }
}

/* ─── Public API ─────────────────────────────────── */
esp_err_t status_led_init(const status_led_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    memset(&s_led, 0, sizeof(s_led));
    s_led.cfg           = *cfg;
    s_led.notify_count  = -1;

    /* 配置 GPIO */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << cfg->pin_ble) | (1ULL << cfg->pin_run),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    led_set(cfg->pin_ble, false);
    led_set(cfg->pin_run, false);

    /* 启动 10ms 定时器 */
    const esp_timer_create_args_t ta = {
        .callback        = led_timer_cb,
        .name            = "led_tmr",
        .dispatch_method = ESP_TIMER_TASK,
    };
    esp_err_t ret = esp_timer_create(&ta, &s_led.timer);
    if (ret != ESP_OK) return ret;

    ret = esp_timer_start_periodic(s_led.timer, 10 * 1000); /* 10ms */
    if (ret != ESP_OK) return ret;

    ESP_LOGI(TAG, "LED init: pin_ble=%d pin_run=%d", cfg->pin_ble, cfg->pin_run);
    return ESP_OK;
}

void status_led_set_mode(led_mode_t mode)
{
    s_led.mode = mode;
    s_led.ble_state = false;
    s_led.run_state = false;
}

void status_led_blink_notify(uint8_t n)
{
    s_led.restore_mode = s_led.mode;
    s_led.notify_count = (int8_t)(n * 2); /* 亮+灭各计一次 */
}
