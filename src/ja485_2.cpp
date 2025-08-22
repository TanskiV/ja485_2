#include "Particle.h"

// This firmware provides a minimal implementation of the standard
// controller functions requested for all Shefa Green controllers. The
// implementation focuses on the public interface and the handling of
// configuration metadata. Hardware specific behaviour (sensor reads,
// HTTP requests, etc.) is intentionally simplified so the code can act
// as a reference template for other controllers in the fleet.

SYSTEM_MODE(AUTOMATIC);
SYSTEM_THREAD(ENABLED);

SerialLogHandler logHandler(LOG_LEVEL_INFO);

// ----- Types -----------------------------------------------------------------

struct ActionResult {
    String status;          // "ok" or "error"
    String error_reason;    // description on failure
    int    http_status;     // optional HTTP status for PushNow
};

struct ConfigParam {
    float  current_value;        // persisted value
    time_t last_changed_unix;    // unix timestamp
    String last_changed_iso;     // iso8601 string
    String last_changed_source;  // source of change
};

struct ConfigPayload {
    String status;
    String device_id;
    String firmware_version;
    uint32_t boot_count;
    uint32_t send_success_count;
    uint32_t send_fail_count;
    ConfigParam display_interval;
    ConfigParam server_interval;
};

struct ReadingPayload {
    String status;
    uint32_t sample_id;
    String device_id;
    String firmware_version;
    float server_interval;
    float display_interval;
    time_t unix_ts;
    String iso_time;
    // Placeholder for actual measured values
    float dummy_value;
};

// ----- Persistent configuration ----------------------------------------------

struct PersistentConfig {
    float display_interval;  // minutes
    float server_interval;   // minutes
    uint32_t boot_count;
};

const int EEPROM_ADDR = 0; // location of persisted configuration
PersistentConfig persistent;

// Runtime configuration metadata
ConfigParam displayIntervalCfg;
ConfigParam serverIntervalCfg;

// Telemetry counters
uint32_t sendSuccessCount = 0;
uint32_t sendFailCount = 0;

// Sample counter for GetReadings
uint32_t sampleCounter = 0;

// Mutex to guard access to the send routine used by PushNow and scheduled
// transmissions. In this reference implementation the scheduled behaviour is
// not implemented but the mutex is kept to show how concurrency is handled.
Mutex sendMutex;

// Configuration for the remote server endpoint. In a real deployment these
// values would be provided by product settings or compile-time definitions.
// They are kept here as simple constants for clarity in this template.
const char *SERVER_HOST   = "example.com";  // server base host
const int   SERVER_PORT   = 80;             // server port
const char *SERVER_PATH   = "/api/sensor-data";
const char *SENSOR_SECRET = "changeme";     // authentication secret

// Last time sensor data was pushed to the server
time_t lastServerPush = 0;

// ----- Utility functions ------------------------------------------------------

String iso8601FromTime(time_t ts) {
    // Particle's Time class provides conversion helpers; we build the string
    // manually to avoid pulling in additional dependencies.
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
             Time.year(ts), Time.month(ts), Time.day(ts),
             Time.hour(ts), Time.minute(ts), Time.second(ts));
    return String(buf);
}

void loadPersistent() {
    EEPROM.get(EEPROM_ADDR, persistent);

    if (persistent.display_interval < 0.1f || persistent.display_interval > 60.0f)
        persistent.display_interval = 1.0f; // default 1 minute

    if (persistent.server_interval < 0.1f || persistent.server_interval > 60.0f)
        persistent.server_interval = 5.0f; // default 5 minutes

    displayIntervalCfg.current_value = persistent.display_interval;
    displayIntervalCfg.last_changed_unix = Time.now();
    displayIntervalCfg.last_changed_iso = iso8601FromTime(displayIntervalCfg.last_changed_unix);
    displayIntervalCfg.last_changed_source = "EEPROM";

    serverIntervalCfg.current_value = persistent.server_interval;
    serverIntervalCfg.last_changed_unix = Time.now();
    serverIntervalCfg.last_changed_iso = iso8601FromTime(serverIntervalCfg.last_changed_unix);
    serverIntervalCfg.last_changed_source = "EEPROM";
}

void savePersistent() {
    persistent.display_interval = displayIntervalCfg.current_value;
    persistent.server_interval  = serverIntervalCfg.current_value;
    EEPROM.put(EEPROM_ADDR, persistent);
}

