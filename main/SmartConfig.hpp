#pragma once

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_smartconfig.h"

class SmartConfig{
    public:
        SmartConfig(const SmartConfig&) = delete;

        static SmartConfig& Get(){
            static SmartConfig Instance;
            return Instance;
        }  
        
        static EventGroupHandle_t s_wifi_event_group;
        
        static const int CONNECTED_BIT = BIT0;
        static const int ESPTOUCH_DONE_BIT = BIT1;
        static const int CONNECT_FAILED = BIT2;
        static void connectWifi();

    private:
        static const char *TAG;

        static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
        static void smartconfig_example_task(void * parm);


        SmartConfig();
};