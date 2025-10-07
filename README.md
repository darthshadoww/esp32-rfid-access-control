# ESP32 RFID-Based Access Control System

A complete ESP32-based RFID access control system using ESP-IDF v5.x, C++ OOP, and FreeRTOS multitasking. This system controls access to a door using RFID card authentication and servo motor control.

## 🏗️ Project Overview

This project implements a production-ready RFID access control system with the following features:

- **RFID Card Authentication**: RC522 RFID module for card detection and UID reading
- **Servo Door Control**: SG90 servo motor for automatic door opening/closing
- **Thread-Safe Logging**: UART-based logging system with queue management
- **WiFi Logging**: Optional TCP/IP logging to remote server
- **FreeRTOS Multitasking**: Concurrent task execution for optimal performance
- **Clean C++ OOP Design**: Modular, extensible, and maintainable code structure

## 🔧 Hardware Requirements

### ESP32 Development Board
- ESP32-WROOM-32 or similar
- 4MB+ Flash memory recommended
- 512KB+ PSRAM (optional but recommended)

### RFID Module (RC522)
- RC522 RFID reader module
- 13.56 MHz frequency
- SPI interface

### Servo Motor
- SG90 micro servo motor
- 4.8V-6V operating voltage
- 180-degree rotation

### Buzzer
- Piezo buzzer or small speaker
- 3.3V-5V operating voltage
- For audio feedback (access granted/denied, door operations)

### Power Supply
- 5V power supply for servo motor and buzzer
- 3.3V for ESP32 and RC522

### Additional Components
- Breadboard and jumper wires
- Pull-up resistors (10kΩ)
- LED indicators (optional)

## 📋 Hardware Connections

### ESP32 to RC522 RFID Module (SPI Connection)

| ESP32 Pin | RC522 Pin | Description |
|-----------|-----------|-------------|
| GPIO 23   | MOSI      | Master Out Slave In |
| GPIO 19   | MISO      | Master In Slave Out |
| GPIO 18   | SCK       | Serial Clock |
| GPIO 5    | SS/SDA    | Slave Select |
| GPIO 21   | RST       | Reset |
| 3.3V      | VCC       | Power Supply |
| GND       | GND       | Ground |

### ESP32 to SG90 Servo Motor

| ESP32 Pin | Servo Pin | Description |
|-----------|-----------|-------------|
| GPIO 2    | Signal    | PWM Control Signal |
| 5V        | VCC       | Power Supply (External) |
| GND       | GND       | Ground |

### ESP32 to Buzzer

| ESP32 Pin | Buzzer Pin | Description |
|-----------|------------|-------------|
| GPIO 4    | Positive   | PWM Control Signal |
| GND       | Negative   | Ground |

### UART for Logging (Default)

| ESP32 Pin | Description |
|-----------|-------------|
| GPIO 1    | TX (Serial Output) |
| GPIO 3    | RX (Serial Input) |

## 🚀 Building and Flashing

### Prerequisites

1. **ESP-IDF v5.x** installed and configured
2. **CMake** (version 3.16 or higher)
3. **Python** (version 3.6 or higher)
4. **Git** for version control

### Build Instructions

1. **Clone the repository:**
   ```bash
   git clone <repository-url>
   cd esp32-rfid-access-control
   ```

2. **Set up ESP-IDF environment:**
   ```bash
   . $HOME/esp/esp-idf/export.sh  # Linux/macOS
   # or
   %USERPROFILE%\esp\esp-idf\export.bat  # Windows
   ```

3. **Configure the project:**
   ```bash
   idf.py menuconfig
   ```
   
   Configure the following settings:
   - **Component config → ESP System Settings**: Set CPU frequency to 240 MHz
   - **Component config → FreeRTOS**: Enable tickless idle mode
   - **Component config → ESP WiFi**: Configure if using WiFi logging

4. **Build the project:**
   ```bash
   idf.py build
   ```

5. **Flash to ESP32:**
   ```bash
   idf.py -p /dev/ttyUSB0 flash monitor  # Linux
   # or
   idf.py -p COM3 flash monitor  # Windows
   ```

6. **Monitor output:**
   ```bash
   idf.py monitor
   ```

### Configuration Options

You can modify the following parameters in `main.cpp`:

```cpp
// WiFi Configuration (Optional)
g_wifiLogger = std::make_shared<WiFiLogger>(
    "YourWiFiSSID",      // Replace with your WiFi SSID
    "YourWiFiPassword",  // Replace with your WiFi password
    "192.168.1.100",     // Replace with your log server IP
    8080,                // Replace with your log server port
    g_logger
);

// Authorized RFID UIDs
static const std::vector<std::vector<uint8_t>> AUTHORIZED_UIDS = {
    {0x12, 0x34, 0x56, 0x78},  // Replace with your card UIDs
    {0xAB, 0xCD, 0xEF, 0x01},  // Replace with your card UIDs
    {0xDE, 0xAD, 0xBE, 0xEF}   // Replace with your card UIDs
};

// Door timing
static constexpr uint32_t DOOR_OPEN_TIME_MS = 5000;  // 5 seconds
static constexpr uint32_t RFID_SCAN_INTERVAL_MS = 100; // 100ms

// Buzzer configuration
g_buzzerController = std::make_shared<BuzzerController>(
    GPIO_NUM_4,          // Buzzer control pin (change as needed)
    LEDC_CHANNEL_1,      // LEDC channel
    LEDC_TIMER_1         // LEDC timer
);
```

