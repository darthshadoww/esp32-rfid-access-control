#include "ServoController.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char* TAG = "SERVO_CONTROLLER";

ServoController::ServoController(gpio_num_t gpio_pin, 
                               ledc_channel_t ledc_channel, 
                               ledc_timer_t ledc_timer,
                               uint32_t frequency)
    : gpioPin_(gpio_pin)
    , ledcChannel_(ledc_channel)
    , ledcTimer_(ledc_timer)
    , frequency_(frequency)
    , currentAngle_(DOOR_CLOSED_ANGLE)
    , currentPosition_(DoorPosition::CLOSED)
    , isEnabled_(false) {
    ESP_LOGI(TAG, "ServoController constructor called");
}

ServoController::~ServoController() {
    disable();
    ESP_LOGI(TAG, "ServoController destructor called");
}

esp_err_t ServoController::init() {
    ESP_LOGI(TAG, "Initializing ServoController");
    
    // Initialize LEDC timer
    esp_err_t ret = initLEDCTimer();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Initialize LEDC channel
    ret = initLEDCChannel();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set initial position to closed
    ret = setAngle(DOOR_CLOSED_ANGLE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set initial angle: %s", esp_err_to_name(ret));
        return ret;
    }
    
    isEnabled_ = true;
    ESP_LOGI(TAG, "ServoController initialized successfully");
    return ESP_OK;
}

esp_err_t ServoController::setAngle(uint32_t angle) {
    if (angle > SERVO_ANGLE_RANGE) {
        ESP_LOGW(TAG, "Angle %lu exceeds maximum range, clamping to %u", angle, SERVO_ANGLE_RANGE);
        angle = SERVO_ANGLE_RANGE;
    }
    
    float duty = calculateDutyCycle(angle);
    esp_err_t ret = setDutyCycle(duty);
    
    if (ret == ESP_OK) {
        currentAngle_ = angle;
        updateCurrentPosition(angle);
        ESP_LOGI(TAG, "Set servo angle to %lu degrees", angle);
    }
    
    return ret;
}

esp_err_t ServoController::setDoorPosition(DoorPosition position) {
    uint32_t angle;
    
    switch (position) {
        case DoorPosition::CLOSED:
            angle = DOOR_CLOSED_ANGLE;
            break;
        case DoorPosition::OPEN:
            angle = DOOR_OPEN_ANGLE;
            break;
        default:
            ESP_LOGE(TAG, "Invalid door position");
            return ESP_ERR_INVALID_ARG;
    }
    
    esp_err_t ret = setAngle(angle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Set door position to %s", 
                (position == DoorPosition::OPEN) ? "OPEN" : "CLOSED");
    }
    
    return ret;
}

esp_err_t ServoController::openDoor() {
    ESP_LOGI(TAG, "Opening door");
    return setDoorPosition(DoorPosition::OPEN);
}

esp_err_t ServoController::closeDoor() {
    ESP_LOGI(TAG, "Closing door");
    return setDoorPosition(DoorPosition::CLOSED);
}

esp_err_t ServoController::smoothMove(uint32_t target_angle, uint32_t duration_ms) {
    if (target_angle > SERVO_ANGLE_RANGE) {
        ESP_LOGW(TAG, "Target angle %lu exceeds maximum range, clamping to %u", 
                target_angle, SERVO_ANGLE_RANGE);
        target_angle = SERVO_ANGLE_RANGE;
    }
    
    uint32_t start_angle = currentAngle_;
    int32_t angle_diff = static_cast<int32_t>(target_angle) - static_cast<int32_t>(start_angle);
    
    if (angle_diff == 0) {
        ESP_LOGI(TAG, "Already at target angle %lu", target_angle);
        return ESP_OK;
    }
    
    // Calculate step parameters
    const uint32_t step_count = 50; // Number of steps for smooth movement
    const uint32_t step_delay = duration_ms / step_count;
    const float angle_step = static_cast<float>(angle_diff) / step_count;
    
    ESP_LOGI(TAG, "Smooth moving from %lu to %lu degrees over %lu ms", 
            start_angle, target_angle, duration_ms);
    
    for (uint32_t i = 0; i <= step_count; ++i) {
        float current_angle_float = start_angle + (angle_step * i);
        uint32_t current_angle = static_cast<uint32_t>(current_angle_float + 0.5f); // Round
        
        esp_err_t ret = setAngle(current_angle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set angle during smooth move: %s", esp_err_to_name(ret));
            return ret;
        }
        
        if (i < step_count) { // Don't delay on the last step
            vTaskDelay(pdMS_TO_TICKS(step_delay));
        }
    }
    
    ESP_LOGI(TAG, "Smooth move completed");
    return ESP_OK;
}

