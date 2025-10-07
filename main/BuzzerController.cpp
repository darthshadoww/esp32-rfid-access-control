#include "BuzzerController.h"
#include "esp_log.h"
#include "freertos/task.h"

static const char* TAG = "BUZZER_CONTROLLER";

// Sound pattern definitions
const uint16_t BuzzerController::SUCCESS_FREQUENCIES[] = {800};
const uint16_t BuzzerController::SUCCESS_DURATIONS[] = {200};

const uint16_t BuzzerController::DENIED_FREQUENCIES[] = {400, 400};
const uint16_t BuzzerController::DENIED_DURATIONS[] = {150, 150};

const uint16_t BuzzerController::ERROR_FREQUENCIES[] = {300};
const uint16_t BuzzerController::ERROR_DURATIONS[] = {1000};

const uint16_t BuzzerController::STARTUP_FREQUENCIES[] = {523, 659, 784};  // C-E-G chord
const uint16_t BuzzerController::STARTUP_DURATIONS[] = {200, 200, 400};

const uint16_t BuzzerController::DOOR_OPEN_FREQUENCIES[] = {600, 700, 800};
const uint16_t BuzzerController::DOOR_OPEN_DURATIONS[] = {100, 100, 200};

const uint16_t BuzzerController::DOOR_CLOSE_FREQUENCIES[] = {800, 700, 600};
const uint16_t BuzzerController::DOOR_CLOSE_DURATIONS[] = {100, 100, 200};

const uint16_t BuzzerController::WARNING_FREQUENCIES[] = {1000, 0, 1000, 0, 1000};
const uint16_t BuzzerController::WARNING_DURATIONS[] = {100, 100, 100, 100, 100};

BuzzerController::BuzzerController(gpio_num_t gpio_pin, 
                                   ledc_channel_t ledc_channel, 
                                   ledc_timer_t ledc_timer)
    : gpioPin_(gpio_pin)
    , ledcChannel_(ledc_channel)
    , ledcTimer_(ledc_timer)
    , isEnabled_(true)
    , isPlaying_(false)
    , defaultVolume_(50)
    , soundTimer_(nullptr)
    , patternTaskHandle_(nullptr) {
    ESP_LOGI(TAG, "BuzzerController constructor called");
}

BuzzerController::~BuzzerController() {
    stop();
    if (soundTimer_) {
        esp_timer_delete(soundTimer_);
    }
    if (patternTaskHandle_) {
        vTaskDelete(patternTaskHandle_);
    }
    ESP_LOGI(TAG, "BuzzerController destructor called");
}

