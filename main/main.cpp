#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_timer.h"
#include "nvs_flash.h"

// Include our custom classes
#include "RFIDReader.h"
#include "ServoController.h"
#include "Logger.h"
#include "WiFiLogger.h"
#include "BuzzerController.h"

// Task names and stack sizes
static constexpr const char* TAG = "MAIN";
static constexpr const char* RFID_TASK_NAME = "rfid_task";
static constexpr const char* SERVO_TASK_NAME = "servo_task";
static constexpr const char* LOGGER_TASK_NAME = "logger_task";
static constexpr const char* WIFI_LOGGER_TASK_NAME = "wifi_logger_task";

static constexpr uint32_t RFID_TASK_STACK_SIZE = 4096;
static constexpr uint32_t SERVO_TASK_STACK_SIZE = 3072;
static constexpr uint32_t LOGGER_TASK_STACK_SIZE = 3072;
static constexpr uint32_t WIFI_LOGGER_TASK_STACK_SIZE = 4096;

// Task priorities
static constexpr UBaseType_t RFID_TASK_PRIORITY = 3;
static constexpr UBaseType_t SERVO_TASK_PRIORITY = 2;
static constexpr UBaseType_t LOGGER_TASK_PRIORITY = 1;
static constexpr UBaseType_t WIFI_LOGGER_TASK_PRIORITY = 1;

// Queue sizes
static constexpr size_t RFID_QUEUE_SIZE = 10;
static constexpr size_t SERVO_QUEUE_SIZE = 5;
static constexpr size_t LOG_QUEUE_SIZE = 20;

// Event group bits
static constexpr int RFID_CARD_DETECTED_BIT = BIT0;
static constexpr int SERVO_OPEN_BIT = BIT1;
static constexpr int SERVO_CLOSE_BIT = BIT2;

// Timing constants
static constexpr uint32_t DOOR_OPEN_TIME_MS = 5000;  // 5 seconds
static constexpr uint32_t RFID_SCAN_INTERVAL_MS = 100; // 100ms

// Global objects
std::shared_ptr<Logger> g_logger;
std::shared_ptr<WiFiLogger> g_wifiLogger;
std::shared_ptr<RFIDReader> g_rfidReader;
std::shared_ptr<ServoController> g_servoController;
std::shared_ptr<BuzzerController> g_buzzerController;

// Global queues and event groups
QueueHandle_t g_rfidQueue;
QueueHandle_t g_servoQueue;
QueueHandle_t g_logQueue;
EventGroupHandle_t g_eventGroup;

// Task handles
TaskHandle_t g_rfidTaskHandle;
TaskHandle_t g_servoTaskHandle;
TaskHandle_t g_loggerTaskHandle;
TaskHandle_t g_wifiLoggerTaskHandle;

// Authorized UIDs (example UIDs - replace with your actual card UIDs)
static const std::vector<std::vector<uint8_t>> AUTHORIZED_UIDS = {
    {0x12, 0x34, 0x56, 0x78},  // Example UID 1
    {0xAB, 0xCD, 0xEF, 0x01},  // Example UID 2
    {0xDE, 0xAD, 0xBE, 0xEF}   // Example UID 3
};

/**
 * @brief RFID scanning task
 * Continuously scans for RFID cards and processes detected cards
 */
