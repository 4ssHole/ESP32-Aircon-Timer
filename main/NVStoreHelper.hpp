#pragma once

#include "nvs_flash.h"
#include "nvs.h"
#include "nvs_handle.hpp"

#include "esp_log.h"

class NVStoreHelper{
    public:
        NVStoreHelper(const NVStoreHelper&) = delete;

        static NVStoreHelper& Get(){
            static NVStoreHelper Instance;
            return Instance;
        }  

        void writeString(char *KEY, char *VALUE);
        void writeInt(char *KEY, int VALUE);
        std::string getString(char *KEY);
        int getInt(char *KEY);

    private:        
        static std::shared_ptr<nvs::NVSHandle> m_handle;

        static esp_err_t m_err;
        static esp_err_t m_result;

        static char* m_nvStoreValue;

        static void openNVSHandle();
        
        NVStoreHelper();
};