esp_err_t ServoController::setDutyCycle(float duty) {
    if (duty < 0.0f || duty > 100.0f) {
        ESP_LOGE(TAG, "Duty cycle %f is out of range [0-100]", duty);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!isEnabled_) {
        ESP_LOGW(TAG, "Servo is disabled, enabling first");
        enable();
    }
    
    uint32_t duty_value = static_cast<uint32_t>((duty / 100.0f) * 8191); // 13-bit resolution
    esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, ledcChannel_, duty_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set duty: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, ledcChannel_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update duty: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

esp_err_t ServoController::disable() {
    if (!isEnabled_) {
        return ESP_OK; // Already disabled
    }
    
    esp_err_t ret = ledc_stop(LEDC_LOW_SPEED_MODE, ledcChannel_, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    isEnabled_ = false;
    ESP_LOGI(TAG, "Servo disabled");
    return ESP_OK;
}

esp_err_t ServoController::enable() {
    if (isEnabled_) {
        return ESP_OK; // Already enabled
    }
    
    esp_err_t ret = ledc_set_duty_and_update(LEDC_LOW_SPEED_MODE, ledcChannel_, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    isEnabled_ = true;
    ESP_LOGI(TAG, "Servo enabled");
    return ESP_OK;
}

float ServoController::calculateDutyCycle(uint32_t angle) const {
    // Convert angle to pulse width in microseconds
    uint32_t pulse_width = angleToPulseWidth(angle);
    
    // Convert pulse width to duty cycle percentage
    // Duty cycle = (pulse_width_us / period_us) * 100
    uint32_t period_us = 1000000 / frequency_; // Period in microseconds
    float duty = (static_cast<float>(pulse_width) / static_cast<float>(period_us)) * 100.0f;
    
    return duty;
}

uint32_t ServoController::angleToPulseWidth(uint32_t angle) const {
    // Linear interpolation between min and max pulse widths
    uint32_t pulse_width = SERVO_MIN_PULSE + 
                          (angle * SERVO_PULSE_RANGE) / SERVO_ANGLE_RANGE;
    
    return pulse_width;
}

void ServoController::updateCurrentPosition(uint32_t angle) {
    // Determine position based on angle
    if (angle <= DOOR_CLOSED_ANGLE + 5) { // 5 degree tolerance
        currentPosition_ = DoorPosition::CLOSED;
    } else if (angle >= DOOR_OPEN_ANGLE - 5) { // 5 degree tolerance
        currentPosition_ = DoorPosition::OPEN;
    } else {
        currentPosition_ = DoorPosition::UNKNOWN;
    }
}

esp_err_t ServoController::initLEDCTimer() {
    ledc_timer_config_t timer_config = {};
    timer_config.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_config.timer_num = ledcTimer_;
    timer_config.duty_resolution = LEDC_TIMER_13_BIT; // 13-bit resolution
    timer_config.freq_hz = frequency_;
    timer_config.clk_cfg = LEDC_AUTO_CLK;
    
    esp_err_t ret = ledc_timer_config(&timer_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "LEDC timer configured with frequency %lu Hz", frequency_);
    return ESP_OK;
}

esp_err_t ServoController::initLEDCChannel() {
    ledc_channel_config_t channel_config = {};
    channel_config.speed_mode = LEDC_LOW_SPEED_MODE;
    channel_config.channel = ledcChannel_;
    channel_config.timer_sel = ledcTimer_;
    channel_config.intr_type = LEDC_INTR_DISABLE;
    channel_config.gpio_num = gpioPin_;
    channel_config.duty = 0; // Start with 0 duty
    channel_config.hpoint = 0;
    
    esp_err_t ret = ledc_channel_config(&channel_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC channel: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "LEDC channel configured on GPIO %d", gpioPin_);
    return ESP_OK;
}