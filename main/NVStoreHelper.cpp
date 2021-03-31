#include "NVStoreHelper.hpp"

#include "esp_system.h"
#include "esp_event.h"

#include <string>

static const char *TAG = "NVStoreHelper";

NVStoreHelper::NVStoreHelper(){
    initializeNVS();
    openNVSHandle();
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

std::string NVStoreHelper::getString(char *KEY){
    std::string returnValue = "error";
    if (err != ESP_OK) ESP_LOGI(TAG, "Error (%s) opening NVS handle!: ", esp_err_to_name(err));
    else {
        size_t required_size;
        // Read
        ESP_LOGI(TAG, "Reading String from NVS");
        handle->get_item_size(nvs::ItemType::SZ , KEY, required_size);
        nvStoreValue = (char*) malloc(required_size);
        err = handle->get_string(KEY, nvStoreValue, required_size);
 
        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Done 2\n");
                ESP_LOGI(TAG, "NON VOLITILE MEMORY\nStored value = %s", nvStoreValue);

                returnValue = (char *) nvStoreValue;

                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG, "The value is not initialized yet!\n");
                returnValue = "";
                break;
            default :
                ESP_LOGI(TAG, "Error (%s) reading!: ", esp_err_to_name(err));
        }                   
    }

    err = nvs_flash_init();

    return returnValue;
}

int NVStoreHelper::getInt(char *KEY){
    int returnValue = 2;
    int value;

    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Error (%s) opening NVS handle!: ", esp_err_to_name(err));
    }
    else {
        err = handle->get_item(KEY, value);

        switch (err) {
            case ESP_OK:
                ESP_LOGI(TAG, "NON VOLITILE MEMORY\nStored value = %d", value);
                returnValue = value; // should be 0 for a setup esp
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG, "The value is not initialized yet!\n");
                returnValue = 1;

                break;
            default :
                ESP_LOGI(TAG, "Error (%s) reading!: ", esp_err_to_name(err));
                returnValue = 2;
        }                   
    }

    err = nvs_flash_init();

    return returnValue;
}


void NVStoreHelper::writeString(char *KEY, char *VALUE){
    ESP_LOGI(TAG, "writing String to KEY: %s\nValue: %s", KEY, VALUE);
    err = handle->set_string(KEY, VALUE);

    ESP_LOGI(TAG, "Committing updates in NVS ... ");
    err = handle->commit();
}

void NVStoreHelper::writeInt(char *KEY, int VALUE){
    ESP_LOGI(TAG, "writing Int to KEY: %s\nValue: %d", KEY, VALUE);
    err = handle->set_item(KEY, VALUE);

    ESP_LOGI(TAG, "Committing updates in NVS ... ");
    err = handle->commit();
}