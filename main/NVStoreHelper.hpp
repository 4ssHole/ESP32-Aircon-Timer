#pragma once

#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_handle.hpp"

#include "esp_log.h"

class NVStoreHelper{
    private:        
        std::shared_ptr<nvs::NVSHandle> handle;

        esp_err_t err;
        esp_err_t result;

        char* nvStoreValue;

        void initializeNVS();
        void openNVSHandle();

    public:
        NVStoreHelper();

        void writeString(char *KEY, char *VALUE);
        void writeInt(char *KEY, int VALUE);
        std::string getString(char *KEY);
        int getInt(char *KEY);
};