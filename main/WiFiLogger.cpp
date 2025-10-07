#include "WiFiLogger.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/event_groups.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include <cstring>
#include <cstdio>

static const char* TAG = "WIFI_LOGGER";

WiFiLogger::WiFiLogger(const std::string& ssid,
                       const std::string& password,
                       const std::string& server_ip,
                       uint16_t server_port,
                       std::shared_ptr<Logger> logger)
    : ssid_(ssid)
    , password_(password)
    , serverIP_(server_ip)
    , serverPort_(server_port)
    , logger_(logger)
    , wifiStatus_(WiFiStatus::DISCONNECTED)
    , isEnabled_(true)
    , isInitialized_(false)
    , wifiTaskHandle_(nullptr)
    , logTaskHandle_(nullptr)
    , wifiEventGroup_(nullptr) {
    ESP_LOGI(TAG, "WiFiLogger constructor called");
}

WiFiLogger::~WiFiLogger() {
    stop();
    if (wifiEventGroup_) {
        vEventGroupDelete(wifiEventGroup_);
    }
    ESP_LOGI(TAG, "WiFiLogger destructor called");
}

esp_err_t WiFiLogger::init() {
    ESP_LOGI(TAG, "Initializing WiFi Logger");
    
    if (isInitialized_) {
        ESP_LOGW(TAG, "WiFi Logger already initialized");
        return ESP_OK;
    }
    
    if (!logger_) {
        ESP_LOGE(TAG, "Logger instance is null");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Initialize network interface
    esp_err_t ret = esp_netif_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize network interface: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create default event loop
    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to create event loop: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create WiFi event group
    wifiEventGroup_ = xEventGroupCreate();
    if (!wifiEventGroup_) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return ESP_FAIL;
    }
    
    // Initialize WiFi
    ret = initWiFi();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    isInitialized_ = true;
    ESP_LOGI(TAG, "WiFi Logger initialized successfully");
    return ESP_OK;
}

esp_err_t WiFiLogger::start() {
    if (!isInitialized_) {
        ESP_LOGE(TAG, "WiFi Logger not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (wifiTaskHandle_) {
        ESP_LOGW(TAG, "WiFi Logger already running");
        return ESP_OK;
    }
    
    // Create WiFi connection task
    BaseType_t ret = xTaskCreate(wifiTask, 
                                WIFI_TASK_NAME, 
                                WIFI_TASK_STACK_SIZE, 
                                this, 
                                WIFI_TASK_PRIORITY, 
                                &wifiTaskHandle_);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi task");
        return ESP_FAIL;
    }
    
    // Create log forwarding task
    ret = xTaskCreate(logTask, 
                     LOG_TASK_NAME, 
                     LOG_TASK_STACK_SIZE, 
                     this, 
                     LOG_TASK_PRIORITY, 
                     &logTaskHandle_);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create log task");
        vTaskDelete(wifiTaskHandle_);
        wifiTaskHandle_ = nullptr;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "WiFi Logger started successfully");
    return ESP_OK;
}

esp_err_t WiFiLogger::stop() {
    if (wifiTaskHandle_) {
        vTaskDelete(wifiTaskHandle_);
        wifiTaskHandle_ = nullptr;
    }
    
    if (logTaskHandle_) {
        vTaskDelete(logTaskHandle_);
        logTaskHandle_ = nullptr;
    }
    
    disconnectWiFi();
    ESP_LOGI(TAG, "WiFi Logger stopped");
    return ESP_OK;
}

void WiFiLogger::setCredentials(const std::string& ssid, const std::string& password) {
    ssid_ = ssid;
    password_ = password;
    ESP_LOGI(TAG, "Updated WiFi credentials for SSID: %s", ssid.c_str());
}

void WiFiLogger::setServer(const std::string& server_ip, uint16_t server_port) {
    serverIP_ = server_ip;
    serverPort_ = server_port;
    ESP_LOGI(TAG, "Updated log server: %s:%d", server_ip.c_str(), server_port);
}

