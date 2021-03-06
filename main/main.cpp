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
#define relayPin GPIO_NUM_18

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


static void setupWiFi();
void setMDNSName(std::string recievedName);
void setupMDNS();
void start_mdns_service(char* hostName);

static void waitForNTPSync(void *pvParameters);   
static void checkTime(void *pvParameters);
const char* bool_cast(const bool b);
static void TCPReceive(void *pvParameters);


extern "C" void app_main(void){
    // ESP_ERROR_CHECK(nvs_flash_erase()); // ctrl, e, r
    gpio_set_direction(relayPin, GPIO_MODE_INPUT_OUTPUT);

    setupWiFi();

    // xTaskCreate(waitForNTPSync, "ntp_sync", 1024, NULL, 5, NULL);
    xTaskCreate(TCPReceive, "tcp_server", 4096, NULL, 5, &serverHandle);
    xTaskCreate(checkTime, "check_time", 2048, NULL, 3, NULL);
    setupMDNS();
}

static void TCPReceive(void *pvParameters){
    TCPServer tcp(PORT, (void*)AF_INET);
    mainHandle = tcp.start(serverHandle);

    char *rx_buffer;

    while(1){
        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );
        rx_buffer = tcp.getRX();

        ESP_LOGI(TAG, "MESSAGE RECIEVED: %s", rx_buffer);
            switch(rx_buffer[0]){
                case tcp.type_time:
                    {
                        setTime = std::stoi(rx_buffer+1);
                        ESP_LOGI(TAG, "%lu", setTime);
                        tcp.transmit(std::to_string(setTime).append("\n").c_str());
                    }
                    break;
                case tcp.type_mDNS_Name:
                    setMDNSName(rx_buffer+1); //+1 should bypass first character which is the identifier but idk for sure
                    tcp.transmit("mdns \n");
                    break;
                case tcp.relay_status:
                    tcp.transmit(relayOn ? "1\n" : "0\n");
                    break;
                default:
                    tcp.transmit("Unknown message type \n");
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
            ESP_LOGI(TAG, "Counting : %lu", time(NULL));
        }
        else if( ( timeNow >= setTime ) && ( relayOn ) ){
            gpio_set_level(relayPin, 0);
            ESP_LOGI(TAG, "STOP RELAY");   
        }

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
        // ESP_LOGI(TAG, "Time now : %lu", time(NULL));
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