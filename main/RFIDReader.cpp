#include "RFIDReader.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>
#include <cstring>

static const char* TAG = "RFID_READER";

RFIDReader::RFIDReader(spi_host_device_t spi_host, 
                       gpio_num_t mosi_pin, 
                       gpio_num_t miso_pin, 
                       gpio_num_t clk_pin, 
                       gpio_num_t cs_pin, 
                       gpio_num_t rst_pin)
    : spiHost_(spi_host)
    , mosiPin_(mosi_pin)
    , misoPin_(miso_pin)
    , clkPin_(clk_pin)
    , csPin_(cs_pin)
    , rstPin_(rst_pin)
    , spiDevice_(nullptr) {
    ESP_LOGI(TAG, "RFIDReader constructor called");
}

RFIDReader::~RFIDReader() {
    if (spiDevice_) {
        spi_bus_remove_device(spiDevice_);
    }
    ESP_LOGI(TAG, "RFIDReader destructor called");
}

esp_err_t RFIDReader::init() {
    ESP_LOGI(TAG, "Initializing RFID Reader");
    
    // Configure SPI bus
    spi_bus_config_t bus_config = {};
    bus_config.mosi_io_num = mosiPin_;
    bus_config.miso_io_num = misoPin_;
    bus_config.sclk_io_num = clkPin_;
    bus_config.quadwp_io_num = -1;
    bus_config.quadhd_io_num = -1;
    bus_config.max_transfer_sz = 4096;
    
    esp_err_t ret = spi_bus_initialize(spiHost_, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure SPI device
    spi_device_interface_config_t dev_config = {};
    dev_config.clock_speed_hz = 1000000; // 1MHz
    dev_config.mode = 0;
    dev_config.spics_io_num = csPin_;
    dev_config.queue_size = 7;
    dev_config.pre_cb = nullptr;
    dev_config.post_cb = nullptr;
    
    ret = spi_bus_add_device(spiHost_, &dev_config, &spiDevice_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add SPI device: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Configure RST pin
    gpio_config_t rst_config = {};
    rst_config.intr_type = GPIO_INTR_DISABLE;
    rst_config.mode = GPIO_MODE_OUTPUT;
    rst_config.pin_bit_mask = (1ULL << rstPin_);
    rst_config.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rst_config.pull_up_en = GPIO_PULLUP_DISABLE;
    
    ret = gpio_config(&rst_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure RST pin: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Reset RC522
    gpio_set_level(rstPin_, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(rstPin_, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // Initialize RC522
    ret = initRC522();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize RC522: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "RFID Reader initialized successfully");
    return ESP_OK;
}

bool RFIDReader::isCardPresent() {
    // Simplified card detection - just try to read a card
    // This is more reliable than separate presence detection
    RFIDReader::CardData testCard;
    esp_err_t result = readCard(testCard);
    return (result == ESP_OK && testCard.isValid);
}

esp_err_t RFIDReader::readCard(CardData& cardData) {
    cardData.isValid = false;
    cardData.uid.clear();
    cardData.timestamp = esp_timer_get_time();
    
    // Add immediate debugging to see if this function is being called
    static int callCount = 0;
    callCount++;
    if (callCount % 50 == 1) { // Log every 50th call to avoid spam
        ESP_LOGI(TAG, "readCard called %d times - attempting card detection...", callCount);
    }
    
    // Step 1: Send REQA (Request Type A) with enhanced debugging
    clearInterrupts();
    writeRegister(RC522_REG_FIFO_LEVEL, 0x80); // Flush FIFO
    
    // Configure for ISO14443A Type A cards
    writeRegister(0x11, 0x3D); // RxMode: 106 kBd, ISO14443A
    writeRegister(0x12, 0x3D); // TxMode: 106 kBd, ISO14443A
    writeRegister(0x13, 0x00); // TxControl: default
    
    uint8_t reqCmd = 0x26; // REQA command
    writeRegister(RC522_REG_FIFO_DATA, reqCmd);
    writeRegister(RC522_REG_COMMAND, RC522_CMD_TRANSCEIVE);
    writeRegister(RC522_REG_BIT_FRAMING, 0x07); // Start sending from bit 7
    
    // Wait for completion with shorter timeout for faster scanning
    uint8_t status;
    int timeout = 100; // Reduced timeout for faster scanning
    do {
        if (readRegister(RC522_REG_COMIRQ, &status) != ESP_OK) {
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout--;
    } while (!(status & 0x01) && !(status & 0x30) && timeout > 0);
    
    if (timeout <= 0 || (status & 0x01)) {
        // No card detected - this is normal, don't log as error
        // But let's add some debugging every 100 calls
        if (callCount % 100 == 1) {
            ESP_LOGI(TAG, "No card response: timeout=%d, status=0x%02X", timeout, status);
        }
        return ESP_ERR_NOT_FOUND;
    }
    
    // Check for errors
    uint8_t errorReg;
    readRegister(RC522_REG_ERROR, &errorReg);
    if (errorReg & 0x13) {
        ESP_LOGD(TAG, "REQA error: 0x%02X", errorReg);
        return ESP_ERR_NOT_FOUND;
    }
    
    // Read ATQA response
    uint8_t fifoLevel;
    readRegister(RC522_REG_FIFO_LEVEL, &fifoLevel);
    if (fifoLevel < 2) {
        ESP_LOGD(TAG, "Invalid ATQA length: %d", fifoLevel);
        return ESP_ERR_NOT_FOUND;
    }
    
    uint8_t atqa[2];
    readFIFO(atqa, 2);
    ESP_LOGI(TAG, "Card detected! ATQA: 0x%02X 0x%02X", atqa[0], atqa[1]);
    
    // Step 2: Send Anti-collision (SELECT CL1)
    clearInterrupts();
    writeRegister(RC522_REG_FIFO_LEVEL, 0x80); // Flush FIFO
    
    uint8_t selCmd[] = {0x93, 0x20}; // SELECT CL1
    writeFIFO(selCmd, 2);
    writeRegister(RC522_REG_COMMAND, RC522_CMD_TRANSCEIVE);
    writeRegister(RC522_REG_BIT_FRAMING, 0x00); // Start from bit 0
    
    // Wait for completion
    timeout = 100;
    do {
        if (readRegister(RC522_REG_COMIRQ, &status) != ESP_OK) {
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
        timeout--;
    } while (!(status & 0x01) && !(status & 0x30) && timeout > 0);
    
    if (timeout <= 0 || (status & 0x01)) {
        ESP_LOGW(TAG, "No response to anti-collision");
        return ESP_ERR_NOT_FOUND;
    }
    
    // Read UID response
    readRegister(RC522_REG_FIFO_LEVEL, &fifoLevel);
    ESP_LOGI(TAG, "Anti-collision response length: %d", fifoLevel);
    
    if (fifoLevel < 4) {
        ESP_LOGW(TAG, "Invalid UID response length: %d", fifoLevel);
        return ESP_ERR_NOT_FOUND;
    }
    
    uint8_t uidBuffer[10]; // Larger buffer for safety
    readFIFO(uidBuffer, fifoLevel);
    
    // Log raw response for debugging
    ESP_LOGI(TAG, "Raw UID response (%d bytes):", fifoLevel);
    for (int i = 0; i < fifoLevel; i++) {
        ESP_LOGI(TAG, "  [%d]: 0x%02X", i, uidBuffer[i]);
    }
    
    // For most cards, UID is first 4 bytes, BCC is 5th byte
    if (fifoLevel >= 5) {
        // Verify BCC (Block Check Character)
        uint8_t bcc = uidBuffer[0] ^ uidBuffer[1] ^ uidBuffer[2] ^ uidBuffer[3];
        if (bcc != uidBuffer[4]) {
            ESP_LOGW(TAG, "UID BCC mismatch: calculated=0x%02X, received=0x%02X", bcc, uidBuffer[4]);
            // Continue anyway - some clone cards have BCC issues
        }
        
        // Extract UID (first 4 bytes)
        cardData.uid.assign(uidBuffer, uidBuffer + 4);
    } else {
        // Fallback: use whatever we got
        cardData.uid.assign(uidBuffer, uidBuffer + fifoLevel);
    }
    
    cardData.isValid = true;
    lastCardData_ = cardData;
    
    ESP_LOGI(TAG, "Card successfully read: UID = %s", uidToString(cardData.uid).c_str());
    
    clearInterrupts();
    return ESP_OK;
}

bool RFIDReader::isAuthorized(const std::vector<uint8_t>& uid) const {
    for (const auto& authorizedUID : authorizedUIDs_) {
        if (uid == authorizedUID) {
            return true;
        }
    }
    return false;
}

void RFIDReader::addAuthorizedUID(const std::vector<uint8_t>& uid) {
    authorizedUIDs_.push_back(uid);
    ESP_LOGI(TAG, "Added authorized UID: %s", uidToString(uid).c_str());
}

void RFIDReader::removeAuthorizedUID(const std::vector<uint8_t>& uid) {
    auto it = std::find(authorizedUIDs_.begin(), authorizedUIDs_.end(), uid);
    if (it != authorizedUIDs_.end()) {
        authorizedUIDs_.erase(it);
        ESP_LOGI(TAG, "Removed authorized UID: %s", uidToString(uid).c_str());
    }
}

void RFIDReader::clearAuthorizedUIDs() {
    authorizedUIDs_.clear();
    ESP_LOGI(TAG, "Cleared all authorized UIDs");
}

std::string RFIDReader::uidToString(const std::vector<uint8_t>& uid) {
    std::string result;
    char buffer[4];
    
    for (size_t i = 0; i < uid.size(); ++i) {
        if (i > 0) {
            result += ":";
        }
        snprintf(buffer, sizeof(buffer), "%02X", uid[i]);
        result += buffer;
    }
    
    return result;
}

esp_err_t RFIDReader::writeRegister(uint8_t addr, uint8_t data) {
    if (!spiDevice_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint8_t tx_data[2];
    tx_data[0] = ((addr << 1) & 0x7E); // Clear MSB for write (RC522 uses 0 for write)
    tx_data[1] = data;
    
    spi_transaction_t trans = {};
    trans.length = 16; // 8 bits address + 8 bits data
    trans.tx_buffer = tx_data;
    trans.rx_buffer = nullptr;
    
    esp_err_t ret = spi_device_transmit(spiDevice_, &trans);
    
    return ret;
}

esp_err_t RFIDReader::readRegister(uint8_t addr, uint8_t* data) {
    if (!spiDevice_ || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    
    uint8_t tx_data[2];
    uint8_t rx_data[2];
    
    tx_data[0] = ((addr << 1) & 0x7E) | 0x80; // Set MSB for read (RC522 uses 1 for read)
    tx_data[1] = 0x00; // Dummy byte
    
    spi_transaction_t trans = {};
    trans.length = 16; // 8 bits address + 8 bits data
    trans.tx_buffer = tx_data;
    trans.rx_buffer = rx_data;
    
    esp_err_t ret = spi_device_transmit(spiDevice_, &trans);
    if (ret != ESP_OK) {
        return ret;
    }
    
    *data = rx_data[1]; // Data is in the second byte
    
    return ret;
}

esp_err_t RFIDReader::writeFIFO(const uint8_t* data, uint8_t len) {
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (uint8_t i = 0; i < len; ++i) {
        esp_err_t ret = writeRegister(RC522_REG_FIFO_DATA, data[i]);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    return ESP_OK;
}

esp_err_t RFIDReader::readFIFO(uint8_t* data, uint8_t len) {
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    for (uint8_t i = 0; i < len; ++i) {
        esp_err_t ret = readRegister(RC522_REG_FIFO_DATA, &data[i]);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    return ESP_OK;
}

esp_err_t RFIDReader::setCommand(uint8_t cmd) {
    return writeRegister(RC522_REG_COMMAND, cmd);
}

void RFIDReader::clearInterrupts() {
    uint8_t value;
    if (readRegister(RC522_REG_COMIRQ, &value) == ESP_OK) {
        writeRegister(RC522_REG_COMIRQ, value);
    }
}

esp_err_t RFIDReader::initRC522() {
    ESP_LOGI(TAG, "Starting RC522 detailed initialization...");
    
    // Perform comprehensive SPI communication test
    esp_err_t ret = performDiagnostics();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "RC522 diagnostics failed!");
        return ret;
    }
    
    // Soft reset
    ret = writeRegister(RC522_REG_COMMAND, RC522_CMD_SOFT_RESET);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send soft reset command");
        return ret;
    }
    vTaskDelay(pdMS_TO_TICKS(50)); // Wait for reset
    
    // Test basic communication - read version register
    uint8_t version;
    ret = readRegister(0x37, &version);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read version register - SPI communication failed!");
        return ret;
    }
    ESP_LOGI(TAG, "RC522 Version register: 0x%02X", version);
    
    // Validate version register (0x91/0x92 for original RC522, 0xB2 for clones)
    if (version != 0x91 && version != 0x92 && version != 0xB2) {
        ESP_LOGW(TAG, "Unexpected version register value: 0x%02X (expected 0x91, 0x92, or 0xB2)", version);
    } else {
        ESP_LOGI(TAG, "RC522 chip detected: %s", 
                (version == 0xB2) ? "Clone/Compatible" : "Original");
    }
    
    // Initialize RC522 with proper configuration
    // Set timer to auto mode
    ret = writeRegister(0x2A, 0x8D);
    if (ret != ESP_OK) return ret;
    
    // Set timer prescaler
    ret = writeRegister(0x2B, 0x3E);
    if (ret != ESP_OK) return ret;
    
    // Set timer reload value (25ms timeout)
    ret = writeRegister(0x2D, 0x1E);
    if (ret != ESP_OK) return ret;
    ret = writeRegister(0x2C, 0x00);
    if (ret != ESP_OK) return ret;
    
    // Configure RF settings for better sensitivity
    ret = writeRegister(0x26, 0x7F); // RxGain = maximum (48dB)
    if (ret != ESP_OK) return ret;
    
    // Set CW conductance
    ret = writeRegister(0x27, 0x40);
    if (ret != ESP_OK) return ret;
    
    // Set modulation width
    ret = writeRegister(0x28, 0x26);
    if (ret != ESP_OK) return ret;
    
    // Turn on antenna with proper sequence
    uint8_t tx_control;
    ret = readRegister(0x14, &tx_control);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "TX Control register before: 0x%02X", tx_control);
        
        // Force antenna off first
        ret = writeRegister(0x14, tx_control & ~0x03);
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Turn antenna on
        ret = writeRegister(0x14, tx_control | 0x03);
        vTaskDelay(pdMS_TO_TICKS(10));
        
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Antenna turned on");
        }
    }
    
    // Verify antenna is on
    ret = readRegister(0x14, &tx_control);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "TX Control after antenna on: 0x%02X", tx_control);
        if (!(tx_control & 0x03)) {
            ESP_LOGE(TAG, "CRITICAL: Antenna failed to turn on!");
            return ESP_FAIL;
        }
    }
    
    // Additional antenna configuration
    ret = writeRegister(0x15, 0x40); // RFU reserved register
    if (ret != ESP_OK) return ret;
    
    // Set demodulator settings
    ret = writeRegister(0x11, 0x3D); // RxMode register
    if (ret != ESP_OK) return ret;
    
    ESP_LOGI(TAG, "RC522 initialization completed successfully");
    return ESP_OK;
}

esp_err_t RFIDReader::performDiagnostics() {
    ESP_LOGI(TAG, "=== RC522 SPI Diagnostics ===");
    
    // Test 1: Read version register multiple times
    ESP_LOGI(TAG, "Test 1: Reading version register 5 times...");
    for (int i = 0; i < 5; i++) {
        uint8_t version;
        esp_err_t ret = readRegister(0x37, &version);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Version read %d failed: %s", i+1, esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "Version read %d: 0x%02X", i+1, version);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Test 2: Write and read back test values
    ESP_LOGI(TAG, "Test 2: Write/Read test...");
    uint8_t testValues[] = {0x00, 0xFF, 0xAA, 0x55, 0x33};
    for (int i = 0; i < 5; i++) {
        esp_err_t ret = writeRegister(0x2C, testValues[i]); // Timer reload register (safe to write)
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Write test %d failed: %s", i+1, esp_err_to_name(ret));
            return ret;
        }
        
        uint8_t readback;
        ret = readRegister(0x2C, &readback);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Read test %d failed: %s", i+1, esp_err_to_name(ret));
            return ret;
        }
        
        ESP_LOGI(TAG, "Write/Read test %d: wrote=0x%02X, read=0x%02X, %s", 
                i+1, testValues[i], readback, 
                (testValues[i] == readback) ? "PASS" : "FAIL");
        
        if (testValues[i] != readback) {
            ESP_LOGE(TAG, "Write/Read test failed!");
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    // Test 3: Check critical registers
    ESP_LOGI(TAG, "Test 3: Reading critical registers...");
    uint8_t registers[] = {0x01, 0x04, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x14, 0x26, 0x37};
    const char* regNames[] = {"Command", "ComIrq", "Error", "Status1", "Status2", 
                             "FifoData", "FifoLevel", "TxControl", "RFCfg", "Version"};
    
    for (int i = 0; i < 10; i++) {
        uint8_t value;
        esp_err_t ret = readRegister(registers[i], &value);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read %s register (0x%02X): %s", 
                    regNames[i], registers[i], esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGI(TAG, "%s (0x%02X): 0x%02X", regNames[i], registers[i], value);
    }
    
    ESP_LOGI(TAG, "=== Diagnostics completed successfully ===");
    return ESP_OK;
}

esp_err_t RFIDReader::testCardDetection() {
    ESP_LOGI(TAG, "=== Manual Card Detection Test ===");
    ESP_LOGI(TAG, "Place an RFID card near the reader now...");
    
    for (int attempt = 1; attempt <= 10; attempt++) {
        ESP_LOGI(TAG, "Detection attempt %d/10...", attempt);
        
        // Clear interrupts and flush FIFO
        clearInterrupts();
        writeRegister(RC522_REG_FIFO_LEVEL, 0x80);
        
        // Ensure antenna is on for each attempt
        uint8_t tx_control;
        readRegister(0x14, &tx_control);
        if (!(tx_control & 0x03)) {
            ESP_LOGW(TAG, "Antenna off during test, turning on...");
            writeRegister(0x14, tx_control | 0x03);
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        
        // Configure for ISO14443A
        writeRegister(0x11, 0x3D); // RxMode
        writeRegister(0x12, 0x3D); // TxMode
        
        // Send REQA command
        uint8_t reqCmd = 0x26;
        writeRegister(RC522_REG_FIFO_DATA, reqCmd);
        writeRegister(RC522_REG_COMMAND, RC522_CMD_TRANSCEIVE);
        writeRegister(RC522_REG_BIT_FRAMING, 0x07);
        
        // Wait for response
        uint8_t status;
        int timeout = 200; // Longer timeout for manual test
        do {
            readRegister(RC522_REG_COMIRQ, &status);
            vTaskDelay(pdMS_TO_TICKS(1));
            timeout--;
        } while (!(status & 0x01) && !(status & 0x30) && timeout > 0);
        
        ESP_LOGI(TAG, "  Status: 0x%02X, Timeout: %d", status, timeout);
        
        if (!(timeout <= 0 || (status & 0x01))) {
            // Got a response!
            uint8_t errorReg;
            readRegister(RC522_REG_ERROR, &errorReg);
            ESP_LOGI(TAG, "  Error register: 0x%02X", errorReg);
            
            if (!(errorReg & 0x13)) {
                uint8_t fifoLevel;
                readRegister(RC522_REG_FIFO_LEVEL, &fifoLevel);
                ESP_LOGI(TAG, "  FIFO level: %d", fifoLevel);
                
                if (fifoLevel >= 2) {
                    uint8_t atqa[2];
                    readFIFO(atqa, 2);
                    ESP_LOGI(TAG, "  *** CARD DETECTED! ATQA: 0x%02X 0x%02X ***", atqa[0], atqa[1]);
                    return ESP_OK;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(500)); // Wait 500ms between attempts
    }
    
    ESP_LOGW(TAG, "No card detected in 10 attempts");
    return ESP_ERR_NOT_FOUND;
}

esp_err_t RFIDReader::testGPIOConnections() {
    ESP_LOGI(TAG, "=== GPIO Connection Test ===");
    
    // Test 1: Check if we can control RST pin
    ESP_LOGI(TAG, "Test 1: RST Pin Control Test");
    gpio_set_level(rstPin_, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "  RST set to LOW");
    
    gpio_set_level(rstPin_, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "  RST set to HIGH");
    
    // Test 2: SPI Communication Test with different patterns
    ESP_LOGI(TAG, "Test 2: SPI Communication Pattern Test");
    
    uint8_t testPatterns[] = {0x00, 0xFF, 0xAA, 0x55, 0x33, 0xCC, 0x0F, 0xF0};
    int passCount = 0;
    int totalTests = sizeof(testPatterns);
    
    for (int i = 0; i < totalTests; i++) {
        // Write to a safe register (Timer reload register)
        esp_err_t writeResult = writeRegister(0x2C, testPatterns[i]);
        vTaskDelay(pdMS_TO_TICKS(5));
        
        uint8_t readback;
        esp_err_t readResult = readRegister(0x2C, &readback);
        
        bool testPassed = (writeResult == ESP_OK && readResult == ESP_OK && readback == testPatterns[i]);
        
        ESP_LOGI(TAG, "  Pattern 0x%02X: Write=%s, Read=%s, Readback=0x%02X, %s",
                testPatterns[i],
                esp_err_to_name(writeResult),
                esp_err_to_name(readResult),
                readback,
                testPassed ? "PASS" : "FAIL");
        
        if (testPassed) passCount++;
    }
    
    ESP_LOGI(TAG, "  SPI Pattern Test: %d/%d passed", passCount, totalTests);
    
    // Test 3: Register Accessibility Test
    ESP_LOGI(TAG, "Test 3: Register Accessibility Test");
    
    uint8_t criticalRegs[] = {0x01, 0x04, 0x06, 0x07, 0x08, 0x14, 0x26, 0x37};
    const char* regNames[] = {"Command", "ComIrq", "Error", "Status1", "Status2", "TxControl", "RFCfg", "Version"};
    int regCount = sizeof(criticalRegs);
    int accessibleRegs = 0;
    
    for (int i = 0; i < regCount; i++) {
        uint8_t value;
        esp_err_t result = readRegister(criticalRegs[i], &value);
        
        if (result == ESP_OK) {
            ESP_LOGI(TAG, "  %s (0x%02X): 0x%02X - OK", regNames[i], criticalRegs[i], value);
            accessibleRegs++;
        } else {
            ESP_LOGE(TAG, "  %s (0x%02X): FAILED - %s", regNames[i], criticalRegs[i], esp_err_to_name(result));
        }
    }
    
    ESP_LOGI(TAG, "  Register Access: %d/%d accessible", accessibleRegs, regCount);
    
    // Test 4: Antenna Control Test
    ESP_LOGI(TAG, "Test 4: Antenna Control Test");
    
    // Turn antenna off
    uint8_t txControl;
    readRegister(0x14, &txControl);
    ESP_LOGI(TAG, "  Initial TX Control: 0x%02X", txControl);
    
    writeRegister(0x14, txControl & ~0x03);
    vTaskDelay(pdMS_TO_TICKS(10));
    readRegister(0x14, &txControl);
    ESP_LOGI(TAG, "  After turning OFF: 0x%02X (should be 0x80)", txControl);
    
    writeRegister(0x14, txControl | 0x03);
    vTaskDelay(pdMS_TO_TICKS(10));
    readRegister(0x14, &txControl);
    ESP_LOGI(TAG, "  After turning ON: 0x%02X (should be 0x83)", txControl);
    
    // Test 5: FIFO Test
    ESP_LOGI(TAG, "Test 5: FIFO Read/Write Test");
    
    // Clear FIFO
    writeRegister(RC522_REG_FIFO_LEVEL, 0x80);
    
    uint8_t testData[] = {0x26, 0x12, 0x34, 0x56};
    
    // Write to FIFO
    for (int i = 0; i < 4; i++) {
        writeRegister(RC522_REG_FIFO_DATA, testData[i]);
    }
    
    // Check FIFO level
    uint8_t fifoLevel;
    readRegister(RC522_REG_FIFO_LEVEL, &fifoLevel);
    ESP_LOGI(TAG, "  FIFO Level after write: %d (should be 4)", fifoLevel);
    
    // Read back from FIFO
    uint8_t readData[4];
    for (int i = 0; i < 4; i++) {
        readRegister(RC522_REG_FIFO_DATA, &readData[i]);
    }
    
    bool fifoOK = true;
    for (int i = 0; i < 4; i++) {
        if (testData[i] != readData[i]) {
            fifoOK = false;
            break;
        }
    }
    
    ESP_LOGI(TAG, "  FIFO Data integrity: %s", fifoOK ? "PASS" : "FAIL");
    if (!fifoOK) {
        ESP_LOGI(TAG, "    Written: 0x%02X 0x%02X 0x%02X 0x%02X", 
                testData[0], testData[1], testData[2], testData[3]);
        ESP_LOGI(TAG, "    Read:    0x%02X 0x%02X 0x%02X 0x%02X", 
                readData[0], readData[1], readData[2], readData[3]);
    }
    
    // Test Summary
    ESP_LOGI(TAG, "=== GPIO Test Summary ===");
    ESP_LOGI(TAG, "SPI Patterns: %d/%d", passCount, totalTests);
    ESP_LOGI(TAG, "Register Access: %d/%d", accessibleRegs, regCount);
    ESP_LOGI(TAG, "Antenna Control: %s", (txControl & 0x03) ? "OK" : "FAILED");
    ESP_LOGI(TAG, "FIFO Operation: %s", fifoOK ? "OK" : "FAILED");
    
    if (passCount == totalTests && accessibleRegs == regCount && (txControl & 0x03) && fifoOK) {
        ESP_LOGI(TAG, "*** ALL GPIO TESTS PASSED - Hardware connections OK ***");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "*** SOME GPIO TESTS FAILED - Check hardware connections ***");
        return ESP_FAIL;
    }
}
