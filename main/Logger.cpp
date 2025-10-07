#include "Logger.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "LOGGER";

Logger::Logger(uart_port_t uart_num, 
               gpio_num_t tx_pin, 
               gpio_num_t rx_pin,
               uint32_t baud_rate,
               size_t buffer_size)
    : uartNum_(uart_num)
    , txPin_(tx_pin)
    , rxPin_(rx_pin)
    , baudRate_(baud_rate)
    , bufferSize_(buffer_size)
    , logQueue_(nullptr)
    , logTaskHandle_(nullptr)
    , minLogLevel_(LogLevel::INFO)
    , isEnabled_(true)
    , isInitialized_(false) {
    ESP_LOGI(TAG, "Logger constructor called");
}

Logger::~Logger() {
    stop();
    if (logQueue_) {
        vQueueDelete(logQueue_);
    }
    ESP_LOGI(TAG, "Logger destructor called");
}

esp_err_t Logger::init() {
    ESP_LOGI(TAG, "Initializing Logger");
    
    if (isInitialized_) {
        ESP_LOGW(TAG, "Logger already initialized");
        return ESP_OK;
    }
    
    // Initialize UART
    esp_err_t ret = initUART();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UART: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create log queue
    ret = createLogQueue();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create log queue: %s", esp_err_to_name(ret));
        return ret;
    }
    
    isInitialized_ = true;
    ESP_LOGI(TAG, "Logger initialized successfully");
    return ESP_OK;
}

esp_err_t Logger::start() {
    if (!isInitialized_) {
        ESP_LOGE(TAG, "Logger not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (logTaskHandle_) {
        ESP_LOGW(TAG, "Logger task already running");
        return ESP_OK;
    }
    
    esp_err_t ret = createLogTask();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create log task: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Logger started successfully");
    return ESP_OK;
}

esp_err_t Logger::stop() {
    if (logTaskHandle_) {
        // Signal task to stop
        LogMessage stopMsg;
        stopMsg.level = LogLevel::INFO;
        stopMsg.tag = "LOGGER";
        stopMsg.message = "STOP_TASK";
        stopMsg.timestamp = esp_timer_get_time();
        
        xQueueSend(logQueue_, &stopMsg, portMAX_DELAY);
        
        // Wait for task to finish
        vTaskDelete(logTaskHandle_);
        logTaskHandle_ = nullptr;
        
        ESP_LOGI(TAG, "Logger stopped");
    }
    
    return ESP_OK;
}

void Logger::log(LogLevel level, const char* tag, const char* format, ...) {
    if (!isEnabled_ || !isInitialized_ || !logQueue_ || level > minLogLevel_) {
        return;
    }
    
    char buffer[512];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0 && len < sizeof(buffer)) {
        LogMessage msg(level, tag, std::string(buffer, len));
        
        // Non-blocking send to avoid blocking calling task
        BaseType_t result = xQueueSend(logQueue_, &msg, 0);
        if (result != pdTRUE) {
            // Queue is full, discard message
            // In production, you might want to handle this differently
        }
    }
}

void Logger::error(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0 && len < sizeof(buffer)) {
        log(LogLevel::ERROR, tag, "%s", buffer);
    }
}

void Logger::warn(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0 && len < sizeof(buffer)) {
        log(LogLevel::WARN, tag, "%s", buffer);
    }
}

void Logger::info(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0 && len < sizeof(buffer)) {
        log(LogLevel::INFO, tag, "%s", buffer);
    }
}

void Logger::debug(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0 && len < sizeof(buffer)) {
        log(LogLevel::DEBUG, tag, "%s", buffer);
    }
}

void Logger::verbose(const char* tag, const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[512];
    int len = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    
    if (len > 0 && len < sizeof(buffer)) {
        log(LogLevel::VERBOSE, tag, "%s", buffer);
    }
}

const char* Logger::getLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::ERROR:   return "E";
        case LogLevel::WARN:    return "W";
        case LogLevel::INFO:    return "I";
        case LogLevel::DEBUG:   return "D";
        case LogLevel::VERBOSE: return "V";
        default:                return "?";
    }
}

std::string Logger::formatTimestamp(uint64_t timestamp) {
    uint64_t seconds = timestamp / 1000000;
    uint64_t microseconds = timestamp % 1000000;
    
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%llu.%06llu", 
            (unsigned long long)seconds, (unsigned long long)microseconds);
    
    return std::string(buffer);
}

void Logger::logTask(void* pvParameters) {
    Logger* logger = static_cast<Logger*>(pvParameters);
    logger->processLogQueue();
    
    // Task cleanup
    vTaskDelete(nullptr);
}

void Logger::processLogQueue() {
    LogMessage msg;
    
    while (true) {
        // Wait for log message
        if (xQueueReceive(logQueue_, &msg, portMAX_DELAY) == pdTRUE) {
            // Check for stop signal
            if (msg.tag == "LOGGER" && msg.message == "STOP_TASK") {
                break;
            }
            
            // Send log message to UART
            sendLogMessage(msg);
        }
    }
}

void Logger::sendLogMessage(const LogMessage& message) {
    std::string formatted = formatLogMessage(message);
    
    // Send to UART
    uart_write_bytes(uartNum_, formatted.c_str(), formatted.length());
    
    // Also send newline
    uart_write_bytes(uartNum_, "\n", 1);
}

std::string Logger::formatLogMessage(const LogMessage& message) {
    std::string timestamp = formatTimestamp(message.timestamp);
    const char* levelStr = getLevelString(message.level);
    
    char buffer[1024];
    snprintf(buffer, sizeof(buffer), "[%s][%s][%s] %s",
            timestamp.c_str(),
            levelStr,
            message.tag.c_str(),
            message.message.c_str());
    
    return std::string(buffer);
}

esp_err_t Logger::initUART() {
    uart_config_t uart_config = {};
    uart_config.baud_rate = baudRate_;
    uart_config.data_bits = UART_DATA_8_BITS;
    uart_config.parity = UART_PARITY_DISABLE;
    uart_config.stop_bits = UART_STOP_BITS_1;
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_config.source_clk = UART_SCLK_APB;
    
    esp_err_t ret = uart_param_config(uartNum_, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART parameters: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_set_pin(uartNum_, txPin_, rxPin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_driver_install(uartNum_, 1024, 1024, 0, nullptr, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "UART configured on port %d, TX=%d, RX=%d, baud=%lu", 
            uartNum_, txPin_, rxPin_, baudRate_);
    return ESP_OK;
}

esp_err_t Logger::createLogQueue() {
    logQueue_ = xQueueCreate(bufferSize_, sizeof(LogMessage));
    if (!logQueue_) {
        ESP_LOGE(TAG, "Failed to create log queue");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Log queue created with size %zu", bufferSize_);
    return ESP_OK;
}

esp_err_t Logger::createLogTask() {
    BaseType_t ret = xTaskCreate(logTask, 
                                LOG_TASK_NAME, 
                                LOG_TASK_STACK_SIZE, 
                                this, 
                                LOG_TASK_PRIORITY, 
                                &logTaskHandle_);
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create log task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Log task created");
    return ESP_OK;
}