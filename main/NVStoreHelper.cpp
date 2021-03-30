#include "NVStoreHelper.hpp"

#include "esp_system.h"
#include "esp_event.h"

#include <string>

static const char *TAG = "NVStoreHelper";

NVStoreHelper::NVStoreHelper(){
    initializeNVS();
  
    ESP_LOGI(TAG, "Opening Non-Volatile Storage (NVS) handle... ");
    // Handle will automatically close when going out of scope or when it's reset.
    handle = nvs::open_nvs_handle("storage", NVS_READWRITE, &result);      
}

void NVStoreHelper::initializeNVS(){
    err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( err );
}

std::string NVStoreHelper::readNVS(std::string Key){
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Done\n");
        size_t required_size;
        // Read
        ESP_LOGI(TAG, "Reading restart counter from NVS ... ");
        handle->get_item_size(nvs::ItemType::SZ , Key.c_str(), required_size);
        nvStoreValue = (char*) malloc(required_size); // value will default to 0, if not set yet in NVS

        err = handle->get_string(Key.c_str(), nvStoreValue, required_size);

        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Done\n");
                ESP_LOGI(TAG, "NON VOLITILE MEMORY\nStored value = %s\n", nvStoreValue);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG, "The value is not initialized yet!\n");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s) reading!\n", esp_err_to_name(err));
        }                   
    }
    return nvStoreValue;
}
void NVStoreHelper::writeNVS(std::string Key, std::string Value){
    ESP_LOGI(TAG, "Updating restart counter in NVS ... ");
    err = handle->set_string(Key.c_str(), Value.c_str());
    // ESP_LOGI(TAG, (err != ESP_OK) ? "Failed!\n" : "Done\n");

    ESP_LOGI(TAG, "Committing updates in NVS ... ");
    err = handle->commit();
    // ESP_LOGI(TAG, (err != ESP_OK) ? "Failed!\n" : "Done\n");
}


