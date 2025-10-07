#ifndef RFID_READER_H
#define RFID_READER_H

#include "esp_err.h"
#include "esp_timer.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string>
#include <vector>

/**
 * @brief RFIDReader class for RC522 RFID module
 * 
 * This class handles communication with the RC522 RFID reader module
 * via SPI interface. It provides functionality to detect and read
 * RFID card UIDs.
 */
class RFIDReader {
public:
    /**
     * @brief RFID card data structure
     */
    struct CardData {
        std::vector<uint8_t> uid;  // Card UID (4-10 bytes)
        bool isValid;              // Whether the card is valid
        uint64_t timestamp;        // Detection timestamp
        
        CardData() : isValid(false), timestamp(0) {}
    };

    /**
     * @brief Constructor
     * @param spi_host SPI host interface (e.g., SPI2_HOST)
     * @param mosi_pin MOSI GPIO pin
     * @param miso_pin MISO GPIO pin
     * @param clk_pin CLK GPIO pin
     * @param cs_pin CS GPIO pin
     * @param rst_pin RST GPIO pin
     */
    RFIDReader(spi_host_device_t spi_host, 
               gpio_num_t mosi_pin, 
               gpio_num_t miso_pin, 
               gpio_num_t clk_pin, 
               gpio_num_t cs_pin, 
               gpio_num_t rst_pin);

    /**
     * @brief Destructor
     */
    ~RFIDReader();

    /**
     * @brief Initialize the RFID reader
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t init();

    /**
     * @brief Check if a card is present
     * @return true if card is detected, false otherwise
     */
    bool isCardPresent();

    /**
     * @brief Read the UID of the detected card
     * @param cardData Reference to store card data
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t readCard(CardData& cardData);

    /**
     * @brief Get the last detected card data
     * @return CardData structure
     */
    CardData getLastCardData() const { return lastCardData_; }

    /**
     * @brief Check if the given UID is in the authorized list
     * @param uid Card UID to check
     * @return true if authorized, false otherwise
     */
    bool isAuthorized(const std::vector<uint8_t>& uid) const;

    /**
     * @brief Add an authorized UID
     * @param uid UID to authorize
     */
    void addAuthorizedUID(const std::vector<uint8_t>& uid);

    /**
     * @brief Remove an authorized UID
     * @param uid UID to remove
     */
    void removeAuthorizedUID(const std::vector<uint8_t>& uid);

    /**
     * @brief Clear all authorized UIDs
     */
    void clearAuthorizedUIDs();

    /**
     * @brief Convert UID to string representation
     * @param uid UID to convert
     * @return String representation of UID
     */
    static std::string uidToString(const std::vector<uint8_t>& uid);

    /**
     * @brief Test card detection with detailed debugging
     * @return ESP_OK if card detected, error code otherwise
     */
    esp_err_t testCardDetection();

