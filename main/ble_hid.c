/**
 * @file  ble_hid.c
 * @brief BLE HID Digitizer (Touch Screen) 实现
 *
 * 协议栈: NimBLE (ESP-IDF 内置, idf >= 4.4)
 * HID Usage Page: Digitizer (0x0D)
 * HID Usage: Touch Screen (0x04)
 * 坐标系: 逻辑分辨率 4096×4096，Android 自动映射到物理分辨率
 *
 * 报告格式 (Report ID = 1, 共 5 字节):
 *   Byte 0   : Contact bits  [bit0 = Tip Switch (1=按下/0=抬起), bit7:1 = padding]
 *   Byte 1~2 : X 坐标 Little-Endian  (0~4095)
 *   Byte 3~4 : Y 坐标 Little-Endian  (0~4095)
 */

#include "ble_hid.h"

#include <string.h>
#include <esp_log.h>
#include <esp_err.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* NimBLE headers */
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

/* ─── 日志 Tag ──────────────────────────────────────────── */
static const char *TAG = "BLE_HID";

/* ─── HID 报告描述符（Digitizer / Touch Screen，单点） ───── */
static const uint8_t hid_report_desc[] = {
    0x05, 0x0D,        /* Usage Page (Digitizer)           */
    0x09, 0x04,        /* Usage (Touch Screen)             */
    0xA1, 0x01,        /* Collection (Application)         */
    0x85, 0x01,        /*   Report ID (1)                  */

    /* ── 触摸手指集合 ── */
    0x09, 0x22,        /*   Usage (Finger)                 */
    0xA1, 0x02,        /*   Collection (Logical)           */

    /* Tip Switch (接触位) */
    0x09, 0x42,        /*     Usage (Tip Switch)           */
    0x15, 0x00,        /*     Logical Min (0)              */
    0x25, 0x01,        /*     Logical Max (1)              */
    0x75, 0x01,        /*     Report Size (1 bit)          */
    0x95, 0x01,        /*     Report Count (1)             */
    0x81, 0x02,        /*     Input (Data, Var, Abs)       */

    /* In Range (接近触摸屏) */
    0x09, 0x32,        /*     Usage (In Range)             */
    0x15, 0x00,        /*     Logical Min (0)              */
    0x25, 0x01,        /*     Logical Max (1)              */
    0x75, 0x01,        /*     Report Size (1 bit)          */
    0x95, 0x01,        /*     Report Count (1)             */
    0x81, 0x02,        /*     Input (Data, Var, Abs)       */

    /* Padding 6 bits */
    0x75, 0x06,        /*     Report Size (6)              */
    0x95, 0x01,        /*     Report Count (1)             */
    0x81, 0x03,        /*     Input (Const)                */

    /* X 坐标 */
    0x05, 0x01,        /*     Usage Page (Generic Desktop) */
    0x09, 0x30,        /*     Usage (X)                    */
    0x15, 0x00,        /*     Logical Min (0)              */
    0x26, 0xFF, 0x0F,  /*     Logical Max (4095)           */
    0x35, 0x00,        /*     Physical Min (0)             */
    0x46, 0xFF, 0x0F,  /*     Physical Max (4095)          */
    0x75, 0x10,        /*     Report Size (16 bits)        */
    0x95, 0x01,        /*     Report Count (1)             */
    0x81, 0x02,        /*     Input (Data, Var, Abs)       */

    /* Y 坐标 */
    0x09, 0x31,        /*     Usage (Y)                    */
    0x15, 0x00,        /*     Logical Min (0)              */
    0x26, 0xFF, 0x0F,  /*     Logical Max (4095)           */
    0x35, 0x00,        /*     Physical Min (0)             */
    0x46, 0xFF, 0x0F,  /*     Physical Max (4095)          */
    0x75, 0x10,        /*     Report Size (16 bits)        */
    0x95, 0x01,        /*     Report Count (1)             */
    0x81, 0x02,        /*     Input (Data, Var, Abs)       */

    0xC0,              /*   End Collection (Finger)        */
    0xC0               /* End Collection (Application)     */
};

/* ─── HID 触摸报告结构 ─────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint8_t  report_id;
    uint8_t  contact;   /* bit0: Tip Switch, bit1: In Range */
    uint16_t x;
    uint16_t y;
} touch_report_t;

