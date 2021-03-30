#include "esp_system.h"
#include "esp_sntp.h"
#include "esp_event.h"
#include "esp_log.h"

#include <string>

#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_handle.hpp"

class NVStoreHelper { 
    private std::string KEY;
    private std::string VALUE;

    private void initializeNVS();
    private std::string readNVS(std::string KEY, std::string VALUE);
    private void writeNVS(std::string KEY, std::string VALUE);
    private std::shared_ptr<nvs::NVSHandle> handle;
    
    NVStoreHelper(){
        initializeNVS();
        ESP_LOGI("Opening Non-Volatile Storage (NVS) handle... ");
        esp_err_t result;
        // Handle will automatically close when going out of scope or when it's reset.
        handle = nvs::open_nvs_handle("storage", NVS_READWRITE, &result);
    }

    NVStoreHelper(std::string KEY, std::string VALUE){
        
    }

    private void initializeNVS(){
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            // NVS partition was truncated and needs to be erased
            // Retry nvs_flash_init
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
        ESP_ERROR_CHECK( err );
    }

    
    private std::string readNVS(std::string KEY, std::string VALUE){

    }
    private void writeNVS(std::string KEY, std::string VALUE){

    }



    if (err != ESP_OK) {
        printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } else {
        printf("Done\n");
        size_t required_size;
        // Read
        printf("Reading restart counter from NVS ... ");
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
        if(defaultmDNSHostName != nvStoreValue && setupMode){
            printf("Updating restart counter in NVS ... ");
            err = handle->set_string("mDNS_HostName", defaultmDNSHostName.c_str());
            printf((err != ESP_OK) ? "Failed!\n" : "Done\n");

            printf("Committing updates in NVS ... ");
            err = handle->commit();
            printf((err != ESP_OK) ? "Failed!\n" : "Done\n");
        }
        else if (defaultmDNSHostName == nvStoreValue){
            setupMode = true;
        }
    }
}