#include "printer.h"
#include <ArduinoJson.h>
#include <string.h>
#include <stdlib.h>

// P2S pushes a full ~19 KB report about once a second. The filter keeps only
// the fields the screen shows, so the parsed document stays a few hundred bytes.
static JsonDocument make_filter() {
    JsonDocument f;
    JsonObject p = f["print"].to<JsonObject>();
    for (const char* k : {"gcode_state", "mc_percent", "mc_remaining_time", "layer_num",
                          "total_layer_num", "subtask_name", "nozzle_temper",
                          "nozzle_target_temper", "bed_temper", "bed_target_temper",
                          "print_error"}) {
        p[k] = true;
    }
    p["ams"]["tray_now"] = true;
    JsonObject tray = p["ams"]["ams"][0]["tray"][0].to<JsonObject>();
    tray["id"] = true;
    tray["tray_color"] = true;
    tray["tray_type"] = true;
    return f;
}

static int as_int(JsonVariantConst v) {
    if (v.is<const char*>()) return atoi(v.as<const char*>());   // tray_now arrives as "1"
    return (int)(v.as<float>() + 0.5f);
}

bool printer_parse_report(const char* json, size_t len, PrinterState* st) {
    static JsonDocument filter = make_filter();
    JsonDocument doc;
    if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter))) return false;
    JsonObjectConst p = doc["print"];
    if (p.isNull()) return false;

    if (p["gcode_state"].is<const char*>()) {
        strncpy(st->gcode_state, p["gcode_state"].as<const char*>(), sizeof(st->gcode_state) - 1);
        st->gcode_state[sizeof(st->gcode_state) - 1] = '\0';
    }
    if (!p["mc_percent"].isNull())           st->pct = as_int(p["mc_percent"]);
    if (!p["mc_remaining_time"].isNull())    st->remain_min = as_int(p["mc_remaining_time"]);
    if (!p["layer_num"].isNull())            st->layer = as_int(p["layer_num"]);
    if (!p["total_layer_num"].isNull())      st->total_layer = as_int(p["total_layer_num"]);
    if (!p["nozzle_temper"].isNull())        st->nozzle = as_int(p["nozzle_temper"]);
    if (!p["nozzle_target_temper"].isNull()) st->nozzle_target = as_int(p["nozzle_target_temper"]);
    if (!p["bed_temper"].isNull())           st->bed = as_int(p["bed_temper"]);
    if (!p["bed_target_temper"].isNull())    st->bed_target = as_int(p["bed_target_temper"]);
    if (!p["print_error"].isNull())          st->print_error = as_int(p["print_error"]);
    if (p["subtask_name"].is<const char*>()) {
        strncpy(st->task, p["subtask_name"].as<const char*>(), sizeof(st->task) - 1);
        st->task[sizeof(st->task) - 1] = '\0';
    }

    JsonObjectConst ams = p["ams"];
    if (!ams.isNull()) {
        if (!ams["tray_now"].isNull()) {
            int t = as_int(ams["tray_now"]);
            st->tray_now = (t >= 0 && t < PRINTER_TRAYS) ? t : -1;   // 254 external, 255 none
        }
        JsonArrayConst trays = ams["ams"][0]["tray"];
        if (!trays.isNull()) {
            st->tray_count = 0;
            for (JsonObjectConst t : trays) {
                int i = t["id"].isNull() ? st->tray_count : as_int(t["id"]);
                if (i < 0 || i >= PRINTER_TRAYS) continue;
                // tray_color is RRGGBBAA; an empty slot has no tray_type
                const char* col = t["tray_color"] | "";
                char rgb[7] = {0};
                strncpy(rgb, col, 6);
                st->tray_rgb[i] = (uint32_t)strtoul(rgb, nullptr, 16);
                strncpy(st->tray_type[i], t["tray_type"] | "", sizeof(st->tray_type[i]) - 1);
                st->tray_type[i][sizeof(st->tray_type[i]) - 1] = '\0';
                st->tray_loaded[i] = st->tray_type[i][0] != '\0';
                if (i + 1 > st->tray_count) st->tray_count = i + 1;
            }
        }
    }

    st->has_report = true;
    st->seq++;
    return true;
}
