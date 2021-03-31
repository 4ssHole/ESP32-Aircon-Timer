/* 

TODO   

Setup:
mDNS server for discovery
Network discovery in android app possibly using mDNS
Pairing and passwords
if ntp cannot sync, notify in app and change to time offset only mode

Functionallity:
Store sent times in rtc memory to survive deep sleep
Store mDNS name and password in persistent memory

Hardware: 
Add leds for status indicators



DONE:
Get current time from ntp, do not accept requests until time is synced
Preliminary mDNS implementation
Sending epoch to client

*/

#include <string>
#include <sys/param.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "mdns.h"

#include "NVStoreHelper.hpp"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define PORT 10000
#define relayPin GPIO_NUM_18

static const char *TAG = "MAIN";

static const char *NVS_KEY_mDNSHostName = "mDNSHostName";
static const char *NVS_VALUE_mDNSHostName = "smartdevice";

static const char *NVS_KEY_setupMode = "setupComplete";
static const int NVS_VALUE_setupMode = 0;

bool setupMode;

time_t setTime = 0;
bool relayOn = false;

void waitForNTPSync();   
void start_mdns_service();
void setupDevice();

static void checkTime(void *pvParameters);
static void tcp_server_task(void *pvParameters);
const char* bool_cast(const bool b);

NVStoreHelper nvStoreHelper = NVStoreHelper();

extern "C" void app_main(void){
    gpio_set_direction(relayPin, GPIO_MODE_OUTPUT);
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());

    ESP_LOGI(TAG, "get string : %s",nvStoreHelper.getString((char *) NVS_KEY_mDNSHostName).c_str());

    int test = nvStoreHelper.getInt((char *) NVS_KEY_setupMode);
    ESP_LOGI(TAG, "int test : %d", test);

    switch(test){
        case 1:
            setupMode = true;
            ESP_LOGI(TAG, "IN SETUP MODE");
            setupDevice();

            break;
        case 0:
            setupMode = false;
            ESP_LOGI(TAG, "IN NORMAL MODE");
            // ESP_ERROR_CHECK(nvs_flash_erase()); // remove when in production

            break;
        case 2:
            ESP_LOGE(TAG, "NVS INT ERROR");
            
            break;
        default:
            ESP_LOGE(TAG, "NOTHING RETURNED NVS INT ERROR");
    }

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();

    waitForNTPSync();
    // ESP_LOGI(TAG, "Time now : %lu", time(NULL));   

    // xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);
    // xTaskCreate(checkTime, "checkTime", 4096, NULL, 5, NULL);
}

void setupDevice(){


    nvStoreHelper.writeInt((char *) NVS_KEY_setupMode, 0);
}


void checkTime(void *pvParameters){  
    while(1){
        time_t timeNow = time(NULL);

        switch(relayOn){
            case false:
                if(timeNow < setTime){
                    relayOn = true;
                    gpio_set_level(relayPin, relayOn);
                    ESP_LOGI(TAG, "START RELAY");   
                }
            break;

            case true:
                ESP_LOGI(TAG, "Counting : %lu", time(NULL));
                if(timeNow >= setTime){
                    relayOn = false;
                    gpio_set_level(relayPin, relayOn);
                    ESP_LOGI(TAG, "STOP RELAY");   
                }
            break;
        } 
        sleep(1);
    }  
    vTaskDelete(NULL);
}

void start_mdns_service()
{
    esp_err_t err = mdns_init();
    if (err) {
        printf("MDNS Init failed: %d\n", err);
        return;
    }

    mdns_hostname_set(NVS_VALUE_mDNSHostName);
    mdns_instance_name_set("Development ESP32 S2");
}

static void recieveEpoch(const int sock){
    int len;
    char rx_buffer[128];

    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        setTime = atoi(rx_buffer);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            ESP_LOGI(TAG, "setTime : %lu rx_buffer : %s", setTime, rx_buffer);
        }
    } while (len > 0);
}

static void setupVariables(const int sock){
    int len;
    char rx_buffer[128];
    do {
        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            ESP_LOGI(TAG, "setTime : %lu rx_buffer : %s", setTime, rx_buffer);
        }
    } while (len > 0);


    setupMode = false;
}

static void tcp_server_task(void *pvParameters)
{
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;

    start_mdns_service();

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    } else if (addr_family == AF_INET6) {
        bzero(&dest_addr.sin6_addr.un, sizeof(dest_addr.sin6_addr.un));
        dest_addr.sin6_family = AF_INET6;
        dest_addr.sin6_port = htons(PORT);
        ip_protocol = IPPROTO_IPV6;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {

        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
        uint addr_len = sizeof(source_addr);
        int sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Convert ip address to string
        if (source_addr.sin6_family == PF_INET) inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);
        else if (source_addr.sin6_family == PF_INET6) inet6_ntoa_r(source_addr.sin6_addr, addr_str, sizeof(addr_str) - 1);
        
        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);

        int written = send(sock, bool_cast(setupMode), 1, 0);
        if (written < 0) {
            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        }

        if(setupMode){
            setupVariables(sock);
        }
        else{
            recieveEpoch(sock);
        }

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

void waitForNTPSync(){    
    while(time(NULL) < 120){
        ESP_LOGI(TAG, "Time now : %lu", time(NULL));
        sleep(1);
    }
} 

const char* bool_cast(const bool b) {
    return b ? "1" : "0";
}