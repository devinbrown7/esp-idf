/* Wi-Fi Provisioning Manager Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

#include <wifi_provisioning/manager.h>
#include <wifi_provisioning/scheme_ble.h>

#define BLINK

static const char *TAG = "app";

/* Event handler for catching system events */
static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                ESP_LOGI(TAG, "Received Wi-Fi credentials"
                         "\n\tSSID     : %s\n\tPassword : %s",
                         (const char *) wifi_sta_cfg->ssid,
                         (const char *) wifi_sta_cfg->password);
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                         "\n\tPlease reset to factory and retry provisioning",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                         "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
                ESP_LOGI(TAG, "Failed to connect with provisioned AP, reseting provisioned credentials");
                wifi_prov_mgr_reset_sm_state_on_failure();
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                wifi_prov_mgr_reset_sm_state_for_reprovision();
                break;
            case WIFI_PROV_END:
                ESP_LOGI(TAG, "Provisioning ENDED");
                /* De-initialize manager once provisioning is finished */
                wifi_prov_mgr_deinit();
                break;
            default:
                break;
        }
    } else if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi Started in STA mode");
                
                /* Check if app is provisioned  */
                bool provisioned = false;
                ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));
                if (provisioned) {
                    ESP_LOGI(TAG, "WiFi credentials are present, so attempt to connect");
                    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
                }

                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_LOGI(TAG, "Disconnected");
                break;
            case WIFI_EVENT_SCAN_DONE:
                ESP_LOGI(TAG, "WiFi Scan Complete");
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected with IP Address:" IPSTR, IP2STR(&event->ip_info.ip));
    } else if (event_base == PROTOCOMM_TRANSPORT_BLE_EVENT) {
        switch (event_id) {
            case PROTOCOMM_TRANSPORT_BLE_CONNECTED:
                ESP_LOGI(TAG, "BLE transport: Connected!");
                break;
            case PROTOCOMM_TRANSPORT_BLE_DISCONNECTED:
                ESP_LOGI(TAG, "BLE transport: Disconnected!");
                break;
            default:
                break;
        }
    } else if (event_base == PROTOCOMM_SECURITY_SESSION_EVENT) {
        switch (event_id) {
            case PROTOCOMM_SECURITY_SESSION_SETUP_OK:
                ESP_LOGI(TAG, "Secured session established!");
                break;
            case PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS:
                ESP_LOGE(TAG, "Received invalid security parameters for establishing secure session!");
                break;
            case PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH:
                ESP_LOGE(TAG, "Received incorrect username and/or PoP for establishing secure session!");
                break;
            default:
                break;
        }
    }
}

static void get_device_service_name(char *service_name, size_t max)
{
    uint8_t eth_mac[6];
    const char *ssid_prefix = "Kaivac_";
    esp_wifi_get_mac(WIFI_IF_STA, eth_mac);
    snprintf(service_name, max, "%s%02X_%02X_%02X",
             ssid_prefix, eth_mac[3], eth_mac[4], eth_mac[5]);
}

/* Handler for the optional provisioning endpoint registered by the application.
 * The data format can be chosen by applications. Here, we are using plain ascii text.
 * Applications can choose to use other formats like protobuf, JSON, XML, etc.
 */
esp_err_t custom_prov_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                          uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if (inbuf) {
        ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
    }
    char response[] = "SUCCESS";
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1; /* +1 for NULL terminating byte */

    return ESP_OK;
}

