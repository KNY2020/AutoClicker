/**
 * @file  config_manager.c
 * @brief NVS 配置管理实现
 */

#include "config_manager.h"

#include <string.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>

static const char *TAG       = "CFG";
static const char *NVS_NS    = "autoclicker";   /* NVS namespace */

/* NVS key 定义（最长 15 字节） */
#define KEY_DEV_NAME    "dev_name"
#define KEY_INTERVAL    "interval_ms"
#define KEY_RAND_MIN    "rand_min"
#define KEY_RAND_MAX    "rand_max"
#define KEY_MODE        "click_mode"
#define KEY_CLICK_X     "click_x"
#define KEY_CLICK_Y     "click_y"
#define KEY_HOLD_MS     "hold_ms"

/* ─── 填充默认值 ─────────────────────────────────────── */
static void set_defaults(device_config_t *cfg)
{
    strncpy(cfg->device_name, CFG_DEFAULT_DEVICE_NAME, sizeof(cfg->device_name) - 1);
    cfg->click.mode         = CFG_DEFAULT_MODE;
    cfg->click.interval_ms  = CFG_DEFAULT_INTERVAL_MS;
    cfg->click.rand_min_ms  = CFG_DEFAULT_RAND_MIN_MS;
    cfg->click.rand_max_ms  = CFG_DEFAULT_RAND_MAX_MS;
    cfg->click.click_x      = CFG_DEFAULT_CLICK_X;
    cfg->click.click_y      = CFG_DEFAULT_CLICK_Y;
    cfg->click.hold_ms      = CFG_DEFAULT_HOLD_MS;
}

/* ─── 初始化 ─────────────────────────────────────────── */
esp_err_t config_manager_init(device_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    /* 初始化 NVS flash（已初始化则跳过） */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        nvs_flash_erase();
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %d", ret);
        return ret;
    }

    nvs_handle_t h;
    ret = nvs_open(NVS_NS, NVS_READONLY, &h);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        /* 首次启动，使用默认值并写入 */
        ESP_LOGI(TAG, "No config found, using defaults");
        set_defaults(cfg);
        return config_manager_save(cfg);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %d", ret);
        set_defaults(cfg);
        return ESP_OK; /* 降级：使用默认值，不报错 */
    }

    /* 读取各字段 */
    size_t name_len = sizeof(cfg->device_name);
    if (nvs_get_str(h, KEY_DEV_NAME, cfg->device_name, &name_len) != ESP_OK) {
        strncpy(cfg->device_name, CFG_DEFAULT_DEVICE_NAME, sizeof(cfg->device_name) - 1);
    }

    uint32_t u32;
    uint16_t u16;
    uint8_t  u8;

#define NVS_GET_U32(key, dst, def) \
    if (nvs_get_u32(h, key, &u32) == ESP_OK) dst = u32; else dst = def

#define NVS_GET_U16(key, dst, def) \
    if (nvs_get_u16(h, key, &u16) == ESP_OK) dst = u16; else dst = def

#define NVS_GET_U8(key, dst, def) \
    if (nvs_get_u8(h, key, &u8) == ESP_OK) dst = (typeof(dst))u8; else dst = def

    NVS_GET_U32(KEY_INTERVAL, cfg->click.interval_ms,  CFG_DEFAULT_INTERVAL_MS);
    NVS_GET_U32(KEY_RAND_MIN, cfg->click.rand_min_ms,  CFG_DEFAULT_RAND_MIN_MS);
    NVS_GET_U32(KEY_RAND_MAX, cfg->click.rand_max_ms,  CFG_DEFAULT_RAND_MAX_MS);
    NVS_GET_U16(KEY_CLICK_X,  cfg->click.click_x,      CFG_DEFAULT_CLICK_X);
    NVS_GET_U16(KEY_CLICK_Y,  cfg->click.click_y,      CFG_DEFAULT_CLICK_Y);
    NVS_GET_U16(KEY_HOLD_MS,  cfg->click.hold_ms,      CFG_DEFAULT_HOLD_MS);
    NVS_GET_U8 (KEY_MODE,     cfg->click.mode,         CFG_DEFAULT_MODE);

    nvs_close(h);
    ESP_LOGI(TAG, "Config loaded from NVS");
    return ESP_OK;
}

/* ─── 保存 ───────────────────────────────────────────── */
esp_err_t config_manager_save(const device_config_t *cfg)
{
    if (!cfg) return ESP_ERR_INVALID_ARG;

    nvs_handle_t h;
    esp_err_t ret = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open(RW) failed: %d", ret);
        return ret;
    }

    nvs_set_str(h, KEY_DEV_NAME, cfg->device_name);
    nvs_set_u32(h, KEY_INTERVAL, cfg->click.interval_ms);
    nvs_set_u32(h, KEY_RAND_MIN, cfg->click.rand_min_ms);
    nvs_set_u32(h, KEY_RAND_MAX, cfg->click.rand_max_ms);
    nvs_set_u16(h, KEY_CLICK_X,  cfg->click.click_x);
    nvs_set_u16(h, KEY_CLICK_Y,  cfg->click.click_y);
    nvs_set_u16(h, KEY_HOLD_MS,  cfg->click.hold_ms);
    nvs_set_u8 (h, KEY_MODE,     (uint8_t)cfg->click.mode);

    ret = nvs_commit(h);
    nvs_close(h);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Config saved to NVS");
    } else {
        ESP_LOGE(TAG, "nvs_commit failed: %d", ret);
    }
    return ret;
}

/* ─── 恢复默认 ───────────────────────────────────────── */
esp_err_t config_manager_reset(device_config_t *cfg)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    set_defaults(cfg);
    ESP_LOGW(TAG, "Config reset to defaults");
    return config_manager_save(cfg);
}

/* ─── 打印 ───────────────────────────────────────────── */
void config_manager_print(const device_config_t *cfg)
{
    ESP_LOGI(TAG, "┌─── Device Config ─────────────────");
    ESP_LOGI(TAG, "│ Device Name : %s",  cfg->device_name);
    ESP_LOGI(TAG, "│ Click Mode  : %s",  click_engine_mode_str(cfg->click.mode));
    ESP_LOGI(TAG, "│ Interval    : %lu ms", (unsigned long)cfg->click.interval_ms);
    ESP_LOGI(TAG, "│ Random Range: %lu ~ %lu ms",
             (unsigned long)cfg->click.rand_min_ms,
             (unsigned long)cfg->click.rand_max_ms);
    ESP_LOGI(TAG, "│ Position    : (%u, %u)", cfg->click.click_x, cfg->click.click_y);
    ESP_LOGI(TAG, "│ Hold Time   : %u ms", cfg->click.hold_ms);
    ESP_LOGI(TAG, "└───────────────────────────────────");
}
