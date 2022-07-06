#pragma once

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>
#include <string>

class TCPServer{

    public:
        TCPServer(int Port, void *PvParameters);

        enum TCP_Types{
            null, type_time, type_mDNS_Name, type_status
        };

        TaskHandle_t &start(TaskHandle_t &serverHandle);
        static void transmit(int type, const char *buffer);
        char *getRX();

    private:    
        void *pvParameters; 
        
        static void run(void *pvParameters);
        static void recieveTCP();
};