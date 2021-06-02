#include "TCPServer.hpp"

static const char *TAG = "TCP Server";

static int PORT; 
static int sock;

static char rx_buffer[256];

static TaskHandle_t ServerHandle;
static TaskHandle_t MainHandle = NULL;


TCPServer::TCPServer(int Port, void *PvParameters){
    PORT=Port;
    pvParameters = PvParameters;
}

TaskHandle_t &TCPServer::start(TaskHandle_t &serverHandle){
    xTaskCreate(run, "tcp_server", 4096, (void*)AF_INET, 5, &MainHandle); 

    ServerHandle = serverHandle;

    return MainHandle;
}

void TCPServer::transmit(const char *buffer){
    int to_write = strlen(buffer);
    while (to_write > 0) {

        int written  = send(sock, buffer, to_write, 0);
        if (written < 0) {
            ESP_LOGE(TAG, "Error occurred during sending: errno %d", errno);
        }
        to_write -= written;
    }
}

void TCPServer::run(void *pvParameters){
    char addr_str[128];
    int addr_family = (int)pvParameters;
    int ip_protocol = 0;
    struct sockaddr_in6 dest_addr;

    if (addr_family == AF_INET) {
        struct sockaddr_in *dest_addr_ip4 = (struct sockaddr_in *)&dest_addr;
        dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
        dest_addr_ip4->sin_family = AF_INET;
        dest_addr_ip4->sin_port = htons(PORT);
        ip_protocol = IPPROTO_IP;
    }

    int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
    if (listen_sock < 0) {
        ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Socket created");

    int err = bind(listen_sock, (struct sockaddr *)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
        ESP_LOGE(TAG, "IPPROTO: %d", addr_family);
        goto CLEAN_UP;
    }
    ESP_LOGI(TAG, "Socket bound, port %d", PORT);

    err = listen(listen_sock, 1);
    if (err != 0) {
        ESP_LOGE(TAG, "Error occurred during listen: errno %d", errno);
        goto CLEAN_UP;
    }

    while (1) {
        ESP_LOGI(TAG, "Socket listening");

        struct sockaddr_in6 source_addr; // Large enough for both IPv4 or IPv6
        uint addr_len = sizeof(source_addr);
        sock = accept(listen_sock, (struct sockaddr *)&source_addr, &addr_len);
        if (sock < 0) {
            ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
            break;
        }

        // Convert ip address to string
        if (source_addr.sin6_family == PF_INET) inet_ntoa_r(((struct sockaddr_in *)&source_addr)->sin_addr.s_addr, addr_str, sizeof(addr_str) - 1);

        ESP_LOGI(TAG, "Socket accepted ip address: %s", addr_str);
        recieveTCP();

        ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

        shutdown(sock, 0);
        close(sock);
    }

CLEAN_UP:
    close(listen_sock);
    vTaskDelete(NULL);
}

void TCPServer::recieveTCP(){
    int len;
    memset(rx_buffer, 0, sizeof(rx_buffer));

    do {

        len = recv(sock, rx_buffer, sizeof(rx_buffer) - 1, 0);
        ESP_LOGI(TAG, "Received :%s, bytes:%d", rx_buffer, len);
        if (len < 0) {
            ESP_LOGE(TAG, "Error occurred during receiving: errno %d", errno);
        } else if (len == 0) {
            ESP_LOGW(TAG, "Connection closed");
        } 
        else {
            // yield here until transmit is called from main
            xTaskNotifyGive(ServerHandle);
            // ulTaskNotifyTake( pdTRUE, portMAX_DELAY );

        }
    } while (len > 0);
}

char *TCPServer::getRX(){
    return rx_buffer;
}