int32_t WiFiLogger::getRSSI() const {
    if (wifiStatus_ != WiFiStatus::CONNECTED) {
        return 0;
    }
    
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    if (ret != ESP_OK) {
        return 0;
    }
    
    return ap_info.rssi;
}

std::string WiFiLogger::getIPAddress() const {
    if (wifiStatus_ != WiFiStatus::CONNECTED) {
        return "";
    }
    
    esp_netif_ip_info_t ip_info;
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        return std::string(ip_str);
    }
    
    return "";
}

void WiFiLogger::wifiEventHandler(void* arg, esp_event_base_t event_base,
                                  int32_t event_id, void* event_data) {
    WiFiLogger* wifiLogger = static_cast<WiFiLogger*>(arg);
    wifiLogger->handleWiFiEvent(event_base, event_id, event_data);
}

void WiFiLogger::handleWiFiEvent(esp_event_base_t event_base, 
                                 int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                ESP_LOGI(TAG, "WiFi station started");
                wifiStatus_ = WiFiStatus::CONNECTING;
                esp_wifi_connect();
                break;
                
            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to WiFi AP");
                break;
                
            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* disconnected = 
                    static_cast<wifi_event_sta_disconnected_t*>(event_data);
                ESP_LOGW(TAG, "Disconnected from WiFi AP, reason: %d", disconnected->reason);
                wifiStatus_ = WiFiStatus::DISCONNECTED;
                xEventGroupClearBits(wifiEventGroup_, WIFI_CONNECTED_BIT);
                
                // Retry connection
                esp_wifi_connect();
                break;
            }
            
            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t* event = static_cast<ip_event_got_ip_t*>(event_data);
                ESP_LOGI(TAG, "Got IP address: " IPSTR, IP2STR(&event->ip_info.ip));
                wifiStatus_ = WiFiStatus::CONNECTED;
                xEventGroupSetBits(wifiEventGroup_, WIFI_CONNECTED_BIT);
                break;
            }
            
            case IP_EVENT_STA_LOST_IP:
                ESP_LOGW(TAG, "Lost IP address");
                wifiStatus_ = WiFiStatus::DISCONNECTED;
                xEventGroupClearBits(wifiEventGroup_, WIFI_CONNECTED_BIT);
                break;
                
            default:
                break;
        }
    }
}

void WiFiLogger::wifiTask(void* pvParameters) {
    WiFiLogger* wifiLogger = static_cast<WiFiLogger*>(pvParameters);
    wifiLogger->processWiFiConnection();
    vTaskDelete(nullptr);
}

void WiFiLogger::logTask(void* pvParameters) {
    WiFiLogger* wifiLogger = static_cast<WiFiLogger*>(pvParameters);
    wifiLogger->processLogForwarding();
    vTaskDelete(nullptr);
}

esp_err_t WiFiLogger::initWiFi() {
    // Create default WiFi station
    esp_netif_create_default_wifi_sta();
    
    // Initialize WiFi with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Register event handlers
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, 
                                   &wifiEventHandler, this);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WiFi event handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                   &wifiEventHandler, this);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, 
                                   &wifiEventHandler, this);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP lost event handler: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set WiFi mode to station
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi mode: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "WiFi initialized successfully");
    return ESP_OK;
}

esp_err_t WiFiLogger::connectWiFi() {
    if (ssid_.empty()) {
        ESP_LOGE(TAG, "WiFi SSID not set");
        return ESP_ERR_INVALID_ARG;
    }
    
    // Configure WiFi station
    wifi_config_t wifi_config = {};
    strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), ssid_.c_str(), 
            sizeof(wifi_config.sta.ssid) - 1);
    strncpy(reinterpret_cast<char*>(wifi_config.sta.password), password_.c_str(), 
            sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    
    esp_err_t ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi configuration: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Start WiFi
    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "WiFi connection initiated");
    return ESP_OK;
}

