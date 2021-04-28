/* 

TODO   

Setup:
mDNS server for discovery
Network discovery in android app possibly using mDNS
Pairing and passwords
if ntp cannot sync, notify in app and change to time offset only mode
scan for ap ssid in nvs, start smartconfig when not found

Functionallity:
Change to randomly select unused ports 
Store sent times in rtc memory to survive deep sleep
Store mDNS name and password in persistent memory
Press and hold a button to start smartconfig 
only start tcp server when IP_EVENT_STA_GOT_IP
tcp server keep ailve and wait on disconnect until smartconfig or change ip

Hardware: 
Add leds for status indicators
Pairing button for new wifi networks

DONE:
Get current time from ntp, do not accept requests until time is synced
Preliminary mDNS implementation
Sending epoch to client

*/

#include <string>
#include <sstream>
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
#include "mdns.h"

#include "esp_smartconfig.h"
#include "esp_wpa2.h"
#include "freertos/event_groups.h"

#include "SmartConfig.hpp"
#include "NVStoreHelper.hpp"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define PORT 10000
#define relayPin GPIO_NUM_18

static const char *TAG = "MAIN";

static const char *NVS_KEY_DeviceName = "DeviceName";
static const char *NVS_VALUE_DeviceName;

static const char *NVS_KEY_setupMode = "setupComplete";
static const int NVS_VALUE_setupMode = 0;


bool setupMode;

time_t setTime = 0;
bool relayOn = false;

void waitForNTPSync();   
void start_mdns_service();
static void waitForWifi();

static void checkTime(void *pvParameters);
static void tcp_server_task(void *pvParameters);
const char* bool_cast(const bool b);

void setupMDNS(){


    // Wait for bit for tcp server start and setupmode to be set using eventGroupWaitBits
    // start_mdns_service();
}

extern "C" void app_main(void){
    ESP_ERROR_CHECK(nvs_flash_erase()); // remove when in production
    gpio_set_direction(relayPin, GPIO_MODE_OUTPUT);

    SmartConfig::Get();
    waitForWifi();
    waitForNTPSync();
    

    // ESP_LOGI(TAG, "get string : %s",nvStoreHelper.getString((char *) NVS_KEY_DeviceName).c_str());

    // int test = nvStoreHelper.getInt((char *) NVS_KEY_setupMode);
    // ESP_LOGI(TAG, "int test : %d", test);

    // switch(test){
    //     case 1:
    //         setupMode = true;
    //         ESP_LOGI(TAG, "IN SETUP MODE");
    //         break;
    //     case 0:
    //         setupMode = false;
    //         ESP_LOGI(TAG, "IN NORMAL MODE");
    //         ESP_ERROR_CHECK(nvs_flash_erase()); // remove when in production
    //         break;
    //     case 2:
    //         ESP_LOGE(TAG, "NVS INT ERROR");
    //         break;
    //     default:
    //         ESP_LOGE(TAG, "NOTHING RETURNED NVS INT ERROR");
    // }

    // xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);
}
static void waitForWifi(){
    //TODO make class return the value to us instead of taking it from the class
    //apparantly thats best practice
    //ive made no progress for 1-2 weeks on this because of that so fuck it
    
    int connectedBit = 0;
    while(connectedBit == 0){
        connectedBit = xEventGroupWaitBits(SmartConfig::s_wifi_event_group, SmartConfig::CONNECTED_BIT, true, false, portMAX_DELAY); 
        
        if(connectedBit){
            ESP_LOGI(TAG, "WIFI CONNECTED, CONTINUE");
        }
    }
}
void checkTime(void *pvParameters){  
    time_t timeNow;
    
    while(1){
        timeNow = time(NULL);

        if(timeNow < setTime){
            gpio_set_level(relayPin, 1);
            ESP_LOGI(TAG, "START RELAY");   
        }
        if(timeNow >= setTime){
            ESP_LOGI(TAG, "Counting : %lu", time(NULL));
            gpio_set_level(relayPin, 0);
            ESP_LOGI(TAG, "STOP RELAY");   
            vTaskDelete(NULL);
        }

        vTaskDelay(1000);
    }  
}
void start_mdns_service(){
    esp_err_t err = mdns_init();
    if (err) {
        printf("MDNS Init failed: %d\n", err);
        return;
    }

    uint8_t MACAddress[NETIF_MAX_HWADDR_LEN];
    char MAC[18];

    mdns_hostname_set(NVS_VALUE_DeviceName);
    
    esp_read_mac(MACAddress, ESP_MAC_WIFI_STA);
    snprintf(MAC, sizeof(MAC), "%02x:%02x:%02x:%02x:%02x:%02x",
         MACAddress[0], MACAddress[1], MACAddress[2], MACAddress[3], MACAddress[4], MACAddress[5]);

    ESP_LOGI(TAG, "TEST %s", MAC);

    mdns_txt_item_t serviceTxtData[3] = {
        //TODO: add unique board id, maybe mac address
        {"ModuleVersion", "ESP32-S2"},
        {"MACAddress", MAC},
        // {"hasPassword", },
        // {"isSetup", },
        {"Name", NVS_VALUE_DeviceName}
    };


    ESP_ERROR_CHECK(mdns_service_add("CHANGE TO SET NAME", "_AirconTimer", "_tcp", 10000, serviceTxtData, 3));
}
static void tcp_server_task(void *pvParameters){
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;


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

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}
void waitForNTPSync(){    
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    
    while(time(NULL) < 120){
        ESP_LOGI(TAG, "Time now : %lu", time(NULL));
        vTaskDelay(1000);
    }
} 
const char* bool_cast(const bool b) {
    return b ? "1" : "0";
}