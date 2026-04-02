/**
 * @file  ble_hid.h
 * @brief BLE HID 触摸屏服务
 *        使用 NimBLE 协议栈（ESP-IDF 内置）
 *        实现 HID Digitizer / Touch Screen，模拟单点触摸
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 回调函数类型 ────────────────────────────────────────── */
typedef void (*ble_hid_connect_cb_t)(void);
typedef void (*ble_hid_disconnect_cb_t)(void);

/* ─── 配置结构体 ──────────────────────────────────────────── */
typedef struct {
    const char             *device_name;   /**< BLE 广播名称           */
    ble_hid_connect_cb_t    on_connect;    /**< 连接成功回调           */
    ble_hid_disconnect_cb_t on_disconnect; /**< 断开连接回调           */
} ble_hid_config_t;

/* ─── API ─────────────────────────────────────────────────── */

/**
 * @brief 初始化并启动 BLE HID 服务
 * @param cfg  配置参数（设备名、回调）
 * @return ESP_OK / 错误码
 */
esp_err_t ble_hid_init(const ble_hid_config_t *cfg);

/**
 * @brief 反初始化，释放 BLE 资源
 */
esp_err_t ble_hid_deinit(void);

/**
 * @brief 发送一次触摸点击（按下 + hold_ms 后抬起）
 * @param x        HID 逻辑坐标 X，范围 0~4095
 * @param y        HID 逻辑坐标 Y，范围 0~4095
 * @param hold_ms  按下保持时长（毫秒），建议 20~50
 * @return true = 发送成功，false = 未连接或发送失败
 */
bool ble_hid_send_click(uint16_t x, uint16_t y, uint16_t hold_ms);

/**
 * @brief 查询 BLE 当前是否已连接
 */
bool ble_hid_is_connected(void);

/**
 * @brief 开始 BLE 广播（断开后重新广播时调用）
 */
esp_err_t ble_hid_start_advertising(void);

#ifdef __cplusplus
}
#endif