void init_esp(void) {
    /* Initialize NVS partition */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        /* NVS partition was truncated
         * and needs to be erased */
        ESP_ERROR_CHECK(nvs_flash_erase());

        /* Retry nvs_flash_init */
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    /* Initialize TCP/IP */
    ESP_ERROR_CHECK(esp_netif_init());

    /* Initialize the event loop */
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Register our event handler for Wi-Fi, IP and Provisioning related events */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(PROTOCOMM_SECURITY_SESSION_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

    /* Initialize Wi-Fi including netif with default config */
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
}

void init_prov(void)
{
    /* Configuration for the provisioning manager */
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,

        /* Any default scheme specific event handler that you would
         * like to choose. Since our example application requires
         * neither BT nor BLE, we can choose to release the associated
         * memory once provisioning is complete, or not needed
         * (in case when device is already provisioned). Choosing
         * appropriate scheme specific event handler allows the manager
         * to take care of this automatically. This can be set to
         * WIFI_PROV_EVENT_HANDLER_NONE when using wifi_prov_scheme_softap*/
        // TODO
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
    };

    /* Initialize provisioning manager with the
     * configuration parameters set above */
    ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

    /* Determine BLE device name */
    char service_name[16];
    get_device_service_name(service_name, sizeof(service_name));

    /* What is the security level that we want (0, 1, 2):
        *      - WIFI_PROV_SECURITY_0 is simply plain text communication.
        *      - WIFI_PROV_SECURITY_1 is secure communication which consists of secure handshake
        *          using X25519 key exchange and proof of possession (pop) and AES-CTR
        *          for encryption/decryption of messages.
        *      - WIFI_PROV_SECURITY_2 SRP6a based authentication and key exchange
        *        + AES-GCM encryption/decryption of messages
        */
    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;

    /* Do we want a proof-of-possession (ignored if Security 0 is selected):
        *      - this should be a string with length > 0
        *      - NULL if not used
        */
    const char *pop = "abcd1234";

    /* This is the structure for passing security parameters
        * for the protocomm security 1.
        */
    wifi_prov_security1_params_t *sec_params = pop;

    /* What is the service key (could be NULL)
        * This translates to :
        *     - Wi-Fi password when scheme is wifi_prov_scheme_softap
        *          (Minimum expected length: 8, maximum 64 for WPA2-PSK)
        *     - simply ignored when scheme is wifi_prov_scheme_ble
        */
    const char *service_key = NULL;

    /* This step is only useful when scheme is wifi_prov_scheme_ble. This will
        * set a custom 128 bit UUID which will be included in the BLE advertisement
        * and will correspond to the primary GATT service that provides provisioning
        * endpoints as GATT characteristics. Each GATT characteristic will be
        * formed using the primary service UUID as base, with different auto assigned
        * 12th and 13th bytes (assume counting starts from 0th byte). The client side
        * applications must identify the endpoints by reading the User Characteristic
        * Description descriptor (0x2901) for each characteristic, which contains the
        * endpoint name of the characteristic */
    uint8_t custom_service_uuid[] = {
        /* LSB <---------------------------------------
            * ---------------------------------------> MSB */
        0xb4, 0xdf, 0x5a, 0x1c, 0x3f, 0x6b, 0xf4, 0xbf,
        0xea, 0x4a, 0x82, 0x03, 0x04, 0x90, 0x1a, 0x02,
    };

    /* If your build fails with linker errors at this point, then you may have
        * forgotten to enable the BT stack or BTDM BLE settings in the SDK (e.g. see
        * the sdkconfig.defaults in the example project) */
    ESP_ERROR_CHECK(wifi_prov_scheme_ble_set_service_uuid(custom_service_uuid));

    /* An optional endpoint that applications can create if they expect to
        * get some additional custom data during provisioning workflow.
        * The endpoint name can be anything of your choice.
        * This call must be made before starting the provisioning.
        */
    ESP_ERROR_CHECK(wifi_prov_mgr_endpoint_create("custom-data"));

    /* Do not stop and de-init provisioning so that we can restart it
        * after WiFi success or failure */
    ESP_ERROR_CHECK(wifi_prov_mgr_disable_auto_stop(1000));
    
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, (const void *) sec_params, service_name, service_key));

    wifi_prov_mgr_endpoint_register("custom-data", custom_prov_data_handler, NULL);
}

void app_main(void)
{
    init_esp();
    init_prov();    

    int app_heartbeat_delay_ms = 5000;
    
    #ifdef BLINK
        static uint8_t led_state = 0;
        static uint8_t led_gpio = 5;
        gpio_reset_pin(led_gpio);
        gpio_set_direction(led_gpio, GPIO_MODE_OUTPUT);
        int led_delay_ms = 1000;
        int interval = app_heartbeat_delay_ms / led_delay_ms;
    #endif // BLINK

    while (1) {
        ESP_LOGI(TAG, "===== ===== ===== < App Heartbeat > ===== ===== =====");
        #ifdef BLINK
            for (int i = 0; i < interval; i++) {
                gpio_set_level(led_gpio, led_state);
                led_state = !led_state;
                vTaskDelay(led_delay_ms / portTICK_PERIOD_MS);
            }
        #else
        vTaskDelay(app_heartbeat_delay_ms / portTICK_PERIOD_MS);
        #endif // BLINK
    }
}
