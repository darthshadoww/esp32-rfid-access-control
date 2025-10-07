#ifndef WIFI_LOGGER_H
#define WIFI_LOGGER_H

#include "esp_err.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "Logger.h"
#include <string>
#include <memory>

/**
 * @brief WiFiLogger class for TCP/IP logging
 * 
 * This class extends the Logger functionality to send log messages
 * over TCP/IP to a remote server. It handles WiFi connection,
 * TCP socket communication, and automatic reconnection.
 */
class WiFiLogger {
public:
    /**
     * @brief WiFi connection status
     */
    enum class WiFiStatus {
        DISCONNECTED = 0,
        CONNECTING = 1,
        CONNECTED = 2,
        ERROR = 3
    };

    /**
     * @brief Constructor
     * @param ssid WiFi SSID
     * @param password WiFi password
     * @param server_ip Log server IP address
     * @param server_port Log server port
     * @param logger Reference to Logger instance
     */
    WiFiLogger(const std::string& ssid,
               const std::string& password,
               const std::string& server_ip,
               uint16_t server_port,
               std::shared_ptr<Logger> logger);

    /**
     * @brief Destructor
     */
    ~WiFiLogger();

    /**
     * @brief Initialize WiFi logger
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t init();

    /**
     * @brief Start WiFi connection and logging task
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t start();

    /**
     * @brief Stop WiFi logger
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t stop();

    /**
     * @brief Check if WiFi is connected
     * @return true if connected, false otherwise
     */
    bool isConnected() const { return wifiStatus_ == WiFiStatus::CONNECTED; }

    /**
     * @brief Get current WiFi status
     * @return Current WiFi status
     */
    WiFiStatus getWiFiStatus() const { return wifiStatus_; }

    /**
     * @brief Enable or disable WiFi logging
     * @param enabled true to enable, false to disable
     */
    void setEnabled(bool enabled) { isEnabled_ = enabled; }

    /**
     * @brief Check if WiFi logging is enabled
     * @return true if enabled, false otherwise
     */
    bool isEnabled() const { return isEnabled_; }

    /**
     * @brief Set WiFi credentials
     * @param ssid WiFi SSID
     * @param password WiFi password
     */
    void setCredentials(const std::string& ssid, const std::string& password);

    /**
     * @brief Set log server address
     * @param server_ip Server IP address
     * @param server_port Server port
     */
    void setServer(const std::string& server_ip, uint16_t server_port);

    /**
     * @brief Get WiFi RSSI
     * @return WiFi signal strength in dBm, 0 if not connected
     */
    int32_t getRSSI() const;

    /**
     * @brief Get WiFi IP address
     * @return IP address string, empty if not connected
     */
    std::string getIPAddress() const;

private:
    // WiFi configuration
    std::string ssid_;
    std::string password_;
    std::string serverIP_;
    uint16_t serverPort_;
    
    // Logger reference
    std::shared_ptr<Logger> logger_;
    
    // Status variables
    WiFiStatus wifiStatus_;
    bool isEnabled_;
    bool isInitialized_;
    
    // Task handles
    TaskHandle_t wifiTaskHandle_;
    TaskHandle_t logTaskHandle_;
    
    // Event group for WiFi events
    EventGroupHandle_t wifiEventGroup_;
    static constexpr int WIFI_CONNECTED_BIT = BIT0;
    static constexpr int WIFI_FAIL_BIT = BIT1;
    
    // Task names and parameters
    static constexpr const char* WIFI_TASK_NAME = "wifi_logger_task";
    static constexpr const char* LOG_TASK_NAME = "wifi_log_task";
    static constexpr uint32_t WIFI_TASK_STACK_SIZE = 4096;
    static constexpr uint32_t LOG_TASK_STACK_SIZE = 4096;
    static constexpr UBaseType_t WIFI_TASK_PRIORITY = 2;
    static constexpr UBaseType_t LOG_TASK_PRIORITY = 1;
    
    // Connection parameters
    static constexpr uint32_t MAX_RETRY_COUNT = 5;
    static constexpr uint32_t RETRY_DELAY_MS = 5000;
    static constexpr uint32_t CONNECTION_TIMEOUT_MS = 10000;
    
    // Socket parameters
    static constexpr uint32_t SOCKET_TIMEOUT_MS = 5000;
    static constexpr uint32_t RECONNECT_DELAY_MS = 10000;
    
    // Static event handlers
    static void wifiEventHandler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);
    
    // Static task functions
    static void wifiTask(void* pvParameters);
    static void logTask(void* pvParameters);
    
    /**
     * @brief Initialize WiFi
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t initWiFi();
    
    /**
     * @brief Connect to WiFi
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t connectWiFi();
    
    /**
     * @brief Disconnect from WiFi
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t disconnectWiFi();
    
    /**
     * @brief Handle WiFi events
     * @param event_base Event base
     * @param event_id Event ID
     * @param event_data Event data
     */
    void handleWiFiEvent(esp_event_base_t event_base, 
                        int32_t event_id, void* event_data);
    
    /**
     * @brief Process WiFi connection task
     */
    void processWiFiConnection();
    
    /**
     * @brief Process log forwarding task
     */
    void processLogForwarding();
    
    /**
     * @brief Send log message to server
     * @param message Log message to send
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t sendLogToServer(const std::string& message);
    
    /**
     * @brief Create TCP socket
     * @return Socket file descriptor, -1 on error
     */
    int createSocket();
    
    /**
     * @brief Close TCP socket
     * @param sockfd Socket file descriptor
     */
    void closeSocket(int sockfd);
    
    /**
     * @brief Get current timestamp string
     * @return Timestamp string
     */
    std::string getTimestamp() const;
};

#endif // WIFI_LOGGER_H
