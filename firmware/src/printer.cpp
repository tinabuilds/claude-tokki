#include "printer.h"
#include <Arduino.h>

#if __has_include(<PubSubClient.h>)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <mbedtls/platform.h>

// Runs in its own FreeRTOS task so Wi-Fi joins and TLS handshakes (1-3 s)
// never stall LVGL. The task owns `work`; the UI reads a mutex-guarded copy.

static const uint32_t PARSE_EVERY_MS = 2000;   // printer pushes ~1/s; the screen needs far less
static const size_t   MQTT_BUF = 24 * 1024;    // one full P2S report is ~19 KB (lands in PSRAM)

struct Cfg { String ssid, pass, ip, sn, code; };

static SemaphoreHandle_t lock;
static PrinterState shared = {};
static PrinterState work = {};
static uint32_t last_seen_seq = 0;
static printer_link_t last_seen_link = PRINTER_UNCONFIGURED;
static volatile bool reconfig = false;
static uint32_t last_parse_ms = 0;

static void publish_state() {
    xSemaphoreTake(lock, portMAX_DELAY);
    shared = work;
    xSemaphoreGive(lock);
}

static void set_link(printer_link_t l) {
    if (work.link == l) return;
    work.link = l;
    publish_state();
}

static bool load_cfg(Cfg* c) {
    Preferences p;
    p.begin("printer", true);
    c->ssid = p.getString("ssid", "");
    c->pass = p.getString("pass", "");
    c->ip   = p.getString("ip", "");
    c->sn   = p.getString("sn", "");
    c->code = p.getString("code", "");
    p.end();
    return c->ssid.length() && c->ip.length() && c->sn.length() && c->code.length();
}

static void on_message(char* topic, byte* payload, unsigned int len) {
    (void)topic;
    uint32_t now = millis();
    if (work.has_report && now - last_parse_ms < PARSE_EVERY_MS) return;
    last_parse_ms = now;
    if (printer_parse_report((const char*)payload, len, &work)) publish_state();
}

static void printer_task(void*) {
    WiFiClientSecure net;
    PubSubClient mqtt(net);
    mqtt.setBufferSize(MQTT_BUF);
    mqtt.setKeepAlive(30);
    mqtt.setSocketTimeout(10);
    mqtt.setCallback(on_message);
    net.setInsecure();   // printer cert is self-signed by Bambu's device CA; LAN only

    Cfg cfg;
    bool have_cfg = false;
    uint32_t backoff_ms = 2000;

    for (;;) {
        if (reconfig || !have_cfg) {
            reconfig = false;
            if (mqtt.connected()) mqtt.disconnect();
            WiFi.disconnect(true);
            have_cfg = load_cfg(&cfg);
            if (!have_cfg) { set_link(PRINTER_UNCONFIGURED); vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
            WiFi.mode(WIFI_STA);
            WiFi.setAutoReconnect(true);
            WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());
            mqtt.setServer(cfg.ip.c_str(), 8883);
            backoff_ms = 2000;
        }

        if (WiFi.status() != WL_CONNECTED) {
            set_link(PRINTER_WIFI);
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        if (!mqtt.connected()) {
            if (work.link != PRINTER_AUTH_FAILED) set_link(PRINTER_CONNECTING);
            char cid[24];
            snprintf(cid, sizeof(cid), "tokki-%06lx", (unsigned long)(ESP.getEfuseMac() & 0xffffff));
            if (mqtt.connect(cid, "bblp", cfg.code.c_str())) {
                String topic = "device/" + cfg.sn + "/report";
                mqtt.subscribe(topic.c_str());
                String req = "device/" + cfg.sn + "/request";
                mqtt.publish(req.c_str(), "{\"pushing\":{\"sequence_id\":\"0\",\"command\":\"pushall\"}}");
                Serial.println("printer: connected");
                set_link(PRINTER_ONLINE);
                backoff_ms = 2000;
            } else {
                int rc = mqtt.state();
                Serial.printf("printer: mqtt connect failed rc=%d (internal free %u, largest %u)\n", rc,
                              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                              (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
                // 4 = bad credentials, 5 = not authorized → wrong access code
                set_link((rc == 4 || rc == 5) ? PRINTER_AUTH_FAILED : PRINTER_CONNECTING);
                vTaskDelay(pdMS_TO_TICKS(backoff_ms));
                if (backoff_ms < 60000) backoff_ms *= 2;
                continue;
            }
        }

        mqtt.loop();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// The prebuilt Arduino libs pin mbedTLS to internal RAM, which BLE + LVGL leave
// too little of for a TLS handshake ("SSL - Memory allocation failed"). Route
// its allocations to PSRAM first; heap_caps_free releases blocks from either heap.
static void* tls_calloc(size_t n, size_t size) {
    void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}
static void tls_free(void* p) { heap_caps_free(p); }

void printer_init(void) {
    mbedtls_platform_set_calloc_free(tls_calloc, tls_free);
    lock = xSemaphoreCreateMutex();
    work.tray_now = -1;
    shared = work;
    xTaskCreatePinnedToCore(printer_task, "printer", 12288, nullptr, 1, nullptr, 0);
}

bool printer_get(PrinterState* out) {
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = shared;
    xSemaphoreGive(lock);
    bool changed = out->seq != last_seen_seq || out->link != last_seen_link;
    last_seen_seq = out->seq;
    last_seen_link = out->link;
    return changed;
}

bool printer_config_cmd(const char* json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    Preferences p;
    p.begin("printer", false);
    for (const char* k : {"ssid", "pass", "ip", "sn", "code"}) {
        if (doc[k].is<const char*>()) p.putString(k, doc[k].as<const char*>());
    }
    p.end();
    reconfig = true;
    // Never echo secrets back over serial
    Serial.printf("printer: config saved (ssid=%s ip=%s)\n",
                  doc["ssid"] | "-", doc["ip"] | "-");
    return true;
}

#else
// Boards built without PubSubClient: printer page stays "not set up".
static PrinterState none = {};
void printer_init(void) { none.tray_now = -1; }
bool printer_get(PrinterState* out) { *out = none; return false; }
bool printer_config_cmd(const char* json) { (void)json; return false; }
#endif
