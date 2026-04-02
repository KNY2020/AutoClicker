/**
 * @file  click_engine.c
 * @brief 点击引擎实现
 *
 * 架构：
 *   - 使用独立 FreeRTOS Task 执行点击，避免阻塞主任务
 *   - esp_timer 作为唤醒信号源（单次定时器，每次点击后重新 arm）
 *   - 通过 xTaskNotify 从定时器 ISR-safe 回调唤醒 click_task
 */

#include "click_engine.h"
#include "ble_hid.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <esp_timer.h>
#include <esp_log.h>
#include <esp_random.h>

static const char *TAG = "CLICK";

/* ─── 内部状态 ─────────────────────────────────────────── */
static struct {
    click_engine_config_t cfg;
    bool                  running;
    uint32_t              count;
    esp_timer_handle_t    timer;
    TaskHandle_t          task_handle;
    SemaphoreHandle_t     cfg_mutex;
} s_eng;

/* ─── 计算下一次间隔（μs） ─────────────────────────────── */
static uint64_t next_interval_us(void)
{
    click_engine_config_t cfg;
    xSemaphoreTake(s_eng.cfg_mutex, portMAX_DELAY);
    cfg = s_eng.cfg;
    xSemaphoreGive(s_eng.cfg_mutex);

    uint32_t ms;
    if (cfg.mode == CLICK_MODE_RANDOM) {
        uint32_t range = cfg.rand_max_ms - cfg.rand_min_ms;
        ms = cfg.rand_min_ms + (esp_random() % (range + 1));
    } else {
        ms = cfg.interval_ms;
    }
    /* 防止间隔过短导致系统过载 */
    if (ms < 20)  ms = 20;
    return (uint64_t)ms * 1000ULL;
}

/* ─── 定时器回调：唤醒 click_task ──────────────────────── */
static void click_timer_cb(void *arg)
{
    /* 从 timer callback 安全地通知任务 */
    BaseType_t higher_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_eng.task_handle, &higher_woken);
    portYIELD_FROM_ISR(higher_woken);
}

/* ─── 点击执行任务 ─────────────────────────────────────── */
static void click_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "click_task started");

    while (1) {
        /* 等待定时器通知或停止信号 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (!s_eng.running) {
            /* 停止信号：清空通知，回到等待 */
            continue;
        }

        /* 读取当前配置 */
        click_engine_config_t cfg;
        xSemaphoreTake(s_eng.cfg_mutex, portMAX_DELAY);
        cfg = s_eng.cfg;
        xSemaphoreGive(s_eng.cfg_mutex);
        
        /* 执行点击 */
        if (ble_hid_is_connected()) {
            bool ok = ble_hid_send_click(cfg.click_x, cfg.click_y, cfg.hold_ms);
            if (ok) {
                s_eng.count++;
                if (s_eng.count % 50 == 0) {
                    ESP_LOGI(TAG, "count=%lu mode=%s pos=(%u,%u)",
                             (unsigned long)s_eng.count,
                             click_engine_mode_str(cfg.mode),
                             cfg.click_x, cfg.click_y);
                }
            }
        } else {
            ESP_LOGD(TAG, "BLE not connected, skip click");
        }

        /* 若仍在运行，重新 arm 单次定时器 */
        if (s_eng.running) {
            esp_timer_start_once(s_eng.timer, next_interval_us());
        }
    }
}

/* ═══════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════ */

esp_err_t click_engine_init(const click_engine_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    memset(&s_eng, 0, sizeof(s_eng));
    s_eng.cfg = *cfg;

    s_eng.cfg_mutex = xSemaphoreCreateMutex();
    if (!s_eng.cfg_mutex) return ESP_ERR_NO_MEM;

    /* 创建单次定时器 */
    const esp_timer_create_args_t timer_args = {
        .callback = click_timer_cb,
        .name     = "click_tmr",
        .dispatch_method = ESP_TIMER_TASK,
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_eng.timer);
    if (ret != ESP_OK) return ret;

    /* 创建点击任务（优先级略高于 main task） */
    BaseType_t rc = xTaskCreate(click_task, "click_task",
                                4096, NULL, 6, &s_eng.task_handle);
    if (rc != pdPASS) {
        esp_timer_delete(s_eng.timer);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Engine init: mode=%s interval=%lums pos=(%u,%u)",
             click_engine_mode_str(cfg->mode),
             (unsigned long)cfg->interval_ms,
             cfg->click_x, cfg->click_y);
    return ESP_OK;
}

esp_err_t click_engine_deinit(void)
{
    click_engine_stop();
    if (s_eng.task_handle) {
        vTaskDelete(s_eng.task_handle);
        s_eng.task_handle = NULL;
    }
    if (s_eng.timer) {
        esp_timer_delete(s_eng.timer);
        s_eng.timer = NULL;
    }
    if (s_eng.cfg_mutex) {
        vSemaphoreDelete(s_eng.cfg_mutex);
        s_eng.cfg_mutex = NULL;
    }
    return ESP_OK;
}

esp_err_t click_engine_start(void)
{
    if (s_eng.running) return ESP_OK;
    s_eng.running = true;
    s_eng.count   = 0;
    ESP_LOGI(TAG, "Start — mode=%s", click_engine_mode_str(s_eng.cfg.mode));
    /* 立即触发第一次点击 */
    esp_timer_start_once(s_eng.timer, next_interval_us());
    return ESP_OK;
}

esp_err_t click_engine_stop(void)
{
    if (!s_eng.running) return ESP_OK;
    s_eng.running = false;
    esp_timer_stop(s_eng.timer);
    ESP_LOGI(TAG, "Stop — total clicks: %lu", (unsigned long)s_eng.count);
    return ESP_OK;
}

bool click_engine_is_running(void)
{
    return s_eng.running;
}

esp_err_t click_engine_set_config(const click_engine_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_eng.cfg_mutex, portMAX_DELAY);
    s_eng.cfg = *cfg;
    xSemaphoreGive(s_eng.cfg_mutex);
    return ESP_OK;
}

click_mode_t click_engine_cycle_mode(void)
{
    xSemaphoreTake(s_eng.cfg_mutex, portMAX_DELAY);
    s_eng.cfg.mode = (click_mode_t)((s_eng.cfg.mode + 1) % CLICK_MODE_MAX);
    click_mode_t m = s_eng.cfg.mode;
    xSemaphoreGive(s_eng.cfg_mutex);
    ESP_LOGI(TAG, "Mode -> %s", click_engine_mode_str(m));
    return m;
}

uint32_t click_engine_get_count(void)
{
    return s_eng.count;
}

const char *click_engine_mode_str(click_mode_t mode)
{
    switch (mode) {
        case CLICK_MODE_FIXED:  return "FIXED";
        case CLICK_MODE_RANDOM: return "RANDOM";
        default:                return "UNKNOWN";
    }
}