## 📊 Example Output Logs

### System Startup
```
I (1234) MAIN: Starting RFID Access Control System
I (1235) MAIN: Initializing system components
I (1236) LOGGER: Logger initialized successfully
I (1237) RFID_READER: RFID Reader initialized successfully
I (1238) SERVO_CONTROLLER: ServoController initialized successfully
I (1239) MAIN: System initialization completed successfully
I (1240) MAIN: Creating FreeRTOS tasks
I (1241) MAIN: All tasks created successfully
I (1242) MAIN: === RFID Access Control System ===
I (1243) MAIN: ESP32 Chip: v5.1.0
I (1244) MAIN: Free heap: 234567 bytes
I (1245) MAIN: Min free heap: 123456 bytes
I (1246) MAIN: CPU frequency: 240 MHz
I (1247) MAIN: Authorized UIDs configured: 3
I (1248) MAIN: WiFi: Not connected
I (1249) MAIN: System ready for operation
I (1250) MAIN: ================================
I (1251) RFID_READER: RFID task started
I (1252) SERVO_CONTROLLER: Servo task started
I (1253) LOGGER: Logger task started
I (1254) WIFI_LOGGER: WiFi Logger task started
```

### Successful Card Authentication
```
I (5678) MAIN: Card detected: 12:34:56:78
I (5679) MAIN: Authorized card detected - opening door
I (5680) BUZZER_CONTROLLER: Playing tone: 800 Hz for 200 ms at volume 60
I (5681) SERVO_CONTROLLER: Opening door
I (5682) SERVO_CONTROLLER: Door opened successfully
I (5683) BUZZER_CONTROLLER: Playing tone: 600 Hz for 100 ms at volume 50
I (5684) MAIN: Door opened
I (5685) MAIN: Access granted for UID: 12:34:56:78
I (5686) MAIN: Auto-closing door after 5000 ms
I (5687) SERVO_CONTROLLER: Door closed successfully
I (5688) BUZZER_CONTROLLER: Playing tone: 800 Hz for 100 ms at volume 50
I (5689) MAIN: Door closed
I (5690) MAIN: Door auto-closed after timeout
```

### Unauthorized Access Attempt
```
I (8901) MAIN: Card detected: FF:FF:FF:FF
W (8902) MAIN: Unauthorized card detected - access denied
I (8903) BUZZER_CONTROLLER: Playing tone: 400 Hz for 150 ms at volume 70
I (8904) BUZZER_CONTROLLER: Playing tone: 400 Hz for 150 ms at volume 70
W (8905) MAIN: Access denied for UID: FF:FF:FF:FF
```

### System Status Monitoring
```
I (12034) MAIN: System status - Free heap: 234567 bytes
I (18034) MAIN: System status - Free heap: 234123 bytes
```

## 🏛️ Architecture Overview

### Class Structure

```
RFIDReader
├── Handles RC522 communication via SPI
├── Card detection and UID reading
├── Authorized UID management
└── Thread-safe card processing

ServoController
├── SG90 servo control via PWM
├── Door position management
├── Smooth movement capabilities
└── Auto-close functionality

Logger
├── Thread-safe UART logging
├── Message queue management
├── Log level filtering
└── Timestamp formatting

WiFiLogger (Optional)
├── WiFi connection management
├── TCP/IP log forwarding
├── Automatic reconnection
└── Connection status monitoring

BuzzerController
├── Audio feedback for system events
├── PWM-based tone generation
├── Predefined sound patterns
├── Volume control
└── Custom sound pattern support
```

### FreeRTOS Task Architecture

```
Main Task
├── System initialization
├── Component coordination
└── Health monitoring

RFID Task (Priority 3)
├── Continuous card scanning
├── UID validation
└── Access control decisions

Servo Task (Priority 2)
├── Door control commands
├── Position management
└── Auto-close timing

Logger Task (Priority 1)
├── UART message processing
├── Queue management
└── Log formatting

WiFi Logger Task (Priority 1)
├── WiFi connection monitoring
├── Remote log forwarding
└── Connection health checks
```

### Communication Flow

```
RFID Card → RFID Task → Authorization Check → Servo Task → Door Control
     ↓
Logger Task ← Log Queue ← Access Events ← All Tasks
     ↓
WiFi Logger Task → Remote Server (Optional)
```

## 🔧 Configuration and Customization

### Adding New Authorized Cards

1. **Read your RFID card UID:**
   ```cpp
   // Place card near reader and check serial output
   I (1234) RFID_READER: Card detected: AB:CD:EF:12
   ```

