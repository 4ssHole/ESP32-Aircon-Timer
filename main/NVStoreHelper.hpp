#pragma once

#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_handle.hpp"

#include "esp_log.h"

class NVStoreHelper{
    private:
        std::string KEY;
        std::string VALUE;
        std::shared_ptr<nvs::NVSHandle> handle;

        esp_err_t err;
        esp_err_t result;

        char* nvStoreValue;

        void initializeNVS();

    public:
        NVStoreHelper();
        void setKey(std::string Key);
        void setValue(std::string Value);
        void writeNVS(std::string Key, std::string Value);
        std::string readNVS(std::string Key);



};