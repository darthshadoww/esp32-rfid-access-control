#ifndef LOGGER_H
#define LOGGER_H

#include "esp_err.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <string>
#include <cstdarg>

/**
 * @brief Logger class for thread-safe UART logging
 * 
 * This class provides thread-safe logging functionality over UART.
 * It uses a dedicated task and queue system to ensure logging
 * doesn't block other tasks and maintains proper formatting.
 */
class Logger {
public:
    /**
     * @brief Log level enumeration
     */
    enum class LogLevel {
        ERROR = 0,
        WARN = 1,
        INFO = 2,
        DEBUG = 3,
        VERBOSE = 4
    };

    /**
     * @brief Log message structure
     */
    struct LogMessage {
        LogLevel level;
        std::string tag;
        std::string message;
        uint64_t timestamp;
        
        LogMessage() : level(LogLevel::INFO), timestamp(0) {}
        
        LogMessage(LogLevel lvl, const std::string& t, const std::string& msg)
            : level(lvl), tag(t), message(msg), timestamp(esp_timer_get_time()) {}
    };

    /**
     * @brief Constructor
     * @param uart_num UART port number
     * @param tx_pin TX GPIO pin
     * @param rx_pin RX GPIO pin (optional, can be -1 if not used)
     * @param baud_rate UART baud rate (default: 115200)
     * @param buffer_size Log queue buffer size
     */
    Logger(uart_port_t uart_num = UART_NUM_0, 
           gpio_num_t tx_pin = GPIO_NUM_1, 
           gpio_num_t rx_pin = GPIO_NUM_3,
           uint32_t baud_rate = 115200,
           size_t buffer_size = 256);

    /**
     * @brief Destructor
     */
    ~Logger();

    /**
     * @brief Initialize the logger
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t init();

    /**
     * @brief Start the logging task
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t start();

    /**
     * @brief Stop the logging task
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t stop();

    /**
     * @brief Log a message with specified level and tag
     * @param level Log level
     * @param tag Log tag
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    void log(LogLevel level, const char* tag, const char* format, ...);

    /**
     * @brief Log error message
     * @param tag Log tag
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    void error(const char* tag, const char* format, ...);

    /**
     * @brief Log warning message
     * @param tag Log tag
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    void warn(const char* tag, const char* format, ...);

    /**
     * @brief Log info message
     * @param tag Log tag
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    void info(const char* tag, const char* format, ...);

    /**
     * @brief Log debug message
     * @param tag Log tag
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    void debug(const char* tag, const char* format, ...);

    /**
     * @brief Log verbose message
     * @param tag Log tag
     * @param format Printf-style format string
     * @param ... Format arguments
     */
    void verbose(const char* tag, const char* format, ...);

    /**
     * @brief Set minimum log level
     * @param level Minimum log level to display
     */
    void setLogLevel(LogLevel level) { minLogLevel_ = level; }

    /**
     * @brief Get current log level
     * @return Current minimum log level
     */
    LogLevel getLogLevel() const { return minLogLevel_; }

    /**
     * @brief Check if logging is enabled
     * @return true if logging is enabled, false otherwise
     */
    bool isEnabled() const { return isEnabled_; }

    /**
     * @brief Enable or disable logging
     * @param enabled true to enable, false to disable
     */
    void setEnabled(bool enabled) { isEnabled_ = enabled; }

    /**
     * @brief Get log level string
     * @param level Log level
     * @return String representation of log level
     */
    static const char* getLevelString(LogLevel level);

    /**
     * @brief Convert timestamp to human-readable format
     * @param timestamp Timestamp in microseconds
     * @return Formatted timestamp string
     */
    static std::string formatTimestamp(uint64_t timestamp);

private:
    // UART configuration
    uart_port_t uartNum_;
    gpio_num_t txPin_;
    gpio_num_t rxPin_;
    uint32_t baudRate_;
    
    // Queue configuration
    size_t bufferSize_;
    QueueHandle_t logQueue_;
    
    // Task configuration
    TaskHandle_t logTaskHandle_;
    static constexpr const char* LOG_TASK_NAME = "logger_task";
    static constexpr uint32_t LOG_TASK_STACK_SIZE = 4096;
    static constexpr UBaseType_t LOG_TASK_PRIORITY = 1;
    
    // Logging configuration
    LogLevel minLogLevel_;
    bool isEnabled_;
    bool isInitialized_;
    
    // Static task function
    static void logTask(void* pvParameters);
    
    /**
     * @brief Process log messages from queue
     */
    void processLogQueue();
    
    /**
     * @brief Send log message to UART
     * @param message Log message to send
     */
    void sendLogMessage(const LogMessage& message);
    
    /**
     * @brief Format log message for output
     * @param message Log message to format
     * @return Formatted log string
     */
    std::string formatLogMessage(const LogMessage& message);
    
    /**
     * @brief Initialize UART
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t initUART();
    
    /**
     * @brief Create log queue
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t createLogQueue();
    
    /**
     * @brief Create log task
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t createLogTask();
};

#endif // LOGGER_H