    /**
     * @brief Test individual GPIO connections and SPI signals
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t testGPIOConnections();

private:
    // SPI configuration
    spi_host_device_t spiHost_;
    gpio_num_t mosiPin_;
    gpio_num_t misoPin_;
    gpio_num_t clkPin_;
    gpio_num_t csPin_;
    gpio_num_t rstPin_;
    
    // SPI device handle
    spi_device_handle_t spiDevice_;
    
    // Last detected card data
    CardData lastCardData_;
    
    // Authorized UIDs list
    std::vector<std::vector<uint8_t>> authorizedUIDs_;
    
    // RC522 register addresses and commands
    static constexpr uint8_t RC522_CMD_IDLE = 0x00;
    static constexpr uint8_t RC522_CMD_MEM = 0x01;
    static constexpr uint8_t RC522_CMD_GENERATE_RANDOM_ID = 0x02;
    static constexpr uint8_t RC522_CMD_CALC_CRC = 0x03;
    static constexpr uint8_t RC522_CMD_TRANSMIT = 0x04;
    static constexpr uint8_t RC522_CMD_NO_CMD_CHANGE = 0x07;
    static constexpr uint8_t RC522_CMD_RECEIVE = 0x08;
    static constexpr uint8_t RC522_CMD_TRANSCEIVE = 0x0C;
    static constexpr uint8_t RC522_CMD_MF_AUTHENT = 0x0E;
    static constexpr uint8_t RC522_CMD_SOFT_RESET = 0x0F;

    // RC522 register addresses
    static constexpr uint8_t RC522_REG_COMMAND = 0x01;
    static constexpr uint8_t RC522_REG_COMIEN = 0x02;
    static constexpr uint8_t RC522_REG_DIVLEN = 0x03;
    static constexpr uint8_t RC522_REG_COMIRQ = 0x04;
    static constexpr uint8_t RC522_REG_DIVIRQ = 0x05;
    static constexpr uint8_t RC522_REG_ERROR = 0x06;
    static constexpr uint8_t RC522_REG_STATUS1 = 0x07;
    static constexpr uint8_t RC522_REG_STATUS2 = 0x08;
    static constexpr uint8_t RC522_REG_FIFO_DATA = 0x09;
    static constexpr uint8_t RC522_REG_FIFO_LEVEL = 0x0A;
    static constexpr uint8_t RC522_REG_WATER_LEVEL = 0x0B;
    static constexpr uint8_t RC522_REG_CONTROL = 0x0C;
    static constexpr uint8_t RC522_REG_BIT_FRAMING = 0x0D;
    static constexpr uint8_t RC522_REG_COLL = 0x0E;

    // RC522 initialization data
    static constexpr uint8_t RC522_INIT_DATA[][2] = {
        {0x2A, 0x8D}, {0x2B, 0x3E}, {0x2D, 0x1E}, {0x2C, 0x00},
        {0x15, 0x40}, {0x11, 0x3D}, {0x26, 0x4D}, {0x37, 0x00},
        {0x36, 0x00}, {0x10, 0x00}, {0x0F, 0x00}, {0x05, 0x00},
        {0x04, 0x00}, {0x02, 0x00}, {0x08, 0x00}, {0x1B, 0x00},
        {0x23, 0x00}, {0x21, 0x00}, {0x22, 0x00}, {0x24, 0x00},
        {0x25, 0x00}, {0x26, 0x00}, {0x27, 0x00}, {0x29, 0x00},
        {0x2A, 0x00}, {0x2B, 0x00}, {0x2C, 0x00}, {0x2D, 0x00},
        {0x2E, 0x00}, {0x14, 0x00}, {0x12, 0x00}, {0x13, 0x00}
    };

    /**
     * @brief Write a byte to RC522 register
     * @param addr Register address
     * @param data Data to write
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t writeRegister(uint8_t addr, uint8_t data);

    /**
     * @brief Read a byte from RC522 register
     * @param addr Register address
     * @param data Pointer to store read data
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t readRegister(uint8_t addr, uint8_t* data);

    /**
     * @brief Write multiple bytes to RC522 FIFO
     * @param data Data to write
     * @param len Number of bytes to write
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t writeFIFO(const uint8_t* data, uint8_t len);

    /**
     * @brief Read multiple bytes from RC522 FIFO
     * @param data Buffer to store read data
     * @param len Number of bytes to read
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t readFIFO(uint8_t* data, uint8_t len);

    /**
     * @brief Set RC522 command
     * @param cmd Command to set
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t setCommand(uint8_t cmd);

    /**
     * @brief Clear RC522 interrupts
     */
    void clearInterrupts();

    /**
     * @brief Initialize RC522 with default values
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t initRC522();
    
    /**
     * @brief Perform comprehensive diagnostics on RC522 communication
     * @return ESP_OK on success, error code otherwise
     */
    esp_err_t performDiagnostics();
};

#endif // RFID_READER_H
