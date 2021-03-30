#pragma once

#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_handle.hpp"

#include "esp_log.h"

class NVStoreHelper{
    private:        
        std::shared_ptr<nvs::NVSHandle> handle;

        std::string KEY;
        std::string VALUE;

        esp_err_t err;
        esp_err_t result;

        char* nvStoreValue;

        void initializeNVS();
        void openNVSHandle();

    public:
        NVStoreHelper();
        NVStoreHelper(std::string Key, std::string Value);

        void setKey(std::string Key);
        void setValue(std::string Value);
        
        void writeNVS();
        std::string readNVS();
};