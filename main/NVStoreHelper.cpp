#include "NVStoreHelper.hpp"

#include "esp_system.h"
#include "esp_event.h"

#include <string>

static const char *TAG = "NVStoreHelper";

NVStoreHelper::NVStoreHelper(){
    initializeNVS();
    openNVSHandle();
}

NVStoreHelper::NVStoreHelper(std::string Key, std::string Value){
    initializeNVS();
    openNVSHandle();

    setKey(Key);
    setValue(Value);
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

void NVStoreHelper::openNVSHandle(){
    ESP_LOGI(TAG, "Opening Non-Volatile Storage (NVS) handle... ");
    // Handle will automatically close when going out of scope or when it's reset.
    handle = nvs::open_nvs_handle("storage", NVS_READWRITE, &result);    
}

std::string NVStoreHelper::readNVS(){
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Error (%s) opening NVS handle!: ", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Done\n");
        size_t required_size;
        // Read
        ESP_LOGI(TAG, "Reading restart counter from NVS ... ");
        handle->get_item_size(nvs::ItemType::SZ , KEY.c_str(), required_size);
        nvStoreValue = (char*) malloc(required_size); // value will default to 0, if not set yet in NVS

        err = handle->get_string(KEY.c_str(), nvStoreValue, required_size);

        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Done\n");
                ESP_LOGI(TAG, "NON VOLITILE MEMORY\nStored value = %s", nvStoreValue);
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG, "The value is not initialized yet!\n");
                break;
            default :
                ESP_LOGI(TAG, "Error (%s) reading!: ", esp_err_to_name(err));
        }                   
    }

    std::string nvStore = nvStoreValue;
    free(nvStoreValue); 

    return nvStore;
}

void NVStoreHelper::setKey(std::string Key){
    KEY = Key;
    ESP_LOGI(TAG, "Key set to : %s", KEY.c_str());
}

void NVStoreHelper::setValue(std::string Value){
    VALUE = Value;
    ESP_LOGI(TAG, "Value set to : %s", VALUE.c_str());
}

void NVStoreHelper::writeNVS(){
    ESP_LOGI(TAG, "writing to KEY: %s\nValue: %s",KEY.c_str() , VALUE.c_str());
    err = handle->set_string(KEY.c_str(), VALUE.c_str());

    ESP_LOGI(TAG, "Committing updates in NVS ... ");
    err = handle->commit();
}