esp_err_t WiFiLogger::disconnectWiFi() {
    esp_err_t ret = esp_wifi_disconnect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect WiFi: %s", esp_err_to_name(ret));
        return ret;
    }
    
    wifiStatus_ = WiFiStatus::DISCONNECTED;
    ESP_LOGI(TAG, "WiFi disconnected");
    return ESP_OK;
}

void WiFiLogger::processWiFiConnection() {
    while (isEnabled_) {
        if (wifiStatus_ == WiFiStatus::DISCONNECTED) {
            ESP_LOGI(TAG, "Attempting to connect to WiFi: %s", ssid_.c_str());
            
            esp_err_t ret = connectWiFi();
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to initiate WiFi connection: %s", esp_err_to_name(ret));
                vTaskDelay(pdMS_TO_TICKS(RETRY_DELAY_MS));
                continue;
            }
            
            // Wait for connection
            EventBits_t bits = xEventGroupWaitBits(wifiEventGroup_,
                                                 WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                                 pdFALSE,
                                                 pdFALSE,
                                                 pdMS_TO_TICKS(CONNECTION_TIMEOUT_MS));
            
            if (bits & WIFI_CONNECTED_BIT) {
                ESP_LOGI(TAG, "WiFi connected successfully");
                wifiStatus_ = WiFiStatus::CONNECTED;
            } else if (bits & WIFI_FAIL_BIT) {
                ESP_LOGE(TAG, "WiFi connection failed");
                wifiStatus_ = WiFiStatus::ERROR;
            } else {
                ESP_LOGW(TAG, "WiFi connection timeout");
                wifiStatus_ = WiFiStatus::ERROR;
            }
        }
        
        // Check connection status periodically
        vTaskDelay(pdMS_TO_TICKS(10000)); // 10 seconds
    }
}

void WiFiLogger::processLogForwarding() {
    while (isEnabled_) {
        if (wifiStatus_ == WiFiStatus::CONNECTED) {
            // Send a test message to verify connection
            std::string testMsg = "[" + getTimestamp() + "] [I] [WIFI_LOGGER] Connection test";
            esp_err_t ret = sendLogToServer(testMsg);
            
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to send test message, connection may be lost");
                wifiStatus_ = WiFiStatus::DISCONNECTED;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(30000)); // 30 seconds between tests
    }
}

esp_err_t WiFiLogger::sendLogToServer(const std::string& message) {
    if (wifiStatus_ != WiFiStatus::CONNECTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int sock = createSocket();
    if (sock < 0) {
        return ESP_FAIL;
    }
    
    // Connect to server
    struct sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(serverPort_);
    server_addr.sin_addr.s_addr = inet_addr(serverIP_.c_str());
    
    int ret = connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to connect to log server");
        closeSocket(sock);
        return ESP_FAIL;
    }
    
    // Send message
    ssize_t sent = send(sock, message.c_str(), message.length(), 0);
    if (sent < 0) {
        ESP_LOGE(TAG, "Failed to send log message");
        closeSocket(sock);
        return ESP_FAIL;
    }
    
    closeSocket(sock);
    return ESP_OK;
}

int WiFiLogger::createSocket() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create socket");
        return -1;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = SOCKET_TIMEOUT_MS / 1000;
    timeout.tv_usec = (SOCKET_TIMEOUT_MS % 1000) * 1000;
    
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    return sock;
}

void WiFiLogger::closeSocket(int sockfd) {
    if (sockfd >= 0) {
        close(sockfd);
    }
}

std::string WiFiLogger::getTimestamp() const {
    uint64_t timestamp = esp_timer_get_time();
    uint64_t seconds = timestamp / 1000000;
    uint64_t microseconds = timestamp % 1000000;
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%llu.%06llu", 
            (unsigned long long)seconds, (unsigned long long)microseconds);
    
    return std::string(buffer);
}