esp_err_t BuzzerController::init() {
    ESP_LOGI(TAG, "Initializing BuzzerController");
    
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
    
    // Create sound timer
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = soundTimerCallback;
    timer_args.arg = this;
    timer_args.name = "sound_timer";
    timer_args.skip_unhandled_events = false;
    timer_args.dispatch_method = ESP_TIMER_TASK;
    
    ret = esp_timer_create(&timer_args, &soundTimer_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create sound timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Create pattern task
    BaseType_t taskRet = xTaskCreate(patternTask, 
                                    PATTERN_TASK_NAME, 
                                    PATTERN_TASK_STACK_SIZE, 
                                    this, 
                                    PATTERN_TASK_PRIORITY, 
                                    &patternTaskHandle_);
    if (taskRet != pdPASS) {
        ESP_LOGE(TAG, "Failed to create pattern task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "BuzzerController initialized successfully");
    return ESP_OK;
}

esp_err_t BuzzerController::playSound(SoundType sound_type, uint8_t volume) {
    if (!isEnabled_) {
        return ESP_OK;
    }
    
    const uint16_t* frequencies = nullptr;
    const uint16_t* durations = nullptr;
    uint8_t count = 0;
    
    switch (sound_type) {
        case SoundType::SUCCESS:
            frequencies = SUCCESS_FREQUENCIES;
            durations = SUCCESS_DURATIONS;
            count = sizeof(SUCCESS_FREQUENCIES) / sizeof(SUCCESS_FREQUENCIES[0]);
            break;
            
        case SoundType::DENIED:
            frequencies = DENIED_FREQUENCIES;
            durations = DENIED_DURATIONS;
            count = sizeof(DENIED_FREQUENCIES) / sizeof(DENIED_FREQUENCIES[0]);
            break;
            
        case SoundType::ERROR:
            frequencies = ERROR_FREQUENCIES;
            durations = ERROR_DURATIONS;
            count = sizeof(ERROR_FREQUENCIES) / sizeof(ERROR_FREQUENCIES[0]);
            break;
            
        case SoundType::STARTUP:
            frequencies = STARTUP_FREQUENCIES;
            durations = STARTUP_DURATIONS;
            count = sizeof(STARTUP_FREQUENCIES) / sizeof(STARTUP_FREQUENCIES[0]);
            break;
            
        case SoundType::DOOR_OPEN:
            frequencies = DOOR_OPEN_FREQUENCIES;
            durations = DOOR_OPEN_DURATIONS;
            count = sizeof(DOOR_OPEN_FREQUENCIES) / sizeof(DOOR_OPEN_FREQUENCIES[0]);
            break;
            
        case SoundType::DOOR_CLOSE:
            frequencies = DOOR_CLOSE_FREQUENCIES;
            durations = DOOR_CLOSE_DURATIONS;
            count = sizeof(DOOR_CLOSE_FREQUENCIES) / sizeof(DOOR_CLOSE_FREQUENCIES[0]);
            break;
            
        case SoundType::WARNING:
            frequencies = WARNING_FREQUENCIES;
            durations = WARNING_DURATIONS;
            count = sizeof(WARNING_FREQUENCIES) / sizeof(WARNING_FREQUENCIES[0]);
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown sound type");
            return ESP_ERR_INVALID_ARG;
    }
    
    return playCustomPattern(frequencies, durations, count, volume);
}

esp_err_t BuzzerController::playCustomPattern(const uint16_t* frequencies, 
                                             const uint16_t* durations, 
                                             uint8_t count, 
                                             uint8_t volume) {
    if (!isEnabled_ || !frequencies || !durations || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Stop any currently playing sound
    stop();
    
    // Process the pattern
    processPattern(frequencies, durations, count, volume);
    
    return ESP_OK;
}

esp_err_t BuzzerController::playTone(uint16_t frequency, uint16_t duration, uint8_t volume) {
    if (!isEnabled_) {
        return ESP_OK;
    }
    
    // Stop any currently playing sound
    stop();
    
    if (frequency == 0) {
        return ESP_OK;
    }
    
    // Set PWM frequency and duty
    uint32_t duty = volumeToDuty(volume);
    esp_err_t ret = setPWM(frequency, duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set PWM: %s", esp_err_to_name(ret));
        return ret;
    }
    
    isPlaying_ = true;
    
    if (duration > 0) {
        // Set timer to stop the sound after duration
        esp_timer_start_once(soundTimer_, duration * 1000); // Convert ms to microseconds
    }
    
    ESP_LOGI(TAG, "Playing tone: %d Hz for %d ms at volume %d", frequency, duration, volume);
    return ESP_OK;
}

esp_err_t BuzzerController::stop() {
    if (!isPlaying_) {
        return ESP_OK;
    }
    
    // Stop PWM
    esp_err_t ret = ledc_stop(LEDC_LOW_SPEED_MODE, ledcChannel_, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop LEDC: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Stop timer if running
    if (soundTimer_) {
        esp_timer_stop(soundTimer_);
    }
    
    isPlaying_ = false;
    ESP_LOGI(TAG, "Buzzer stopped");
    return ESP_OK;
}

void BuzzerController::patternTask(void* pvParameters) {
    (void)pvParameters; // Suppress unused parameter warning
    
    while (true) {
        // Wait for pattern commands
        // This task will be used for complex pattern processing
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    vTaskDelete(nullptr);
}

esp_err_t BuzzerController::initLEDCTimer() {
    ledc_timer_config_t timer_config = {};
    timer_config.speed_mode = LEDC_LOW_SPEED_MODE;
    timer_config.timer_num = ledcTimer_;
    timer_config.duty_resolution = LEDC_TIMER_8_BIT; // 8-bit resolution for buzzer
    timer_config.freq_hz = 1000; // Base frequency, will be changed per tone
    timer_config.clk_cfg = LEDC_AUTO_CLK;
    
    esp_err_t ret = ledc_timer_config(&timer_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure LEDC timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "LEDC timer configured");
    return ESP_OK;
}

esp_err_t BuzzerController::initLEDCChannel() {
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

uint32_t BuzzerController::volumeToDuty(uint8_t volume) const {
    if (volume > 100) {
        volume = 100;
    }
    
    // Convert volume percentage to duty cycle (0-255 for 8-bit)
    return (volume * 255) / 100;
}

esp_err_t BuzzerController::setPWM(uint16_t frequency, uint32_t duty) {
    // Update timer frequency
    esp_err_t ret = ledc_set_freq(LEDC_LOW_SPEED_MODE, ledcTimer_, frequency);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set frequency: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set duty cycle
    ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, ledcChannel_, duty);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set duty: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Update duty
    ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, ledcChannel_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update duty: %s", esp_err_to_name(ret));
        return ret;
    }
    
    return ESP_OK;
}

void BuzzerController::processPattern(const uint16_t* frequencies, 
                                     const uint16_t* durations, 
                                     uint8_t count, 
                                     uint8_t volume) {
    isPlaying_ = true;
    
    // Process each frequency/duration pair
    for (uint8_t i = 0; i < count; ++i) {
        uint16_t freq = frequencies[i];
        uint16_t duration = durations[i];
        
        if (freq == 0) {
            // Silence
            stop();
            vTaskDelay(pdMS_TO_TICKS(duration));
        } else {
            // Play tone
            playTone(freq, 0, volume); // Continuous tone
            vTaskDelay(pdMS_TO_TICKS(duration));
        }
    }
    
    // Stop after pattern completion
    stop();
}

void BuzzerController::soundTimerCallback(void* arg) {
    BuzzerController* buzzer = static_cast<BuzzerController*>(arg);
    if (buzzer) {
        buzzer->stop();
    }
}
