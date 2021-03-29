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

#include <stdio.h>

#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_handle.hpp"

#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define PORT 10000
#define relayPin GPIO_NUM_18

static const char *TAG = "example";

std::string defaultmDNSHostName = "smartdevice";
time_t setTime = 0;
bool relayOn = false;
bool setupMode = true;

void waitForNTPSync();   
void start_mdns_service();

static void checkTime(void *pvParameters);
static void tcp_server_task(void *pvParameters);
const char* bool_cast(const bool b);

extern "C" void app_main(void){
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );

    // Open
    printf("\n");
    printf("Opening Non-Volatile Storage (NVS) handle... ");
    esp_err_t result;
    // Handle will automatically close when going out of scope or when it's reset.
    std::shared_ptr<nvs::NVSHandle> handle = nvs::open_nvs_handle("storage", NVS_READWRITE, &result);

    nvs::ItemType test = nvs::ItemType::SZ;
  
    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        printf("Done\n");
        size_t required_size;
        // Read
        printf("Reading restart counter from NVS ... ");
        // err = handle->get_string("mDNS_HostName", NULL, required_size);
        handle->get_item_size(nvs::ItemType::SZ , "mDNS_HostName", required_size);
        char* nvStoreValue = (char*) malloc(required_size); // value will default to 0, if not set yet in NVS

        err = handle->get_string("mDNS_HostName", nvStoreValue, required_size);


        switch (err) {
            case ESP_OK:
                printf("Done\n");
                printf("NON VOLITILE MEMORY\nmDNS_HostName = %s\n", nvStoreValue);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                printf("The value is not initialized yet!\n");
                break;
            default :
                printf("Error (%s) reading!\n", esp_err_to_name(err));
        }

        // Write
        if(defaultmDNSHostName == )
        printf("Updating restart counter in NVS ... ");
        err = handle->set_string("mDNS_HostName", "big tiddy ass hole");
        printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

        // Commit written value.
        // After setting any values, nvs_commit() must be called to ensure changes are written
        // to flash storage. Implementations may write to storage at other times,
        // but this is not guaranteed.
        printf("Committing updates in NVS ... ");
        err = handle->commit();
        printf((err != ESP_OK) ? "Failed!\n" : "Done\n");
    }


    // gpio_set_direction(relayPin, GPIO_MODE_OUTPUT);
    
    // ESP_ERROR_CHECK(nvs_flash_init());
    // ESP_ERROR_CHECK(esp_netif_init());
    // ESP_ERROR_CHECK(esp_event_loop_create_default());
    // ESP_ERROR_CHECK(example_connect());

    // sntp_setoperatingmode(SNTP_OPMODE_POLL);
    // sntp_setservername(0, "pool.ntp.org");
    // sntp_init();

    // waitForNTPSync();

    // ESP_LOGI(TAG, "Time now : %lu", time(NULL));   

    // xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);
    // xTaskCreate(checkTime, "checkTime", 4096, NULL, 5, NULL);
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

    mdns_hostname_set(mDNSHostName.c_str());
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
        mDNSHostName = rx_buffer;
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