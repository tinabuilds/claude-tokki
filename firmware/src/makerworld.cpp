#include "makerworld.h"
#include <Arduino.h>
#include "task_log.h"

#if __has_include(<HTTPClient.h>)
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "bambu_ca.h"

// Runs in its own FreeRTOS task: each HTTPS call takes 1-3 s and must never
// stall LVGL. Wi-Fi is brought up by the printer task (same stored network);
// this task just waits for it. The UI reads a mutex-guarded copy of `work`.

static const char*    API = "https://api.bambulab.com";
static const uint32_t POLL_MS = 5 * 60 * 1000;   // creator stats move slowly; be a light client

static SemaphoreHandle_t lock;
static MakerWorldState shared = {};
static MakerWorldState work = {};
static uint32_t last_seen_seq = 0;
static mw_link_t last_seen_link = MW_OFF;

// Pending serial requests, handed from the loop to the task
static volatile bool want_code = false, want_login = false, want_off = false, want_points = false;
static char pending_email[96] = "";
static char pending_code[16] = "";

static String uid, token;
static String avatar_url, avatar_loaded;   // profile avatar, and the one on screen

static void publish_state() {
    xSemaphoreTake(lock, portMAX_DELAY);
    shared = work;
    xSemaphoreGive(lock);
}

static void set_link(mw_link_t l) {
    if (work.link == l) return;
    work.link = l;
    publish_state();
}

static void load_cfg() {
    Preferences p;
    p.begin("mw", false);   // read-write so a fresh board creates the namespace instead of logging NOT_FOUND
    uid = p.getString("uid", "");
    token = p.getString("tok", "");
    bool expired = p.getBool("expired", false);   // a rejected token stays rejected across reboots
    p.end();
    work.has_points = token.length() > 0;
    work.link = !uid.length() ? MW_OFF : expired ? MW_EXPIRED : MW_WAITING;
}

// One HTTPS call to the app API. Returns the HTTP status (<0 on transport error).
static int api(const char* path, const char* body, String* out) {
    WiFiClientSecure net;
    net.setCACert(BAMBU_API_ROOT_CA);
    HTTPClient http;
    http.setTimeout(12000);
    if (!http.begin(net, String(API) + path)) return -1;
    http.setUserAgent("Bambooboo/1.1");   // the default "ESP32HTTPClient" gets a Cloudflare 403
    http.addHeader("Content-Type", "application/json");
    if (token.length()) http.addHeader("Authorization", "Bearer " + token);
    int code = body ? http.POST(String(body)) : http.GET();
    if (code > 0 && out) *out = http.getString();
    http.end();
    return code;
}

static void send_code() {
    JsonDocument d;
    d["email"] = pending_email;
    d["type"] = "codeLogin";
    String body;
    serializeJson(d, body);
    String saved = token;
    token = "";   // the code request must go out unauthenticated
    int st = api("/v1/user-service/user/sendemail/code", body.c_str(), nullptr);
    token = saved;
    if (st == 200) task_log("mw: code sent");
    else           task_log("mw: code request failed (%d)", st);
}

static void login() {
    JsonDocument d;
    d["account"] = pending_email;
    d["code"] = pending_code;
    String body, resp;
    serializeJson(d, body);
    token = "";
    int st = api("/v1/user-service/user/login", body.c_str(), &resp);
    JsonDocument r;
    if (st != 200 || deserializeJson(r, resp) || !r["accessToken"].is<const char*>()) {
        task_log("mw: sign-in failed (%d) - check the code", st);
        load_cfg();   // keep whatever account was there before
        return;
    }
    token = r["accessToken"].as<const char*>();

    // The token tells us who this is; the user id is all public mode keeps
    JsonDocument filter;
    filter["uid"] = true;
    st = api("/v1/user-service/my/profile", nullptr, &resp);
    JsonDocument prof;
    if (st != 200 || deserializeJson(prof, resp, DeserializationOption::Filter(filter)) || !prof["uid"]) {
        task_log("mw: signed in but profile failed (%d)", st);
        load_cfg();
        return;
    }
    uid = String(prof["uid"].as<uint64_t>());
    if (!want_points) token = "";

    Preferences p;
    p.begin("mw", false);
    p.putString("uid", uid);
    if (token.length()) p.putString("tok", token);
    else                p.remove("tok");
    p.putBool("expired", false);
    p.end();

    work = {};
    work.has_points = token.length() > 0;
    work.link = MW_WAITING;
    publish_state();
    task_log("mw: signed in (%s mode)", work.has_points ? "points" : "public");
}

static void forget() {
    Preferences p;
    p.begin("mw", false);
    p.clear();
    p.end();
    uid = token = "";
    avatar_loaded = "";
    work = {};
    publish_state();
    task_log("mw: signed out");
}

static void mark_expired() {
    Preferences p;
    p.begin("mw", false);
    p.putBool("expired", true);
    p.end();
    task_log("mw: token rejected - sign in again");
    set_link(MW_EXPIRED);
}

