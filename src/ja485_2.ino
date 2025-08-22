// This #include statement was automatically added by the Particle IDE.
#include <ModbusMaster-Particle.h>

#include "ModbusMaster-Particle.h"

// Create Modbus instance
ModbusMaster node;

// Global variables for storing sensor values
float ecValue = -1.0, ecTempValue = -1.0, phValue = -1.0, orpValue = -1.0;

// Default intervals (in minutes)
float displayInterval = 0.1; // Default: 6 sec
float serverInterval = 30.0; // Default: 30 min

// Timers
unsigned long lastDisplayTime = 0;
unsigned long lastServerTime = 0;

void setup() {
    Serial.begin(9600);
    waitFor(Serial.isConnected, 5000);

    Serial1.begin(9600, SERIAL_8N1);
    Serial.println("Modbus Initialized.");

    // Register Particle cloud functions
    Particle.function("getReadings", getReadings);
    Particle.function("setDisplayInterval", setDisplayInterval);
    Particle.function("setServerInterval", setServerInterval);
}

void loop() {
    unsigned long currentMillis = millis();

    // Check if it's time to display the readings
    if (currentMillis - lastDisplayTime >= displayInterval * 60000) {
        lastDisplayTime = currentMillis;
        readECController();
        delay(300);
        readPHController();
        delay(300);
        readORPController();
        Serial.println("------------------------------------------------");
    }

    // Check if it's time to publish readings to the server
    if (currentMillis - lastServerTime >= serverInterval * 60000) {
        lastServerTime = currentMillis;
        publishReadings();
    }
}

// Function to read EC controller
void readECController() {
    node.begin(1, Serial1); // EC Controller ID
    float temp;

    // Read temperature (Register 72)
    if (readRegister(72, temp, 10.0)) {
        ecTempValue = temp;
        Serial.printlnf("EC Temperature: %.1f °C", ecTempValue);
    } else {
        ecTempValue = -1.0;
    }

    // Read EC (Register 75)
    if (readRegister(75, temp, 1000.0)) {
        ecValue = temp;
        Serial.printlnf("Electrical Conductivity: %.3f mS/cm", ecValue);
    } else {
        ecValue = -1.0;
    }
}

// Function to read pH controller
void readPHController() {
    node.begin(2, Serial1); // pH Controller ID
    float temp;

    // Read pH (Register 75)
    if (readRegister(75, temp, 100.0)) {
        phValue = temp;
        Serial.printlnf("pH Value: %.2f", phValue);
    } else {
        phValue = -1.0;
    }
}

// Function to read ORP controller
void readORPController() {
    node.begin(3, Serial1); // ORP Controller ID
    float temp;

    // Read ORP (Register 169) -> (Manual says 170, so we use 169 in the code)
    if (readRegister(169, temp, 1.0)) {
        orpValue = temp;
        Serial.printlnf("ORP Value: %.1f mV", orpValue);
    } else {
        orpValue = -1.0;
    }
}

// Helper function to read registers
bool readRegister(uint16_t reg, float &value, float scale) {
    uint8_t result = node.readHoldingRegisters(reg, 1);
    if (result == node.ku8MBSuccess) {
        value = node.getResponseBuffer(0) / scale;
        return true;
    } else {
        Serial.printlnf("Error reading register %d: %d", reg, result);
        return false;
    }
}

// Cloud function to get sensor readings immediately
int getReadings(String command) {
    readECController();
    delay(300);
    readPHController();
    delay(300);
    readORPController();
    publishReadings();
    return 1;
}

// Function to publish readings to the Particle Cloud
void publishReadings() {
    String response = String::format(
        "{\\\"deviceId\\\":\\\"%s\\\",\\\"timestamp\\\":%lu,\\\"firmware\\\":\\\"%s\\\",\\\"EC_Temperature\\\":%.1f,\\\"EC\\\":%.3f,\\\"pH\\\":%.2f,\\\"ORP\\\":%.1f}",
        System.deviceID().c_str(), Time.now(), System.version().c_str(),
        ecTempValue, ecValue, phValue, orpValue
    );
    Particle.publish("sensor/readings", response, PRIVATE);
    Serial.println("[Cloud Publish] " + response);
}

// Cloud function to set display interval
int setDisplayInterval(String command) {
    float interval = command.toFloat();
    if (interval > 0) {
        displayInterval = interval;
        Serial.printlnf("Display interval set to %.1f minutes (%.1f sec)", displayInterval, displayInterval * 60);
        return 1;
    } else {
        return -1;
    }
}

// Cloud function to set server interval
int setServerInterval(String command) {
    float interval = command.toFloat();
    if (interval > 0) {
        serverInterval = interval;
        Serial.printlnf("Server interval set to %.1f minutes", serverInterval);
        return 1;
    } else {
        return -1;
    }
}
