/* 

TODO   

Setup:
mDNS server for discovery
Network discovery in android app possibly using mDNS
Pairing and passwords
if ntp cannot sync, notify in app and change to time offset only mode
scan for ap ssid in nvs, start smartconfig when not found

Functionallity:
Change to randomly select unused ports to avoid conflicts
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

enum TCP_Types{
    null, type_time, type_mDNS_Name, 
};

static const char *TAG = "MAIN";

static const char *NVS_KEY_setupMode = "setupComplete";
static const int NVS_VALUE_setupMode = 0;

static const char *NVS_KEY_DeviceName = "DeviceName";
static const char *NVS_VALUE_DeviceName = "DefaultName";

static time_t setTime = 0;

EventGroupHandle_t s_tcp_event_group = xEventGroupCreate();
static const int TCP_SERVER_STARTED = BIT0;
static const int MDNS_NAME_SET = BIT1;
static const int NTP_SYNC_COMPLETE = BIT2;


static void waitForWifi();
void setMDNSName(std::string recievedName);
void setupMDNS();
void start_mdns_service(char* hostName);

static void waitForNTPSync(void *pvParameters);   
static void checkTime(void *pvParameters);
static void tcp_server_task(void *pvParameters);
const char* bool_cast(const bool b);


extern "C" void app_main(void){
    // ESP_ERROR_CHECK(nvs_flash_erase()); // ctrl, e, r
    gpio_set_direction(relayPin, GPIO_MODE_INPUT_OUTPUT);

    waitForWifi();

    xTaskCreate(waitForNTPSync, "ntp_sync", 4096, NULL, 5, NULL);
    xTaskCreate(tcp_server_task, "tcp_server", 4096, (void*)AF_INET, 5, NULL);

    setupMDNS();
}

void waitForWifi(){
    //TODO make class return the value to us instead of taking it from the class
    //apparantly thats best practice
    //ive made no progress for 1-2 weeks on this because of that so fuck it
    
    SmartConfig::Get();

    int connectedBit = 0;
    while(connectedBit == 0){
        connectedBit = xEventGroupWaitBits(SmartConfig::s_wifi_event_group, SmartConfig::CONNECTED_BIT, true, false, portMAX_DELAY); 
    }
}

static void checkTime(void *pvParameters){ 
    time_t timeNow;
    
    while(1){
        timeNow = time(NULL);

        if( ( timeNow < setTime ) && ( gpio_get_level(relayPin) == 0) ){
            gpio_set_level(relayPin, 1);
            ESP_LOGI(TAG, "START RELAY");   
            ESP_LOGI(TAG, "Counting : %lu", time(NULL));
        }
        else if( ( timeNow >= setTime ) && ( gpio_get_level(relayPin) == 1) ){
            gpio_set_level(relayPin, 0);
            ESP_LOGI(TAG, "STOP RELAY");   
        }

        vTaskDelay(100); //100 is 1 second
    }  
    vTaskDelete(NULL);
}

void start_mdns_service(char* hostName){
    esp_err_t err = mdns_init();
    if (err) {
        printf("MDNS Init failed: %d\n", err);
        return;
    }

    uint8_t MACAddress[NETIF_MAX_HWADDR_LEN];
    char MAC[18];

    mdns_hostname_set(hostName);
    
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
        {"Name", hostName}
    };


    ESP_ERROR_CHECK(mdns_service_add("CHANGE TO SET NAME", "_AirconTimer", "_tcp", 10000, serviceTxtData, 3));
}

static void parseTCP(char *rx_buffer, int *len){
    ESP_LOGI(TAG, "Received %d bytes: %s", *len, rx_buffer);

    std::string dirtyBuffer(rx_buffer);
    std::string cleanedBuffer;

    if(rx_buffer[0] != '\0') 
         cleanedBuffer = dirtyBuffer.substr(1, (dirtyBuffer.length()-1));

    switch(rx_buffer[0]){
        case type_time:
            {
                EventBits_t uxBits = xEventGroupWaitBits(s_tcp_event_group, MDNS_NAME_SET | NTP_SYNC_COMPLETE, false, true, 1);

                ESP_LOGI("TIME", "%s", cleanedBuffer.c_str());
                
                if(rx_buffer[1] != null){
                    if( ( uxBits & ( MDNS_NAME_SET | NTP_SYNC_COMPLETE ) ) == ( MDNS_NAME_SET | NTP_SYNC_COMPLETE ) ) {
                        setTime = stoi(cleanedBuffer);
                    }
                }
            }
            break;
        case type_mDNS_Name:
            setMDNSName(cleanedBuffer);
            break;
    }
}

void recieveTCP(const int *sock){
    int len;
    char rx_buffer[256];

    do {
        len = recv(*sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } else {
            // rx_buffer[len] = 0; // Null-terminate whatever is received and treat it like a string
            std::string timePrevious = '\01'+std::to_string(setTime)+'\n';
            char const *pchar = timePrevious.c_str();  //use char const* as target type

            ESP_LOGW(TAG, "TimePrevious : %s", timePrevious.c_str());

            int to_write = strlen(pchar);
            while (to_write > 0) {

                int written  = send(*sock, pchar, to_write, 0);
                if (written < 0) {
                    ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
                }
                to_write -= written;
            }
        }
    } while (len > 0);

    parseTCP(rx_buffer, &len);
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

    xEventGroupSetBits(s_tcp_event_group, TCP_SERVER_STARTED);

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
        recieveTCP(&sock);

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

static void waitForNTPSync(void *pvParameters){    
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    
    while(time(NULL) < 120){
        // ESP_LOGI(TAG, "Time now : %lu", time(NULL));
        vTaskDelay(100);
    }

    ESP_LOGW(TAG, "NTP SYNC COMPLETE");
    xTaskCreate(checkTime, "time_check", 4096, NULL, 5, NULL);

    xEventGroupSetBits(s_tcp_event_group, NTP_SYNC_COMPLETE);
    vTaskDelete(NULL);
} 

void setMDNSName(std::string recievedName){
    if(recievedName[0] != '\0' && recievedName.length() <= 256) //maximum length for an mdns hostname is 256 including \0 nvs strings are 4000
        NVStoreHelper().writeString(NVS_KEY_DeviceName, recievedName.c_str());
}

void setupMDNS(){
    std::string StoredName = NVStoreHelper().getString(NVS_KEY_DeviceName);

    if('\0' == StoredName[0]){ //TODO: More robust empty string checking
        ESP_LOGE(TAG, "NVSTORE EMPTY");
        //send device name to android app, handle the case of a blank device name in app 
        xEventGroupWaitBits(s_tcp_event_group, MDNS_NAME_SET, true, false, portMAX_DELAY); 
    }
    else{
        // Wait for bit for tcp server start and setupmode to be set using eventGroupWaitBits
        ESP_LOGE(TAG, "NVSTORE HAS VALUE");
        xEventGroupSetBits(s_tcp_event_group, MDNS_NAME_SET);
    }

    xEventGroupWaitBits(s_tcp_event_group, TCP_SERVER_STARTED, true, false, portMAX_DELAY); 
    start_mdns_service((char *) StoredName.c_str());
}

const char* bool_cast(const bool b) {
    return b ? "1" : "0";
}