/**
 * @file  status_led.h
 * @brief 状态 LED 管理（非阻塞，基于 esp_timer）
 *        支持单色 GPIO LED 和 WS2812 RGB LED（可选编译）
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── LED 模式 ──────────────────────────────────────── */
typedef enum {
    LED_MODE_OFF           = 0,
    LED_MODE_BLE_SEARCHING = 1,  /**< 蓝色慢闪 1Hz：BLE 广播中     */
    LED_MODE_BLE_CONNECTED = 2,  /**< 蓝色常亮：已连接              */
    LED_MODE_CLICKING      = 3,  /**< 绿色快闪 5Hz：点击运行中      */
    LED_MODE_ERROR         = 4,  /**< 红色快闪：错误                */
} led_mode_t;

/* ─── 配置 ──────────────────────────────────────────── */
typedef struct {
    uint8_t pin_ble;     /**< BLE 状态 LED GPIO                */
    uint8_t pin_run;     /**< 运行状态 LED GPIO                */
    bool    active_high; /**< true=高电平点亮, false=低电平点亮 */
} status_led_config_t;

/* ─── API ────────────────────────────────────────────── */
esp_err_t status_led_init(const status_led_config_t *cfg);
void      status_led_set_mode(led_mode_t mode);

/** 临时闪烁 n 次后恢复当前模式（用于模式切换提示） */
void      status_led_blink_notify(uint8_t n);

#ifdef __cplusplus
}
#endif
