/**
 * @file  click_engine.h
 * @brief 点击引擎：管理点击模式、间隔计算与定时执行
 *        使用 ESP-IDF esp_timer 高精度定时器驱动，不阻塞主循环
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 点击模式 ──────────────────────────────────────────── */
typedef enum {
    CLICK_MODE_FIXED  = 0,  /**< 固定间隔（interval_ms 参数）      */
    CLICK_MODE_RANDOM = 1,  /**< 随机间隔（rand_min ~ rand_max）   */
    CLICK_MODE_MAX    = 2,
} click_mode_t;

/* ─── 配置结构体 ────────────────────────────────────────── */
typedef struct {
    click_mode_t mode;          /**< 点击模式                     */
    uint32_t     interval_ms;   /**< FIXED模式：点击间隔(毫秒)    */
    uint32_t     rand_min_ms;   /**< RANDOM模式：最小间隔(毫秒)   */
    uint32_t     rand_max_ms;   /**< RANDOM模式：最大间隔(毫秒)   */
    uint16_t     click_x;       /**< 点击X坐标 (0~4095)          */
    uint16_t     click_y;       /**< 点击Y坐标 (0~4095)          */
    uint16_t     hold_ms;       /**< 按下保持时长(毫秒)，建议20~50*/
} click_engine_config_t;

/* ─── API ─────────────────────────────────────────────── */

esp_err_t click_engine_init(const click_engine_config_t *cfg);
esp_err_t click_engine_deinit(void);

esp_err_t click_engine_start(void);
esp_err_t click_engine_stop(void);
bool      click_engine_is_running(void);

/** 运行时更新配置（无需 stop/start） */
esp_err_t click_engine_set_config(const click_engine_config_t *cfg);

/** 循环切换到下一个模式，返回新模式 */
click_mode_t click_engine_cycle_mode(void);

/** 获取自上次 start 以来的点击次数 */
uint32_t click_engine_get_count(void);

const char *click_engine_mode_str(click_mode_t mode);

#ifdef __cplusplus
}
#endif
