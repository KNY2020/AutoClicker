/**
 * @file  config_manager.h
 * @brief NVS 配置持久化管理
 *        封装 ESP-IDF nvs_flash，提供类型安全的读写接口
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "click_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 设备配置结构体（完整配置） ──────────────────────── */
typedef struct {
    char         device_name[32];   /**< BLE 广播名称          */
    click_engine_config_t click;    /**< 点击引擎配置          */
} device_config_t;

/* ─── 默认值 ───────────────────────────────────────────── */
#define CFG_DEFAULT_DEVICE_NAME   "AutoClicker"
#define CFG_DEFAULT_INTERVAL_MS   50  // 从 200ms 改为 100ms，更快间隔
#define CFG_DEFAULT_RAND_MIN_MS   30   // 从 150ms 改为 80ms，更快随机最小
#define CFG_DEFAULT_RAND_MAX_MS   70  // 从 400ms 改为 200ms，更快随机最大
#define CFG_DEFAULT_CLICK_X       1000
#define CFG_DEFAULT_CLICK_Y       1500
#define CFG_DEFAULT_HOLD_MS       20   // 从 30ms 改为 20ms，更短按下时长
#define CFG_DEFAULT_MODE          CLICK_MODE_FIXED

/* ─── API ─────────────────────────────────────────────── */

/** 初始化 NVS，加载配置到 cfg（若无则写入默认值） */
esp_err_t config_manager_init(device_config_t *cfg);

/** 将 cfg 写入 NVS */
esp_err_t config_manager_save(const device_config_t *cfg);

/** 恢复出厂设置（清除 NVS namespace） */
esp_err_t config_manager_reset(device_config_t *cfg);

/** 打印当前配置到串口 */
void config_manager_print(const device_config_t *cfg);

#ifdef __cplusplus
}
#endif