/* ─── UUIDs ─────────────────────────────────────────────── */
/* HID Service: 0x1812 */
static const ble_uuid16_t svc_hid_uuid      = BLE_UUID16_INIT(0x1812);
/* Battery Service: 0x180F */
static const ble_uuid16_t svc_bat_uuid      = BLE_UUID16_INIT(0x180F);
/* Device Info Service: 0x180A */
static const ble_uuid16_t svc_dis_uuid      = BLE_UUID16_INIT(0x180A);

/* HID Report (Input): 0x2A4D */
static const ble_uuid16_t chr_hid_report_uuid = BLE_UUID16_INIT(0x2A4D);
/* HID Report Map: 0x2A4B */
static const ble_uuid16_t chr_report_map_uuid = BLE_UUID16_INIT(0x2A4B);
/* HID Information: 0x2A4A */
static const ble_uuid16_t chr_hid_info_uuid   = BLE_UUID16_INIT(0x2A4A);
/* HID Control Point: 0x2A4C */
static const ble_uuid16_t chr_hid_ctrl_uuid   = BLE_UUID16_INIT(0x2A4C);
/* Battery Level: 0x2A19 */
static const ble_uuid16_t chr_bat_level_uuid  = BLE_UUID16_INIT(0x2A19);
/* PnP ID: 0x2A50 */
static const ble_uuid16_t chr_pnp_id_uuid     = BLE_UUID16_INIT(0x2A50);

/* ─── 模块内部状态 ──────────────────────────────────────── */
static struct {
    bool                     initialized;
    bool                     connected;
    uint16_t                 conn_handle;
    uint16_t                 input_report_handle; /* Input Report 特征 handle */
    ble_hid_config_t         cfg;
    uint8_t                  own_addr[6];
} s_hid;

/* ─── 前置声明 ───────────────────────────────────────────── */
static int  hid_gap_event(struct ble_gap_event *event, void *arg);
static void hid_on_sync(void);
static void hid_on_reset(int reason);
static void nimble_host_task(void *param);

/* ═══════════════════════════════════════════════════════════
 *  GATT 特征读取回调
 * ══════════════════════════════════════════════════════════ */

/* Report Map 读取 */
static int chr_report_map_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    return os_mbuf_append(ctxt->om, hid_report_desc, sizeof(hid_report_desc));
}

/* HID Information 读取: bcdHID=0x0111, bCountryCode=0x00, Flags=0x02(normally connectable) */
static int chr_hid_info_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    static const uint8_t hid_info[] = { 0x11, 0x01, 0x00, 0x02 };
    return os_mbuf_append(ctxt->om, hid_info, sizeof(hid_info));
}

/* HID Control Point 写入（Suspend/Exit Suspend，忽略即可） */
static int chr_hid_ctrl_cb(uint16_t conn_handle, uint16_t attr_handle,
                            struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg; (void)ctxt;
    return 0;
}

/* Input Report 读取（返回全零空报告） */
static int chr_input_report_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    static const touch_report_t empty = {
        .report_id = 1,
        .contact   = 0x00,
        .x         = 0,
        .y         = 0,
    };
    return os_mbuf_append(ctxt->om, &empty, sizeof(empty));
}

/* Battery Level 读取：返回 100% */
static int chr_bat_level_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    static const uint8_t bat = 100;
    return os_mbuf_append(ctxt->om, &bat, sizeof(bat));
}

/* PnP ID 读取 */
static int chr_pnp_id_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle; (void)attr_handle; (void)arg;
    /* VendorIDSource=USB(0x02), VID=0x045E(MS), PID=0x07A5, Version=0x0111 */
    static const uint8_t pnp[] = { 0x02, 0x5E, 0x04, 0xA5, 0x07, 0x11, 0x01 };
    return os_mbuf_append(ctxt->om, pnp, sizeof(pnp));
}

