#include "SmartConfig.hpp"

const char *SmartConfig::TAG = "Smart Config helper";
EventGroupHandle_t SmartConfig::s_wifi_event_group = nullptr;

SmartConfig::SmartConfig(){
    ESP_ERROR_CHECK( nvs_flash_init() );

    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif && "sta_netif assert false"); //assert terminates application on false 

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA));

    
    wifi_config_t conf;
    esp_wifi_get_config(WIFI_IF_STA, &conf);
    
    if(conf.sta.ssid[0] != '\0'){
        ESP_LOGI(TAG, "\nSSID :%s\n", conf.sta.ssid);
        ESP_LOGI(TAG, "\nPassword :%s\n", conf.sta.password);

        ESP_ERROR_CHECK( esp_wifi_start() );        
        connectWifi();
    }
    else if(conf.sta.ssid != nullptr && conf.sta.ssid[0] == '\0'){
        ESP_LOGE(TAG, "\nSSID is blank");
        ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
        ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
        ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
        ESP_ERROR_CHECK( esp_wifi_start() );     
    }
}
void SmartConfig::connectWifi(){
    switch(esp_wifi_connect()){
        case ESP_OK:
            ESP_LOGE(TAG, "ESP_OK");
            xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
            break;
        case ESP_ERR_WIFI_SSID:
            ESP_LOGE(TAG, "ESP_ERR_WIFI_SSID");
            ESP_ERROR_CHECK( esp_wifi_stop() );  
            ESP_ERROR_CHECK( esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
            ESP_ERROR_CHECK( esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL) );
            ESP_ERROR_CHECK( esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL) );
            ESP_ERROR_CHECK( esp_wifi_start() );        
            break;
        case ESP_ERR_WIFI_CONN:
            ESP_LOGE(TAG, "ESP_ERR_WIFI_CONN");
            break;
        case ESP_ERR_WIFI_NOT_STARTED:
            ESP_LOGE(TAG, "ESP_ERR_WIFI_NOT_STARTED");
            break;
        case ESP_ERR_WIFI_NOT_INIT:
            ESP_LOGE(TAG, "ESP_ERR_WIFI_NOT_INIT");
            break;
        case ESP_ERR_WIFI_MODE:
            ESP_LOGE(TAG, "ESP_ERR_WIFI_MODE");
            break;
        default:
            ESP_LOGE(TAG, "UNKNOWN WIFI CONNECT ERROR");
            break;
    }
}
void SmartConfig::smartconfig_example_task(void * parm){
    ESP_LOGI(TAG, "SMART CONFIG TASK");

    EventBits_t uxBits;
    ESP_ERROR_CHECK( esp_smartconfig_set_type(SC_TYPE_ESPTOUCH) );
    smartconfig_start_config_t cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
    
    ESP_ERROR_CHECK( esp_smartconfig_fast_mode(true) );
    ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
    while (1) {
        ESP_LOGI(TAG, "WAITING %d", CONNECTED_BIT);

        uxBits = xEventGroupWaitBits(s_wifi_event_group, CONNECTED_BIT | ESPTOUCH_DONE_BIT | CONNECT_FAILED, true, false, portMAX_DELAY); 
        if(uxBits & CONNECT_FAILED){
            //TODO SET LED TO BLINK or something
            ESP_LOGI(TAG, "CONNECTION FAILED, try new password");
            esp_smartconfig_stop();
            
            xEventGroupClearBits(s_wifi_event_group, CONNECT_FAILED);
            ESP_ERROR_CHECK( esp_smartconfig_start(&cfg) );
        }
        if(uxBits & CONNECTED_BIT) {
            ESP_LOGI(TAG, "WiFi Connected to ap");
        }
        if(uxBits & ESPTOUCH_DONE_BIT) {
            ESP_LOGI(TAG, "smartconfig over");
            esp_smartconfig_stop();
            vTaskDelete(NULL);
        }
    }
}
void SmartConfig::event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){
    ESP_LOGI(TAG, "\nEVENT BASE : %s\nEVENT ID : %d",event_base, event_id);
    if ((event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)) {
        xTaskCreate(smartconfig_example_task, "smartconfig_example_task", 4096, NULL, 3, NULL);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        //todo restart sc task
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
        if( event->reason == 15 || event->reason == 205) {
            ESP_LOGE(TAG, "%d", event->reason);
            xEventGroupSetBits(s_wifi_event_group, CONNECT_FAILED);

            //TODO Send failure packet(?) to Android app to notify failed password
            //impossible since smartconfig can only receive and not send as far as I can tell
        }
        else{
            connectWifi();
        }
        xEventGroupClearBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_wifi_event_group, CONNECTED_BIT);
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "Scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "Found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "Got SSID and password");

        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;
        wifi_config_t wifi_config;
        uint8_t ssid[33] = { 0 };
        uint8_t password[65] = { 0 };

        bzero(&wifi_config, sizeof(wifi_config_t));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));
        wifi_config.sta.bssid_set = evt->bssid_set;
        if (wifi_config.sta.bssid_set == true) {
            memcpy(wifi_config.sta.bssid, evt->bssid, sizeof(wifi_config.sta.bssid));
        }

        memcpy(ssid, evt->ssid, sizeof(evt->ssid));
        memcpy(password, evt->password, sizeof(evt->password));
        ESP_LOGI(TAG, "SSID:%s", ssid);
        ESP_LOGI(TAG, "PASSWORD:%s", password);

        ESP_ERROR_CHECK(esp_wifi_disconnect());
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

        connectWifi();

    } else if (event_base == SC_EVENT && event_id == SC_EVENT_SEND_ACK_DONE) {
        xEventGroupSetBits(s_wifi_event_group, ESPTOUCH_DONE_BIT);
    }
}