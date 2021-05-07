#include "NVStoreHelper.hpp"

#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_handle.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_event.h"

#include <string>

static const char *TAG = "NVStoreHelper";

NVStoreHelper::NVStoreHelper(){
    m_err = nvs_flash_init();
    if (m_err == ESP_ERR_NVS_NO_FREE_PAGES || m_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        m_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK( m_err );
    openNVSHandle();
}

void NVStoreHelper::openNVSHandle(){
    ESP_LOGI(TAG, "Opening Non-Volatile Storage (NVS) handle... ");
    // Handle will automatically close when going out of scope or when it's reset.
    m_handle = nvs::open_nvs_handle("storage", NVS_READWRITE, &m_result);    
}

std::string NVStoreHelper::getString(const char *KEY){
    std::string returnValue = "error";
    if (m_err != ESP_OK) ESP_LOGI(TAG, "Error (%s) opening NVS handle!: ", esp_err_to_name(m_err));
    else {
        size_t required_size;
        // Read
        ESP_LOGI(TAG, "Reading String from NVS");
        m_handle->get_item_size(nvs::ItemType::SZ , KEY, required_size);
        m_nvStoreValue = (char*) malloc(required_size);
        m_err = m_handle->get_string(KEY, m_nvStoreValue, required_size);
 
        switch (m_err) {
            case ESP_OK:
                ESP_LOGI(TAG, "Done 2\n");
                ESP_LOGI(TAG, "NON VOLITILE MEMORY\nStored value = %s", m_nvStoreValue);

                returnValue = (char *) m_nvStoreValue;

                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG, "The value is not initialized yet!\n");
                returnValue = "";
                break;
            default :
                ESP_LOGI(TAG, "Error (%s) reading!: ", esp_err_to_name(m_err));
        }                   
    }

    m_err = nvs_flash_init();

    return returnValue;
}

int NVStoreHelper::getInt(char *KEY){
    int returnValue = 2;
    int value;

    if (m_err != ESP_OK) {
        ESP_LOGI(TAG, "Error (%s) opening NVS handle!: ", esp_err_to_name(m_err));
    }
    else {
        m_err = m_handle->get_item(KEY, value);

        switch (m_err) {
            case ESP_OK:
                ESP_LOGI(TAG, "NON VOLITILE MEMORY\nStored value = %d", value);
                returnValue = value; // should be 0 for a setup esp
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                ESP_LOGI(TAG, "The value is not initialized yet!\n");
                returnValue = 1;

                break;
            default :
                ESP_LOGI(TAG, "Error (%s) reading!: ", esp_err_to_name(m_err));
                returnValue = 2;
        }                   
    }

    m_err = nvs_flash_init();

    return returnValue;
}


void NVStoreHelper::writeString(const char *KEY, const char *VALUE){
    ESP_LOGI(TAG, "writing String to KEY: %s\nValue: %s", KEY, VALUE);
    m_err = m_handle->set_string(KEY, VALUE);

    ESP_LOGI(TAG, "Committing updates in NVS ... ");
    m_err = m_handle->commit();
}

void NVStoreHelper::writeInt(char *KEY, int VALUE){
    ESP_LOGI(TAG, "writing Int to KEY: %s\nValue: %d", KEY, VALUE);
    m_err = m_handle->set_item(KEY, VALUE);

    ESP_LOGI(TAG, "Committing updates in NVS ... ");
    m_err = m_handle->commit();
}