/* ═══════════════════════════════════════════════════════════
 *  GATT 服务表
 * ══════════════════════════════════════════════════════════ */
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {

    /* ── HID Service (0x1812) ── */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_hid_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {

            /* Report Map (0x2A4B) */
            {
                .uuid       = &chr_report_map_uuid.u,
                .access_cb  = chr_report_map_cb,
                .flags      = BLE_GATT_CHR_F_READ,
            },

            /* HID Information (0x2A4A) */
            {
                .uuid       = &chr_hid_info_uuid.u,
                .access_cb  = chr_hid_info_cb,
                .flags      = BLE_GATT_CHR_F_READ,
            },

            /* HID Control Point (0x2A4C) */
            {
                .uuid       = &chr_hid_ctrl_uuid.u,
                .access_cb  = chr_hid_ctrl_cb,
                .flags      = BLE_GATT_CHR_F_WRITE_NO_RSP,
            },

            /* Input Report (0x2A4D) — 带 Notify，触摸数据从这里发 */
            {
                .uuid       = &chr_hid_report_uuid.u,
                .access_cb  = chr_input_report_cb,
                .val_handle = &s_hid.input_report_handle,
                .flags      = BLE_GATT_CHR_F_READ |
                              BLE_GATT_CHR_F_NOTIFY,
                /* Report Reference 描述符：Report ID=1, Type=Input(1) */
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid      = BLE_UUID16_DECLARE(0x2908), /* Report Reference */
                        .att_flags = BLE_ATT_F_READ,
                        .access_cb = (ble_gatt_access_fn *)chr_input_report_cb,
                    },
                    { 0 } /* 终止符 */
                },
            },

            { 0 } /* 终止符 */
        },
    },

    /* ── Battery Service (0x180F) ── */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_bat_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid      = &chr_bat_level_uuid.u,
                .access_cb = chr_bat_level_cb,
                .flags     = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        },
    },

    /* ── Device Information Service (0x180A) ── */
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &svc_dis_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid      = &chr_pnp_id_uuid.u,
                .access_cb = chr_pnp_id_cb,
                .flags     = BLE_GATT_CHR_F_READ,
            },
            { 0 }
        },
    },

    { 0 } /* GATT 服务表终止符 */
};

/* ═══════════════════════════════════════════════════════════
 *  GAP 广播
 * ══════════════════════════════════════════════════════════ */
esp_err_t ble_hid_start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {0};
    struct ble_hs_adv_fields  fields     = {0};
    struct ble_hs_adv_fields  rsp_fields = {0};
    int rc;

    /* ── 广播数据 ── */
    fields.flags                 = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.appearance            = 0x03C4;  /* HID Digitizer (Touchscreen) */
    fields.appearance_is_present = 1;

    /* 包含 HID 服务 UUID */
    static const ble_uuid16_t adv_uuid = BLE_UUID16_INIT(0x1812);
    fields.uuids16               = &adv_uuid;
    fields.num_uuids16           = 1;
    fields.uuids16_is_complete   = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed: %d", rc);
        return ESP_FAIL;
    }

    /* ── Scan Response（放设备名，节省广播包空间）── */
    rsp_fields.name             = (uint8_t *)s_hid.cfg.device_name;
    rsp_fields.name_len         = strlen(s_hid.cfg.device_name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv_rsp_set_fields failed: %d", rc);
        return ESP_FAIL;
    }

    /* ── 广播参数：通用可发现，无定向连接 ── */
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, hid_gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv_start failed: %d", rc);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Advertising started as \"%s\"", s_hid.cfg.device_name);
    return ESP_OK;
}

/* ═══════════════════════════════════════════════════════════
 *  GAP 事件回调
 * ══════════════════════════════════════════════════════════ */