void rfidTask(void* pvParameters) {
    ESP_LOGI(TAG, "RFID task started");
    
    RFIDReader::CardData cardData;
    uint32_t lastScanTime = 0;

    while (true) {
        uint32_t currentTime = esp_timer_get_time() / 1000; // Convert to milliseconds
        
        // Scan for cards at specified interval
        if (currentTime - lastScanTime >= RFID_SCAN_INTERVAL_MS) {
            esp_err_t ret = g_rfidReader->readCard(cardData);
            
            // Debug: Log every scan attempt (every 5 seconds)
            static uint32_t lastDebugTime = 0;
            if (currentTime - lastDebugTime >= 5000) {
                bool cardPresent = g_rfidReader->isCardPresent();
                ESP_LOGI(TAG, "RFID scan attempt - ret: %s, isValid: %s, cardPresent: %s", 
                        esp_err_to_name(ret), cardData.isValid ? "true" : "false", 
                        cardPresent ? "true" : "false");
                lastDebugTime = currentTime;
            }
            
            if (ret == ESP_OK && cardData.isValid) {
                ESP_LOGI(TAG, "Card detected: %s", 
                        RFIDReader::uidToString(cardData.uid).c_str());
                
                // Check if card is authorized
                bool isAuthorized = g_rfidReader->isAuthorized(cardData.uid);
                
                if (isAuthorized) {
                    ESP_LOGI(TAG, "Authorized card detected - opening door");
                    
                    // Play success sound
                    if (g_buzzerController) {
                        g_buzzerController->playSound(BuzzerController::SoundType::SUCCESS, 60);
                    }
                    
                    // Send message to servo task
                    ServoController::DoorPosition doorCmd = ServoController::DoorPosition::OPEN;
                    xQueueSend(g_servoQueue, &doorCmd, pdMS_TO_TICKS(100));
                    
                    // Set event bit
                    xEventGroupSetBits(g_eventGroup, RFID_CARD_DETECTED_BIT | SERVO_OPEN_BIT);
                    
                    // Log the access
                    g_logger->info(TAG, "Access granted for UID: %s", 
                                  RFIDReader::uidToString(cardData.uid).c_str());
                } else {
                    ESP_LOGW(TAG, "Unauthorized card detected - access denied");
                    
                    // Play denied sound
                    if (g_buzzerController) {
                        g_buzzerController->playSound(BuzzerController::SoundType::DENIED, 70);
                    }
                    
                    // Log the denied access
                    g_logger->warn(TAG, "Access denied for UID: %s", 
                                  RFIDReader::uidToString(cardData.uid).c_str());
                }
            }
            
            lastScanTime = currentTime;
        }
        
        // Small delay to prevent busy waiting
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief Servo control task
 * Controls door opening/closing based on commands from RFID task
 */
void servoTask(void* pvParameters) {
    ESP_LOGI(TAG, "Servo task started");
    
    ServoController::DoorPosition doorCmd;
    uint32_t doorOpenTime = 0;
    bool doorIsOpen = false;
    
    while (true) {
        // Wait for servo command
        if (xQueueReceive(g_servoQueue, &doorCmd, portMAX_DELAY) == pdTRUE) {
            
            switch (doorCmd) {
                case ServoController::DoorPosition::OPEN:
                    if (!doorIsOpen) {
                        ESP_LOGI(TAG, "Opening door");
                        esp_err_t ret = g_servoController->openDoor();
                        
                        if (ret == ESP_OK) {
                            doorIsOpen = true;
                            doorOpenTime = esp_timer_get_time() / 1000; // Convert to milliseconds
                            ESP_LOGI(TAG, "Door opened successfully");
                            
                            // Play door open sound
                            if (g_buzzerController) {
                                g_buzzerController->playSound(BuzzerController::SoundType::DOOR_OPEN, 50);
                            }
                            
                            // Log door action
                            g_logger->info(TAG, "Door opened");
                        } else {
                            ESP_LOGE(TAG, "Failed to open door");
                            
                            // Play error sound
                            if (g_buzzerController) {
                                g_buzzerController->playSound(BuzzerController::SoundType::ERROR, 80);
                            }
                            
                            g_logger->error(TAG, "Failed to open door: %s", esp_err_to_name(ret));
                        }
                    } else {
                        ESP_LOGI(TAG, "Door already open");
                    }
                    break;
                    
                case ServoController::DoorPosition::CLOSED:
                    if (doorIsOpen) {
                        ESP_LOGI(TAG, "Closing door");
                        esp_err_t ret = g_servoController->closeDoor();
                        
                        if (ret == ESP_OK) {
                            doorIsOpen = false;
                            ESP_LOGI(TAG, "Door closed successfully");
                            
                            // Play door close sound
                            if (g_buzzerController) {
                                g_buzzerController->playSound(BuzzerController::SoundType::DOOR_CLOSE, 50);
                            }
                            
                            // Log door action
                            g_logger->info(TAG, "Door closed");
                        } else {
                            ESP_LOGE(TAG, "Failed to close door");
                            
                            // Play error sound
                            if (g_buzzerController) {
                                g_buzzerController->playSound(BuzzerController::SoundType::ERROR, 80);
                            }
                            
                            g_logger->error(TAG, "Failed to close door: %s", esp_err_to_name(ret));
                        }
                    } else {
                        ESP_LOGI(TAG, "Door already closed");
                    }
                    break;
                    
                default:
                    ESP_LOGW(TAG, "Unknown door command");
                    break;
            }
        }
        
        // Auto-close door after specified time
        if (doorIsOpen) {
            uint32_t currentTime = esp_timer_get_time() / 1000;
            if (currentTime - doorOpenTime >= DOOR_OPEN_TIME_MS) {
                ESP_LOGI(TAG, "Auto-closing door after %lu ms", DOOR_OPEN_TIME_MS);
                
                esp_err_t ret = g_servoController->closeDoor();
                
                if (ret == ESP_OK) {
                    doorIsOpen = false;
                    ESP_LOGI(TAG, "Door auto-closed successfully");
                    g_logger->info(TAG, "Door auto-closed after timeout");
                } else {
                    ESP_LOGE(TAG, "Failed to auto-close door");
                    g_logger->error(TAG, "Failed to auto-close door: %s", esp_err_to_name(ret));
                }
            }
        }
        
        // Small delay
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

/**
 * @brief Logger task
 * Processes log messages from the queue and sends them to UART
 */
void loggerTask(void* pvParameters) {
    ESP_LOGI(TAG, "Logger task started");
    
    Logger::LogMessage logMsg;

    while (true) {
        // Wait for log message
        if (xQueueReceive(g_logQueue, &logMsg, portMAX_DELAY) == pdTRUE) {
            // Process the log message
            g_logger->log(logMsg.level, logMsg.tag.c_str(), "%s", logMsg.message.c_str());
        }
    }
}

/**
 * @brief WiFi Logger task
 * Manages WiFi connection and forwards logs to remote server
 */
void wifiLoggerTask(void* pvParameters) {
    ESP_LOGI(TAG, "WiFi Logger task started");

    while (true) {
        // Check WiFi connection status
        if (g_wifiLogger && g_wifiLogger->isEnabled()) {
            if (!g_wifiLogger->isConnected()) {
                ESP_LOGW(TAG, "WiFi not connected, attempting reconnection...");
                
                // Try to restart WiFi logger
                g_wifiLogger->stop();
                vTaskDelay(pdMS_TO_TICKS(5000)); // Wait 5 seconds
                g_wifiLogger->start();
            } else {
                // Send periodic status message
                g_logger->debug(TAG, "WiFi connected, RSSI: %ld dBm, IP: %s", 
                               g_wifiLogger->getRSSI(), 
                               g_wifiLogger->getIPAddress().c_str());
            }
        }
        
        // Check every 30 seconds
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/**
 * @brief Initialize all system components
 */
esp_err_t initializeSystem() {
    ESP_LOGI(TAG, "Initializing system components");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Create queues
    g_rfidQueue = xQueueCreate(RFID_QUEUE_SIZE, sizeof(RFIDReader::CardData));
    if (!g_rfidQueue) {
        ESP_LOGE(TAG, "Failed to create RFID queue");
        return ESP_FAIL;
    }
    
    g_servoQueue = xQueueCreate(SERVO_QUEUE_SIZE, sizeof(ServoController::DoorPosition));
    if (!g_servoQueue) {
        ESP_LOGE(TAG, "Failed to create servo queue");
        return ESP_FAIL;
    }
    
    g_logQueue = xQueueCreate(LOG_QUEUE_SIZE, sizeof(Logger::LogMessage));
    if (!g_logQueue) {
        ESP_LOGE(TAG, "Failed to create log queue");
        return ESP_FAIL;
    }
    
    // Create event group
    g_eventGroup = xEventGroupCreate();
    if (!g_eventGroup) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }
    
    // Initialize Logger
    g_logger = std::make_shared<Logger>(UART_NUM_0, GPIO_NUM_1, GPIO_NUM_3, 115200);
    ret = g_logger->init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize logger: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = g_logger->start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start logger: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize RFID Reader
    g_rfidReader = std::make_shared<RFIDReader>(
        SPI2_HOST,           // SPI host
        GPIO_NUM_23,         // MOSI
        GPIO_NUM_19,         // MISO
        GPIO_NUM_18,         // CLK
        GPIO_NUM_5,          // CS
        GPIO_NUM_21          // RST
    );
    
    ret = g_rfidReader->init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RFID reader: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Add authorized UIDs
    for (const auto& uid : AUTHORIZED_UIDS) {
        g_rfidReader->addAuthorizedUID(uid);
    }
    
    // Test RC522 basic functionality
    ESP_LOGI(TAG, "Testing RC522 basic functionality...");
    RFIDReader::CardData testCard;
    esp_err_t testRet = g_rfidReader->readCard(testCard);
    ESP_LOGI(TAG, "RC522 test result: %s", esp_err_to_name(testRet));
    
    // Run GPIO connection test first
    ESP_LOGI(TAG, "Starting GPIO connection test...");
    esp_err_t gpioTestRet = g_rfidReader->testGPIOConnections();
    ESP_LOGI(TAG, "GPIO connection test result: %s", esp_err_to_name(gpioTestRet));
    
    // Run manual card detection test
    ESP_LOGI(TAG, "Starting manual card detection test...");
    ESP_LOGI(TAG, "*** PLACE AN RFID CARD NEAR THE READER NOW! ***");
    vTaskDelay(pdMS_TO_TICKS(3000)); // Give user 3 seconds to place card
    esp_err_t manualTestRet = g_rfidReader->testCardDetection();
    ESP_LOGI(TAG, "Manual card detection test result: %s", esp_err_to_name(manualTestRet));
    
    // Initialize Servo Controller
    g_servoController = std::make_shared<ServoController>(
        GPIO_NUM_2,          // Servo control pin
        LEDC_CHANNEL_0,      // LEDC channel
        LEDC_TIMER_0,        // LEDC timer
        50                   // 50Hz frequency
    );
    
    ret = g_servoController->init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize servo controller: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize Buzzer Controller
    g_buzzerController = std::make_shared<BuzzerController>(
        GPIO_NUM_4,          // Buzzer control pin
        LEDC_CHANNEL_1,      // LEDC channel
        LEDC_TIMER_1         // LEDC timer
    );
    
    ret = g_buzzerController->init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize buzzer controller: %s", esp_err_to_name(ret));
        // Buzzer is optional, continue without it
        g_buzzerController.reset();
    } else {
        // Play startup sound
        g_buzzerController->playSound(BuzzerController::SoundType::STARTUP, 60);
    }
    
    // WiFi Logger disabled for testing
    g_wifiLogger.reset();
    ESP_LOGI(TAG, "WiFi Logger disabled for testing");
    
    ESP_LOGI(TAG, "System initialization completed successfully");
    return ESP_OK;
}

/**
 * @brief Create all FreeRTOS tasks
 */
esp_err_t createTasks() {
    ESP_LOGI(TAG, "Creating FreeRTOS tasks");
    
    // Create RFID task
    BaseType_t ret = xTaskCreate(rfidTask, 
                                RFID_TASK_NAME, 
                                RFID_TASK_STACK_SIZE, 
                                nullptr, 
                                RFID_TASK_PRIORITY, 
                                &g_rfidTaskHandle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create RFID task");
        return ESP_FAIL;
    }
    
    // Create Servo task
    ret = xTaskCreate(servoTask, 
                     SERVO_TASK_NAME, 
                     SERVO_TASK_STACK_SIZE, 
                     nullptr, 
                     SERVO_TASK_PRIORITY, 
                     &g_servoTaskHandle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create servo task");
        return ESP_FAIL;
    }
    
    // Create Logger task
    ret = xTaskCreate(loggerTask, 
                     LOGGER_TASK_NAME, 
                     LOGGER_TASK_STACK_SIZE, 
                     nullptr, 
                     LOGGER_TASK_PRIORITY, 
                     &g_loggerTaskHandle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create logger task");
        return ESP_FAIL;
    }
    
    // Create WiFi Logger task
    ret = xTaskCreate(wifiLoggerTask, 
                     WIFI_LOGGER_TASK_NAME, 
                     WIFI_LOGGER_TASK_STACK_SIZE, 
                     nullptr, 
                     WIFI_LOGGER_TASK_PRIORITY, 
                     &g_wifiLoggerTaskHandle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create WiFi logger task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "All tasks created successfully");
    return ESP_OK;
}

/**
 * @brief Print system information
 */
void printSystemInfo() {
    ESP_LOGI(TAG, "=== RFID Access Control System ===");
    ESP_LOGI(TAG, "ESP32 Chip: %s", esp_get_idf_version());
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Min free heap: %lu bytes", esp_get_minimum_free_heap_size());
    ESP_LOGI(TAG, "CPU frequency: 240 MHz (default)");
    ESP_LOGI(TAG, "Authorized UIDs configured: %zu", AUTHORIZED_UIDS.size());
    
    if (g_wifiLogger && g_wifiLogger->isConnected()) {
        ESP_LOGI(TAG, "WiFi connected: %s, RSSI: %ld dBm", 
                 g_wifiLogger->getIPAddress().c_str(), g_wifiLogger->getRSSI());
    } else {
        ESP_LOGI(TAG, "WiFi: Not connected");
    }
    
    ESP_LOGI(TAG, "System ready for operation");
    ESP_LOGI(TAG, "================================");
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "Starting RFID Access Control System");
    
    // Initialize system components
    esp_err_t ret = initializeSystem();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "System initialization failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Create FreeRTOS tasks
    ret = createTasks();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Task creation failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Print system information
    printSystemInfo();
    
    // Log system startup
    g_logger->info(TAG, "RFID Access Control System started successfully");
    g_logger->info(TAG, "Ready to scan for RFID cards");
    
    // Main loop - monitor system health
    while (true) {
        // Check system health periodically
        uint32_t freeHeap = esp_get_free_heap_size();
        if (freeHeap < 10000) { // Less than 10KB free
            ESP_LOGW(TAG, "Low memory warning: %lu bytes free", freeHeap);
            g_logger->warn(TAG, "Low memory warning: %lu bytes free", freeHeap);
        }
        
        // Print status every 60 seconds
        static uint32_t lastStatusTime = 0;
        uint32_t currentTime = esp_timer_get_time() / 1000000; // Convert to seconds
        
        if (currentTime - lastStatusTime >= 60) {
            ESP_LOGI(TAG, "System status - Free heap: %lu bytes", freeHeap);
            g_logger->info(TAG, "System status - Free heap: %lu bytes", freeHeap);
            lastStatusTime = currentTime;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds
    }
}