2. **Add UID to authorized list:**
   ```cpp
   static const std::vector<std::vector<uint8_t>> AUTHORIZED_UIDS = {
       {0x12, 0x34, 0x56, 0x78},  // Existing card
       {0xAB, 0xCD, 0xEF, 0x12},  // New card
   };
   ```

3. **Rebuild and flash:**
   ```bash
   idf.py build flash
   ```

### Modifying Door Timing

```cpp
// Adjust door open duration
static constexpr uint32_t DOOR_OPEN_TIME_MS = 10000;  // 10 seconds

// Adjust RFID scan frequency
static constexpr uint32_t RFID_SCAN_INTERVAL_MS = 50;  // 50ms (faster scanning)
```

### GPIO Pin Configuration

If you need to change GPIO pins, modify the initialization in `main.cpp`:

```cpp
// RFID Reader pins
g_rfidReader = std::make_shared<RFIDReader>(
    SPI2_HOST,           // SPI host
    GPIO_NUM_23,         // MOSI (change as needed)
    GPIO_NUM_19,         // MISO (change as needed)
    GPIO_NUM_18,         // CLK (change as needed)
    GPIO_NUM_5,          // CS (change as needed)
    GPIO_NUM_21          // RST (change as needed)
);

// Servo control pin
g_servoController = std::make_shared<ServoController>(
    GPIO_NUM_2,          // Servo control pin (change as needed)
    LEDC_CHANNEL_0,      // LEDC channel
    LEDC_TIMER_0,        // LEDC timer
    50                   // 50Hz frequency
);
```

## 🚀 Future Improvements

### Hardware Enhancements
- **Ethernet Support**: Add Ethernet connectivity for more reliable network logging
- **RS485 Communication**: Implement RS485 for long-distance communication
- **LCD Display**: Add LCD display for system status and user feedback
- **Keypad Interface**: Implement keypad for PIN-based access
- **Biometric Sensors**: Add fingerprint or face recognition
- **Camera Module**: Add camera for access logging with photos
- **Advanced Audio**: Add speaker for voice announcements and music
- **LED Strip**: Add RGB LED strip for visual status indicators

### Software Features
- **Web Interface**: Create web-based configuration and monitoring interface
- **Database Integration**: Store access logs in SQLite or external database
- **Time-based Access**: Implement time-based access control (working hours)
- **Multi-zone Support**: Support multiple doors/zones
- **Mobile App**: Develop mobile app for remote monitoring
- **Cloud Integration**: Integrate with cloud services for remote management

### Security Enhancements
- **Encryption**: Implement end-to-end encryption for network communication
- **Certificate-based Authentication**: Use X.509 certificates for secure communication
- **Tamper Detection**: Add tamper detection and alerting
- **Backup Power**: Implement UPS/battery backup system
- **Audit Trail**: Enhanced audit trail with digital signatures

### Performance Optimizations
- **Power Management**: Implement deep sleep modes for battery operation
- **Memory Optimization**: Optimize memory usage for better performance
- **Real-time Scheduling**: Implement real-time scheduling for critical tasks
- **Load Balancing**: Distribute tasks across multiple cores

## 🐛 Troubleshooting

### Common Issues

1. **RFID Reader Not Detecting Cards**
   - Check SPI connections
   - Verify power supply (3.3V)
   - Ensure proper grounding
   - Check if RC522 is properly initialized

2. **Servo Not Moving**
   - Verify PWM signal connection
   - Check servo power supply (5V)
   - Ensure proper PWM frequency (50Hz)
   - Check servo control pin configuration

3. **Buzzer Not Working**
   - Check PWM signal on GPIO 4
   - Verify buzzer power supply (3.3V-5V)
   - Check buzzer polarity (positive to GPIO 4, negative to GND)
   - Test buzzer with simple tone generation

4. **WiFi Connection Issues**
   - Verify WiFi credentials
   - Check signal strength
   - Ensure correct IP address and port
   - Check firewall settings

5. **Build Errors**
   - Ensure ESP-IDF v5.x is installed
   - Check CMake version (3.16+)
   - Verify all dependencies are installed
   - Clean build: `idf.py fullclean`

### Debug Mode

Enable debug logging by modifying log levels in `main.cpp`:

```cpp
// Set debug log level
g_logger->setLogLevel(Logger::LogLevel::DEBUG);
```

### Performance Monitoring

Monitor system performance using built-in logging:

```cpp
// Check memory usage
ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());

// Check task stack usage
UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(nullptr);
ESP_LOGI(TAG, "Stack high water mark: %lu bytes", stackHighWaterMark);
```

## 📄 License

This project is licensed under the MIT License - see the LICENSE file for details.

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request. For major changes, please open an issue first to discuss what you would like to change.

## 📞 Support

For support and questions:
- Create an issue in the repository
- Check the troubleshooting section
- Review ESP-IDF documentation
- Consult the ESP32 community forums

---

**Note**: This project is designed for educational and development purposes. For production use, ensure proper security measures and thorough testing are implemented.
