#ifndef BUZZER_CONTROLLER_H
#define BUZZER_CONTROLLER_H

#include "esp_err.h"
#include "esp_timer.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>

/**
 * @brief BuzzerController class for audio feedback
 * 
 * This class controls a piezo buzzer or speaker for providing audio feedback
 * during various system events like successful access, denied access, errors, etc.
 */
class BuzzerController {
public:
    /**
     * @brief Buzzer sound types
     */
    enum class SoundType {
        SUCCESS = 0,        // Short beep for successful access
        DENIED = 1,         // Two short beeps for denied access
        ERROR = 2,          // Long beep for errors
        STARTUP = 3,        // Startup sequence
        DOOR_OPEN = 4,      // Door opening sound
        DOOR_CLOSE = 5,     // Door closing sound
        WARNING = 6,        // Warning sound
        CUSTOM = 7          // Custom sound pattern
    };

    /**
     * @brief Constructor
     * @param gpio_pin GPIO pin connected to buzzer positive terminal
     * @param ledc_channel LEDC channel to use for PWM generation
     * @param ledc_timer LEDC timer to use
     */
    BuzzerController(gpio_num_t gpio_pin, 
                     ledc_channel_t ledc_channel, 
                     ledc_timer_t ledc_timer);

    /**
     * @brief Destructor
     */
    ~BuzzerController();

    /**
     * @brief Initialize the buzzer controller
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t init();

    /**
     * @brief Play a predefined sound
     * @param sound_type Type of sound to play
     * @param volume Volume level (0-100)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t playSound(SoundType sound_type, uint8_t volume = 50);

    /**
     * @brief Play a custom sound pattern
     * @param frequencies Array of frequencies in Hz
     * @param durations Array of durations in milliseconds
     * @param count Number of frequency/duration pairs
     * @param volume Volume level (0-100)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t playCustomPattern(const uint16_t* frequencies, 
                               const uint16_t* durations, 
                               uint8_t count, 
                               uint8_t volume = 50);

    /**
     * @brief Play a single tone
     * @param frequency Frequency in Hz (0 to stop)
     * @param duration Duration in milliseconds (0 for continuous)
     * @param volume Volume level (0-100)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t playTone(uint16_t frequency, uint16_t duration = 0, uint8_t volume = 50);

    /**
     * @brief Stop the buzzer
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t stop();

    /**
     * @brief Check if buzzer is currently playing
     * @return true if playing, false otherwise
     */
    bool isPlaying() const { return isPlaying_; }

    /**
     * @brief Set default volume
     * @param volume Volume level (0-100)
     */
    void setVolume(uint8_t volume) { defaultVolume_ = volume; }

    /**
     * @brief Get current volume
     * @return Current volume level
     */
    uint8_t getVolume() const { return defaultVolume_; }

    /**
     * @brief Enable or disable buzzer
     * @param enabled true to enable, false to disable
     */
    void setEnabled(bool enabled) { isEnabled_ = enabled; }

    /**
     * @brief Check if buzzer is enabled
     * @return true if enabled, false otherwise
     */
    bool isEnabled() const { return isEnabled_; }

private:
    // Hardware configuration
    gpio_num_t gpioPin_;
    ledc_channel_t ledcChannel_;
    ledc_timer_t ledcTimer_;
    
    // State variables
    bool isEnabled_;
    bool isPlaying_;
    uint8_t defaultVolume_;
    
    // Timer for duration-based sounds
    esp_timer_handle_t soundTimer_;
    
    // Task handle for pattern playing
    TaskHandle_t patternTaskHandle_;
    
    // Task names and parameters
    static constexpr const char* PATTERN_TASK_NAME = "buzzer_pattern_task";
    static constexpr uint32_t PATTERN_TASK_STACK_SIZE = 2048;
    static constexpr UBaseType_t PATTERN_TASK_PRIORITY = 1;
    
    // Sound definitions
    struct SoundDefinition {
        const uint16_t* frequencies;
        const uint16_t* durations;
        uint8_t count;
    };
    
    // Predefined sound patterns
    static const uint16_t SUCCESS_FREQUENCIES[];
    static const uint16_t SUCCESS_DURATIONS[];
    static const uint16_t DENIED_FREQUENCIES[];
    static const uint16_t DENIED_DURATIONS[];
    static const uint16_t ERROR_FREQUENCIES[];
    static const uint16_t ERROR_DURATIONS[];
    static const uint16_t STARTUP_FREQUENCIES[];
    static const uint16_t STARTUP_DURATIONS[];
    static const uint16_t DOOR_OPEN_FREQUENCIES[];
    static const uint16_t DOOR_OPEN_DURATIONS[];
    static const uint16_t DOOR_CLOSE_FREQUENCIES[];
    static const uint16_t DOOR_CLOSE_DURATIONS[];
    static const uint16_t WARNING_FREQUENCIES[];
    static const uint16_t WARNING_DURATIONS[];
    
    // Static task function
    static void patternTask(void* pvParameters);
    
    /**
     * @brief Initialize LEDC timer for buzzer
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t initLEDCTimer();
    
    /**
     * @brief Initialize LEDC channel for buzzer
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t initLEDCChannel();
    
    /**
     * @brief Calculate duty cycle from volume percentage
     * @param volume Volume percentage (0-100)
     * @return Duty cycle value
     */
    uint32_t volumeToDuty(uint8_t volume) const;
    
    /**
     * @brief Set PWM frequency and duty
     * @param frequency Frequency in Hz
     * @param duty Duty cycle value
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t setPWM(uint16_t frequency, uint32_t duty);
    
    /**
     * @brief Process sound pattern
     * @param frequencies Array of frequencies
     * @param durations Array of durations
     * @param count Number of elements
     * @param volume Volume level
     */
    void processPattern(const uint16_t* frequencies, 
                       const uint16_t* durations, 
                       uint8_t count, 
                       uint8_t volume);
    
    /**
     * @brief Timer callback for stopping sound
     * @param arg Timer handle
     */
    static void soundTimerCallback(void* arg);
};

#endif // BUZZER_CONTROLLER_H