// The image CDN resizes on request: a 44px PNG (the on-screen size) is a few KB
// instead of the 450 KB original.
static bool fetch_avatar() {
    WiFiClientSecure net;
    net.setCACert(BAMBU_API_ROOT_CA);   // same DigiCert root as the API
    HTTPClient http;
    http.setTimeout(12000);
    http.setUserAgent("Bambooboo/1.1");
    String url = avatar_url + "?x-oss-process=image/resize,w_44,h_44/format,png";
    if (!http.begin(net, url)) { task_log("mw: avatar url rejected"); return false; }
    int st = http.GET();
    int len = http.getSize();
    const char* why = nullptr;
    if (st != 200)                       why = "http";
    else if (len <= 0 || len >= 65536)   why = "size";
    uint8_t* png = why ? nullptr : (uint8_t*)malloc(len);
    if (!why && !png)                    why = "memory";
    if (!why) {
        WiFiClient* in = http.getStreamPtr();
        int got = 0;
        uint32_t t0 = millis();
        while (got < len && millis() - t0 < 10000) {   // TLS hands bytes over in chunks
            int n = in->available() ? in->read(png + got, len - got) : 0;
            if (n > 0) got += n; else delay(5);
        }
        if (got != len)                                   why = "short read";
        else if (!makerworld_set_avatar_png(png, len))    why = "decode";
    }
    free(png);
    http.end();
    if (why) task_log("mw: avatar download failed (%s, status %d, size %d)", why, st, len);
    return !why;
}

// Returns true when the numbers were refreshed.
static bool fetch() {
    String resp;
    JsonDocument filter;
    filter["fanCount"] = true;
    filter["likeCount"] = true;
    filter["downloadCount"] = true;
    filter["point"] = true;
    filter["name"] = true;
    filter["avatar"] = true;

    MakerWorldState next = work;
    if (token.length()) {
        int st = api("/v1/user-service/my/profile", nullptr, &resp);
        if (st == 401) { mark_expired(); return false; }
        JsonDocument d;
        if (st != 200 || deserializeJson(d, resp, DeserializationOption::Filter(filter))) {
            task_log("mw: profile fetch failed (%d)", st);
            return false;
        }
        next.fans = d["fanCount"] | 0;
        next.likes = d["likeCount"] | 0;
        next.downloads = d["downloadCount"] | 0;
        next.points = d["point"] | 0;
        strlcpy(next.name, d["name"] | "", sizeof(next.name));
        avatar_url = d["avatar"] | "";

        // totalIncome = every boost ever received (redeemed or not)
        if (api("/v1/point-service/boost/boostbill?offset=0&limit=1", nullptr, &resp) == 200) {
            JsonDocument b;
            if (!deserializeJson(b, resp)) next.boosts = b["totalIncome"] | 0;
        }
        if (api("/v1/user-service/my/message/count", nullptr, &resp) == 200) {
            JsonDocument m;
            if (!deserializeJson(m, resp)) next.new_comments = m["commentCount"] | 0;
        }
    } else {
        int st = api(("/v1/user-service/user/profile/" + uid).c_str(), nullptr, &resp);
        JsonDocument d;
        if (st != 200 || deserializeJson(d, resp, DeserializationOption::Filter(filter))) {
            task_log("mw: public profile fetch failed (%d)", st);
            return false;
        }
        next.fans = d["fanCount"] | 0;
        next.likes = d["likeCount"] | 0;
        next.downloads = d["downloadCount"] | 0;
        strlcpy(next.name, d["name"] | "", sizeof(next.name));
        avatar_url = d["avatar"] | "";
    }
    if (avatar_url.length() && avatar_url != avatar_loaded && fetch_avatar()) {
        avatar_loaded = avatar_url;
        next.avatar_seq++;
    }
    next.has_points = token.length() > 0;
    next.has_data = true;
    next.link = MW_ONLINE;
    next.seq++;
    work = next;
    publish_state();
    return true;
}

static void mw_task(void*) {
    load_cfg();
    publish_state();
    uint32_t last_fetch = 0;
    bool fetched = false;

    for (;;) {
        if (want_off) { want_off = false; forget(); fetched = false; }

        if (WiFi.status() != WL_CONNECTED) {
            if (want_code || want_login) {   // drop it: the setup page asks again once Wi-Fi is up
                want_code = want_login = false;
                task_log("mw: waiting for Wi-Fi (set up the printer page first)");
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (want_code)  { want_code = false;  send_code(); }
        if (want_login) { want_login = false; login(); fetched = false; }

        if (uid.length() && work.link != MW_EXPIRED &&
            (!fetched || millis() - last_fetch >= POLL_MS)) {
            last_fetch = millis();
            fetched = fetch() || fetched;
            if (!fetched) last_fetch = millis() - POLL_MS + 30000;   // first fetch failed: retry in 30 s
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void makerworld_init(void) {
    lock = xSemaphoreCreateMutex();
    xTaskCreatePinnedToCore(mw_task, "makerworld", 10240, nullptr, 1, nullptr, 0);
}

bool makerworld_get(MakerWorldState* out) {
    xSemaphoreTake(lock, portMAX_DELAY);
    *out = shared;
    xSemaphoreGive(lock);
    bool changed = out->seq != last_seen_seq || out->link != last_seen_link;
    last_seen_seq = out->seq;
    last_seen_link = out->link;
    return changed;
}

bool makerworld_cmd(const char* json) {
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    if (doc["off"] | false) { want_off = true; return true; }
    if (doc["email"].is<const char*>() && doc["code"].is<const char*>()) {   // both at once: sign in, no new code
        strlcpy(pending_email, doc["email"], sizeof(pending_email));
    } else if (doc["email"].is<const char*>()) {
        strlcpy(pending_email, doc["email"], sizeof(pending_email));
        want_code = true;
        return true;
    }
    if (doc["code"].is<const char*>()) {
        if (!pending_email[0]) { task_log("mw: send the email first"); return true; }
        strlcpy(pending_code, doc["code"], sizeof(pending_code));
        want_points = doc["points"] | false;
        want_login = true;
        return true;
    }
    return false;
}

#else
// Boards built without the network stack: no MakerWorld page.
static MakerWorldState none = {};
void makerworld_init(void) {}
bool makerworld_get(MakerWorldState* out) { *out = none; return false; }
bool makerworld_cmd(const char* json) { (void)json; return false; }
#endif
