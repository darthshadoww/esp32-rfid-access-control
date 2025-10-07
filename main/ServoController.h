#ifndef SERVO_CONTROLLER_H
#define SERVO_CONTROLLER_H

#include "esp_err.h"
#include "esp_timer.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <cstdint>

/**
 * @brief ServoController class for SG90 servo motor
 * 
 * This class controls an SG90 servo motor using ESP32's LEDC PWM functionality.
 * It provides smooth movement between positions and supports different door states.
 */
class ServoController {
public:
    /**
     * @brief Door position enumeration
     */
    enum class DoorPosition {
        CLOSED = 0,    // Door closed (0 degrees)
        OPEN = 1,      // Door open (90 degrees)
        UNKNOWN = -1   // Unknown position
    };

    /**
     * @brief Constructor
     * @param gpio_pin GPIO pin connected to servo control signal
     * @param ledc_channel LEDC channel to use for PWM generation
     * @param ledc_timer LEDC timer to use
     * @param frequency PWM frequency in Hz (default: 50Hz for servo)
     */
    ServoController(gpio_num_t gpio_pin, 
                   ledc_channel_t ledc_channel, 
                   ledc_timer_t ledc_timer,
                   uint32_t frequency = 50);

    /**
     * @brief Destructor
     */
    ~ServoController();

    /**
     * @brief Initialize the servo controller
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t init();

    /**
     * @brief Set servo position by angle
     * @param angle Angle in degrees (0-180)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t setAngle(uint32_t angle);

    /**
     * @brief Set door position
     * @param position Door position to set
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t setDoorPosition(DoorPosition position);

    /**
     * @brief Open the door
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t openDoor();

    /**
     * @brief Close the door
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t closeDoor();

    /**
     * @brief Get current door position
     * @return Current door position
     */
    DoorPosition getCurrentPosition() const { return currentPosition_; }

    /**
     * @brief Get current angle
     * @return Current angle in degrees
     */
    uint32_t getCurrentAngle() const { return currentAngle_; }

    /**
     * @brief Check if door is open
     * @return true if door is open, false otherwise
     */
    bool isDoorOpen() const { return currentPosition_ == DoorPosition::OPEN; }

    /**
     * @brief Check if door is closed
     * @return true if door is closed, false otherwise
     */
    bool isDoorClosed() const { return currentPosition_ == DoorPosition::CLOSED; }

    /**
     * @brief Smoothly move to target angle
     * @param target_angle Target angle in degrees
     * @param duration_ms Movement duration in milliseconds
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t smoothMove(uint32_t target_angle, uint32_t duration_ms = 1000);

    /**
     * @brief Set PWM duty cycle directly
     * @param duty Duty cycle (0-100%)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t setDutyCycle(float duty);

    /**
     * @brief Disable servo (stop PWM)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t disable();

    /**
     * @brief Enable servo (start PWM)
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t enable();

private:
    // Hardware configuration
    gpio_num_t gpioPin_;
    ledc_channel_t ledcChannel_;
    ledc_timer_t ledcTimer_;
    uint32_t frequency_;
    
    // Current state
    uint32_t currentAngle_;
    DoorPosition currentPosition_;
    bool isEnabled_;
    
    // Servo timing constants (in microseconds)
    static constexpr uint32_t SERVO_MIN_PULSE = 500;   // 0.5ms (0 degrees)
    static constexpr uint32_t SERVO_MAX_PULSE = 2500;  // 2.5ms (180 degrees)
    static constexpr uint32_t SERVO_PULSE_RANGE = SERVO_MAX_PULSE - SERVO_MIN_PULSE;
    static constexpr uint32_t SERVO_ANGLE_RANGE = 180; // 180 degrees
    
    // Door positions in degrees
    static constexpr uint32_t DOOR_CLOSED_ANGLE = 0;
    static constexpr uint32_t DOOR_OPEN_ANGLE = 90;
    
    /**
     * @brief Calculate duty cycle for given angle
     * @param angle Angle in degrees
     * @return Duty cycle percentage
     */
    float calculateDutyCycle(uint32_t angle) const;

    /**
     * @brief Convert angle to pulse width in microseconds
     * @param angle Angle in degrees
     * @return Pulse width in microseconds
     */
    uint32_t angleToPulseWidth(uint32_t angle) const;

    /**
     * @brief Update current position based on angle
     * @param angle Current angle
     */
    void updateCurrentPosition(uint32_t angle);

    /**
     * @brief Initialize LEDC timer
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t initLEDCTimer();

    /**
     * @brief Initialize LEDC channel
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t initLEDCChannel();
};

#endif // SERVO_CONTROLLER_H