static int hid_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {

    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_hid.connected   = true;
            s_hid.conn_handle = event->connect.conn_handle;
            ESP_LOGI(TAG, "Connected, handle=%d", s_hid.conn_handle);

            /* 请求更新连接参数（降低延迟） */
            struct ble_gap_upd_params params = {
                .itvl_min            = 6,   /* 7.5ms */
                .itvl_max            = 12,  /* 15ms  */
                .latency             = 0,
                .supervision_timeout = 200, /* 2s    */
                .min_ce_len          = 0,
                .max_ce_len          = 0,
            };
            ble_gap_update_params(s_hid.conn_handle, &params);

            if (s_hid.cfg.on_connect) {
                s_hid.cfg.on_connect();
            }
        } else {
            ESP_LOGW(TAG, "Connect failed, status=%d", event->connect.status);
            ble_hid_start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        s_hid.connected   = false;
        s_hid.conn_handle = BLE_HS_CONN_HANDLE_NONE;
        ESP_LOGI(TAG, "Disconnected, reason=0x%02x", event->disconnect.reason);

        if (s_hid.cfg.on_disconnect) {
            s_hid.cfg.on_disconnect();
        }
        /* 自动重新广播 */
        ble_hid_start_advertising();
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGD(TAG, "MTU updated: conn=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        /* 已有 bond，允许重新配对 */
        return BLE_GAP_REPEAT_PAIRING_RETRY;

    case BLE_GAP_EVENT_ENC_CHANGE:
        ESP_LOGD(TAG, "Encryption changed: conn=%d status=%d",
                 event->enc_change.conn_handle, event->enc_change.status);
        break;

    default:
        break;
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 *  NimBLE 主机回调
 * ══════════════════════════════════════════════════════════ */
static void hid_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    rc = ble_hs_id_infer_auto(0, &s_hid.own_addr[0]);
    if (rc != 0) {
        ESP_LOGE(TAG, "hs_id_infer_auto failed: %d", rc);
        return;
    }

    uint8_t addr[6] = {0};
    ble_hs_id_copy_addr(BLE_ADDR_PUBLIC, addr, NULL);
    ESP_LOGI(TAG, "BLE addr: %02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);

    ble_hid_start_advertising();
}

static void hid_on_reset(int reason)
{
    ESP_LOGE(TAG, "BLE host reset, reason=%d", reason);
}

/* NimBLE 主机任务（必须独立任务运行） */
static void nimble_host_task(void *param)
{
    (void)param;
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();  /* 阻塞直到 nimble_port_stop() */
    nimble_port_freertos_deinit();
}

/* ═══════════════════════════════════════════════════════════
 *  公共 API 实现
 * ══════════════════════════════════════════════════════════ */
esp_err_t ble_hid_init(const ble_hid_config_t *cfg)
{
    if (!cfg || !cfg->device_name) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(&s_hid, 0, sizeof(s_hid));
    s_hid.cfg         = *cfg;
    s_hid.conn_handle = BLE_HS_CONN_HANDLE_NONE;

    /* 初始化 NimBLE 协议栈 */
    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", ret);
        return ret;
    }

    /* 配置主机回调 */
    ble_hs_cfg.sync_cb  = hid_on_sync;
    ble_hs_cfg.reset_cb = hid_on_reset;

    /* 配置安全参数：Just Works Bonding */
    ble_hs_cfg.sm_io_cap    = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding   = 1;
    ble_hs_cfg.sm_our_key_dist  = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    /* 设置设备名 */
    int rc = ble_svc_gap_device_name_set(cfg->device_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "device_name_set failed: %d", rc);
        return ESP_FAIL;
    }

    /* 初始化 GAP / GATT 基础服务 */
    ble_svc_gap_init();
    ble_svc_gatt_init();

    /* 注册 GATT 服务表 */
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    /* 启动 NimBLE 主机任务 */
    nimble_port_freertos_init(nimble_host_task);

    s_hid.initialized = true;
    ESP_LOGI(TAG, "BLE HID initialized, device=\"%s\"", cfg->device_name);
    return ESP_OK;
}

esp_err_t ble_hid_deinit(void)
{
    if (!s_hid.initialized) return ESP_OK;
    nimble_port_stop();
    nimble_port_deinit();
    s_hid.initialized = false;
    return ESP_OK;
}

bool ble_hid_is_connected(void)
{
    return s_hid.connected;
}

bool ble_hid_send_click(uint16_t x, uint16_t y, uint16_t hold_ms)
{
    if (!s_hid.connected || s_hid.input_report_handle == 0) {
        ESP_LOGW(TAG, "BLE not connected or no input handle, skipping click");
        return false;
    }

    ESP_LOGD(TAG, "Sending click at (%d,%d) for %dms", x, y, hold_ms);

    struct os_mbuf *om;
    touch_report_t report;

    /* ── 按下 ── */
    report.report_id = 1;
    report.contact   = 0x03; /* Tip + InRange */
    report.x         = x;
    report.y         = y;

    om = ble_hs_mbuf_from_flat(&report, sizeof(report));
    if (!om) {
        ESP_LOGE(TAG, "mbuf alloc failed (press)");
        return false;
    }
    int rc = ble_gatts_notify_custom(s_hid.conn_handle,
                                     s_hid.input_report_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed (press): %d", rc);
        return false;
    }

    ESP_LOGD(TAG, "Press sent successfully");

    vTaskDelay(pdMS_TO_TICKS(hold_ms));

    /* ── 抬起 ── */
    report.report_id = 1;
    report.contact   = 0x00;
    report.x         = x;
    report.y         = y;
    om = ble_hs_mbuf_from_flat(&report, sizeof(report));
    if (!om) {
        ESP_LOGE(TAG, "mbuf alloc failed (release)");
        return false;
    }
    rc = ble_gatts_notify_custom(s_hid.conn_handle,
                                 s_hid.input_report_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "notify failed (release): %d", rc);
        return false;
    }

    ESP_LOGD(TAG, "Release sent successfully");
    return true;
}
