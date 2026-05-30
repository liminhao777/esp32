/*
 * ESP32 BLE Provisioning Template
 * Minimal, ready-to-adapt example showing:
 * - NVS, esp_netif, event loop init order
 * - guarded esp_netif_create_default_wifi_sta()
 * - wifi_prov_mgr with BLE scheme
 * - simple custom BLE endpoint stub
 * - DHCP watchdog and 2.4GHz BSSID-lock scan placeholder
 *
 * Adapt to your sdkconfig and partition table as needed.
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

static const char *TAG = "ble_prov_template";

/* Guarded default STA netif pointer to avoid duplicate creation */
static esp_netif_t *s_netif = NULL;

/* DHCP watchdog: if no IP after timeout, re-scan/connect logic can run */
static esp_timer_handle_t s_dhcp_watchdog = NULL;
static const int DHCP_WATCHDOG_MS = 20000; // 20s

/* forward declarations */
static void start_provisioning(void);
static void stop_provisioning(void);
static void dhcp_watchdog_cb(void *arg);

/* Simple custom BLE endpoint: receives arbitrary bytes from mobile app */
static esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf,
                                         ssize_t inlen, uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    ESP_LOGI(TAG, "custom endpoint: got %d bytes", (int)inlen);
    // Persist or parse inbuf as needed. Here we simply acknowledge.
    const char *ack = "OK";
    *outlen = strlen(ack);
    *outbuf = (uint8_t *)esp_memdup(ack, *outlen);
    return ESP_OK;
}

/* wifi_prov_mgr event handler */
static void prov_event_handler(void* handler_arg, esp_event_base_t base, int32_t id, void* event_data)
{
    if (base != WIFI_PROV_EVENT) return;

    switch (id) {
    case WIFI_PROV_START:
        ESP_LOGI(TAG, "Provisioning started");
        break;
    case WIFI_PROV_CRED_RECV: {
        wifi_prov_sta_credentials_t *cred = (wifi_prov_sta_credentials_t *)event_data;
        ESP_LOGI(TAG, "Received credentials for SSID '%s'", cred->ssid);
        break;
    }
    case WIFI_PROV_CRED_FAIL:
        ESP_LOGW(TAG, "Provisioning credential failed");
        break;
    case WIFI_PROV_CRED_SUCCESS:
        ESP_LOGI(TAG, "Provisioning credential success");
        break;
    case WIFI_PROV_END:
        ESP_LOGI(TAG, "Provisioning ended");
        break;
    default:
        break;
    }
}

/* IP event handler */
static void ip_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP address");
        // Stop DHCP watchdog
        if (s_dhcp_watchdog) {
            esp_timer_stop(s_dhcp_watchdog);
        }
        // Do NOT deinit wifi_prov_mgr immediately here. Let the mobile app receive ACK,
        // and rely on wifi_prov_mgr auto-stop timeout or stop_provisioning() after a short delay.
    }
}

/* Start wifi_prov_mgr with BLE scheme and register a simple endpoint */
static void start_provisioning(void)
{
    ESP_LOGI(TAG, "Starting BLE provisioning");

    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_CONTEXT
    };

    esp_err_t err = wifi_prov_mgr_init(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi_prov_mgr_init failed: %s", esp_err_to_name(err));
        return;
    }

    // Register custom endpoint
    wifi_prov_mgr_endpoint_t ep = {
        .name = "custom",
        .handler = custom_prov_data_handler,
        .priv_data = NULL
    };
    wifi_prov_mgr_add_endpoint(ep);

    // Start provisioning (no POP here; add when needed for security)
    wifi_prov_config_t prov_config = {
        .pop = NULL
    };

    err = wifi_prov_mgr_start_provisioning(&prov_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start_provisioning failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Provisioning started over BLE");
}

static void stop_provisioning(void)
{
    ESP_LOGI(TAG, "Stopping provisioning and cleaning up");
    wifi_prov_mgr_stop_provisioning();
    wifi_prov_mgr_deinit();
}

/* DHCP watchdog callback */
static void dhcp_watchdog_cb(void *arg)
{
    ESP_LOGW(TAG, "DHCP watchdog fired: no IP obtained");
    // Placeholder: implement re-scan and 2.4GHz BSSID-lock here
    // e.g., call scan_and_lock_and_connect(saved_ssid, saved_bssid)
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default wifi STA netif if not yet created. Guard to avoid duplicate key assert.
    if (!s_netif) {
        s_netif = esp_netif_create_default_wifi_sta();
        if (!s_netif) {
            ESP_LOGW(TAG, "esp_netif_create_default_wifi_sta returned NULL or failed");
        }
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                                        prov_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        ip_event_handler, NULL, NULL));

    // Create DHCP watchdog timer
    const esp_timer_create_args_t wd_args = {
        .callback = &dhcp_watchdog_cb,
        .name = "dhcp_wd"
    };
    ESP_ERROR_CHECK(esp_timer_create(&wd_args, &s_dhcp_watchdog));

    // Start BLE provisioning
    start_provisioning();

    // Optional: start DHCP watchdog when connecting to Wi-Fi (example usage)
    // esp_timer_start_once(s_dhcp_watchdog, DHCP_WATCHDOG_MS * 1000);

    // Main app loop; replace with your business logic
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
