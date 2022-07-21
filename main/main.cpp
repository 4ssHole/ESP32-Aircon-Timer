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
#include "TCPServer.hpp"
#include "driver/gpio.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

#define PORT 10000
#define relayPin GPIO_NUM_21

static const char *TAG = "MAIN";

static const char *NVS_KEY_setupMode = "setupComplete";
static const int NVS_VALUE_setupMode = 0;

static const char *NVS_KEY_DeviceName = "DeviceName";
static const char *NVS_VALUE_DeviceName = "DefaultName";

static time_t setTime = 0;
static bool relayOn = false;

EventGroupHandle_t s_general_event_group = xEventGroupCreate();
static const int TCP_SERVER_STARTED = BIT0;
static const int MDNS_NAME_SET = BIT1;
static const int NTP_SYNC_COMPLETE = BIT2;

static TaskHandle_t serverHandle = NULL;
static TaskHandle_t mainHandle;
EventGroupHandle_t s_wifi_event_group;


static void setupWiFi();
void setMDNSName(std::string recievedName);
void setupMDNS();
void start_mdns_service(char* hostName);

static void waitForNTPSync(void *pvParameters);   
static void checkTime(void *pvParameters);
const char* bool_cast(const bool b);
static void TCPReceive(void *pvParameters);
void main_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);

extern "C" void app_main(void){

    // ESP_ERROR_CHECK(nvs_flash_erase()); // ctrl, e, r
    gpio_set_direction(relayPin, GPIO_MODE_INPUT_OUTPUT);

    setupWiFi();

    s_wifi_event_group = xEventGroupCreate();
    esp_event_loop_create_default();

    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &main_event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &main_event_handler, NULL);

    xTaskCreate(waitForNTPSync, "ntp_sync", 4096, NULL, 5, NULL);
    xEventGroupWaitBits(s_general_event_group, NTP_SYNC_COMPLETE, true, false, portMAX_DELAY); 

    xTaskCreate(TCPReceive, "tcp_server", 4096, NULL, 5, &serverHandle);
    setupMDNS();
}

void main_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnect_reason = (wifi_event_sta_disconnected_t*) event_data;

        ESP_LOGE(TAG, "disconnect %i", disconnect_reason->reason);
        esp_wifi_connect();
    } 
}

static void TCPReceive(void *pvParameters){
    TCPServer tcp(PORT, (void*)AF_INET);
    mainHandle = tcp.start(serverHandle);

    xEventGroupSetBits(s_general_event_group, TCP_SERVER_STARTED);
    char *rx_buffer;

    while(1){
        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
        rx_buffer = tcp.getRX();

        ESP_LOGI(TAG, "MESSAGE RECEIVED: %s", rx_buffer);
            switch(rx_buffer[0]){
                case tcp.type_time:
                    {
                        setTime = std::stoi(rx_buffer+1);
                        ESP_LOGI(TAG, "%lu", setTime);
                        tcp.transmit(tcp.type_time, std::to_string(setTime).append("\n").c_str());
                    }
                    break;
                case tcp.type_mDNS_Name:
                    ESP_LOGI(TAG, "MDNS NAME");
                    setMDNSName(rx_buffer+1); //+1 should bypass first character which is the identifier but idk for sure
                    tcp.transmit(tcp.type_mDNS_Name, "mdns\n");
                    break;
                case tcp.type_status:
                    ESP_LOGI(TAG, "SENDING STATUS");
                    tcp.transmit(tcp.type_status, ((relayOn ? "1" : "0") + (std::to_string(setTime) + "\n")).c_str());
                    break;
                default:
                    tcp.transmit(tcp.null, "Unknown message type\n");
                    break;
            }

        xTaskNotifyGive(mainHandle);
    }

    vTaskDelete(NULL);
}

void setupWiFi(){
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
        relayOn = gpio_get_level(relayPin);

        if( ( timeNow < setTime ) && ( !relayOn ) ){
            gpio_set_level(relayPin, 1);
            ESP_LOGI(TAG, "START RELAY");   
        }
        else if( ( timeNow >= setTime ) && ( relayOn ) ){
            gpio_set_level(relayPin, 0);
            ESP_LOGI(TAG, "STOP RELAY");   
        }

        // ESP_LOGI(TAG, "Counting : %lu", time(NULL));
        vTaskDelay(50); //100 is 1 second
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


    ESP_ERROR_CHECK(mdns_service_add(NULL, "_AirconTimer", "_tcp", PORT, serviceTxtData, 3));
}

static void waitForNTPSync(void *pvParameters){    
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    
    while(time(NULL) < 120){
        ESP_LOGI(TAG, "Time now : %lu", time(NULL));
        vTaskDelay(100);
    }

    ESP_LOGW(TAG, "NTP SYNC COMPLETE");
    xTaskCreate(checkTime, "time_check", 4096, NULL, 5, NULL);

    xEventGroupSetBits(s_general_event_group, NTP_SYNC_COMPLETE);
    vTaskDelete(NULL);
} 

void setMDNSName(std::string recievedName){
    ESP_LOGI(TAG, "MDNS Name set to : %s", recievedName.c_str());
    if(recievedName[0] != '\0' && recievedName.length() <= 256) //maximum length for an mdns hostname is 256 including \0 nvs strings are 4000
        NVStoreHelper().writeString(NVS_KEY_DeviceName, recievedName.c_str());
}

void setupMDNS(){
    std::string StoredName = NVStoreHelper().getString(NVS_KEY_DeviceName);

    if('\0' == StoredName[0]){ //TODO: More robust empty string checking
        ESP_LOGE(TAG, "NVSTORE EMPTY");
        //send device name to android app, handle the case of a blank device name in app 
        xEventGroupWaitBits(s_general_event_group, MDNS_NAME_SET, true, false, portMAX_DELAY); 
    }
    else{
        // Wait for bit for tcp server start and setupmode to be set using eventGroupWaitBits
        ESP_LOGE(TAG, "NVSTORE HAS VALUE");
        xEventGroupSetBits(s_general_event_group, MDNS_NAME_SET);
    }

    xEventGroupWaitBits(s_general_event_group, TCP_SERVER_STARTED, true, false, portMAX_DELAY); 
    start_mdns_service((char *) StoredName.c_str());
}