// Serialize the readings and send them to the configured server. The function
// returns true on HTTP 2xx responses and provides the status code through
// `httpStatus`.
bool sendReadingsToServer(const ReadingPayload &r, int &httpStatus) {
    // Build JSON body
    String body = String::format(
        "{\"deviceId\":\"%s\",\"firmwareVersion\":\"%s\",\"timestamp\":%lu,\"dummy_value\":%.2f}",
        r.device_id.c_str(), r.firmware_version.c_str(), (unsigned long)r.unix_ts, r.dummy_value);

    TCPClient client;
    if (!client.connect(SERVER_HOST, SERVER_PORT)) {
        httpStatus = 0;
        return false;
    }

    String request = String::format(
        "POST %s HTTP/1.1\r\nHost: %s\r\nContent-Type: application/json\r\nx-sensor-secret: %s\r\nContent-Length: %d\r\n\r\n%s",
        SERVER_PATH, SERVER_HOST, SENSOR_SECRET, body.length(), body.c_str());
    client.print(request);

    // Wait for response
    unsigned long start = millis();
    while (client.connected() && !client.available() && millis() - start < 5000) {
        Particle.process();
    }
    String statusLine = client.readStringUntil('\n');
    if (statusLine.startsWith("HTTP/1.1 ")) {
        httpStatus = statusLine.substring(9, 12).toInt();
    } else {
        httpStatus = 0;
    }
    client.stop();
    return httpStatus >= 200 && httpStatus < 300;
}

// ----- API function implementations -----------------------------------------

ReadingPayload GetReadings() {
    ReadingPayload r;
    r.status = "ok";
    r.sample_id = ++sampleCounter;
    r.device_id = System.deviceID();
    r.firmware_version = System.version().string();
    r.server_interval = serverIntervalCfg.current_value;
    r.display_interval = displayIntervalCfg.current_value;
    r.unix_ts = Time.now();
    r.iso_time = iso8601FromTime(r.unix_ts);
    r.dummy_value = 0.0f; // replace with real sensor data
    return r;
}

ConfigPayload GetConfig() {
    ConfigPayload c;
    c.status = "ok";
    c.device_id = System.deviceID();
    c.firmware_version = System.version().string();
    c.boot_count = persistent.boot_count;
    c.send_success_count = sendSuccessCount;
    c.send_fail_count = sendFailCount;
    c.display_interval = displayIntervalCfg;
    c.server_interval = serverIntervalCfg;
    return c;
}

ActionResult SetDisplayInterval(float minutes) {
    ActionResult res;
    if (minutes < 0.1f || minutes > 60.0f) {
        res.status = "error";
        res.error_reason = "out_of_range";
        return res;
    }
    if (fabs(displayIntervalCfg.current_value - minutes) < 0.0001f) {
        res.status = "ok";
        return res;
    }

    displayIntervalCfg.current_value = minutes;
    displayIntervalCfg.last_changed_unix = Time.now();
    displayIntervalCfg.last_changed_iso = iso8601FromTime(displayIntervalCfg.last_changed_unix);
    displayIntervalCfg.last_changed_source = "Cloud/UI";
    savePersistent();

    res.status = "ok";
    return res;
}

ActionResult SetServerInterval(float minutes) {
    ActionResult res;
    if (minutes < 0.1f || minutes > 60.0f) {
        res.status = "error";
        res.error_reason = "out_of_range";
        return res;
    }
    if (fabs(serverIntervalCfg.current_value - minutes) < 0.0001f) {
        res.status = "ok";
        return res;
    }

    serverIntervalCfg.current_value = minutes;
    serverIntervalCfg.last_changed_unix = Time.now();
    serverIntervalCfg.last_changed_iso = iso8601FromTime(serverIntervalCfg.last_changed_unix);
    serverIntervalCfg.last_changed_source = "Cloud/UI";
    savePersistent();

    res.status = "ok";
    return res;
}

ActionResult SoftReset() {
    ActionResult res;
    res.status = "ok";
    res.error_reason = "";
    System.reset();
    return res; // never reached; device resets immediately
}

ActionResult PushNow() {
    ActionResult res;

    sendMutex.lock();
    ReadingPayload reading = GetReadings();
    int httpStatus = 0;
    bool success = sendReadingsToServer(reading, httpStatus);
    if (success) {
        sendSuccessCount++;
        res.status = "ok";
        res.http_status = httpStatus;
        Log.info("PushNow succeeded (%d)", httpStatus);
        Particle.publish("sensor/push", "manual_ok", PRIVATE);
    } else {
        sendFailCount++;
        res.status = "error";
        res.http_status = httpStatus;
        res.error_reason = "send_failed";
        Log.error("PushNow failed (%d)", httpStatus);
        Particle.publish("sensor/push", "manual_error", PRIVATE);
    }
    sendMutex.unlock();

    return res;
}

// ----- Standard setup/loop ---------------------------------------------------

void setup() {
    loadPersistent();
    persistent.boot_count++;
    EEPROM.put(EEPROM_ADDR, persistent);
}

void loop() {
    time_t now = Time.now();
    if (now - lastServerPush >= (time_t)(serverIntervalCfg.current_value * 60)) {
        lastServerPush = now;
        sendMutex.lock();
        ReadingPayload reading = GetReadings();
        int httpStatus = 0;
        bool success = sendReadingsToServer(reading, httpStatus);
        if (success) {
            sendSuccessCount++;
            Log.info("Scheduled push succeeded (%d)", httpStatus);
            Particle.publish("sensor/push", "scheduled_ok", PRIVATE);
        } else {
            sendFailCount++;
            Log.error("Scheduled push failed (%d)", httpStatus);
            Particle.publish("sensor/push", "scheduled_error", PRIVATE);
        }
        sendMutex.unlock();
    }
}

