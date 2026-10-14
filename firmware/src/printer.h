#pragma once
#include <stdint.h>
#include <stddef.h>

// Bambu printer status, read straight from the printer over LAN MQTT
// (port 8883, user "bblp" + the access code on the printer's screen).
// No cloud, no Mac in the loop. Hardware: printer.cpp. Sim: boards/sim/printer_sim.cpp.

enum printer_link_t {
    PRINTER_UNCONFIGURED = 0,   // no Wi-Fi / printer settings stored yet
    PRINTER_WIFI,               // joining Wi-Fi
    PRINTER_CONNECTING,         // Wi-Fi up, opening MQTT
    PRINTER_ONLINE,             // reports flowing
    PRINTER_AUTH_FAILED,        // printer rejected the access code
};

#define PRINTER_TRAYS 4

struct PrinterState {
    printer_link_t link;
    bool     has_report;            // at least one report parsed since boot
    char     gcode_state[12];       // IDLE / PREPARE / RUNNING / PAUSE / FINISH / FAILED
    int      pct;                   // mc_percent
    int      remain_min;            // mc_remaining_time
    int      layer, total_layer;
    char     task[48];              // subtask_name
    int      nozzle, nozzle_target; // degrees C, rounded
    int      bed, bed_target;
    int      print_error;
    int      tray_count;            // trays reported by AMS unit 0
    uint32_t tray_rgb[PRINTER_TRAYS];
    bool     tray_loaded[PRINTER_TRAYS];
    char     tray_type[PRINTER_TRAYS][8];
    int      tray_now;              // active AMS slot 0-3, -1 none / external
    uint32_t seq;                   // bumps on every parsed report
};

// Shared parser (printer_parse.cpp): merge one {"print":{...}} report into *st.
// Keys missing from the report keep their previous value. Returns false on bad JSON.
bool printer_parse_report(const char* json, size_t len, PrinterState* st);

void printer_init(void);
// Copy the latest state. Returns true when it changed since the previous call.
bool printer_get(PrinterState* out);
// Serial "printer {json}" → store settings in NVS and (re)connect.
// Keys: ssid, pass, ip, sn, code. Returns false on bad input.
bool printer_config_cmd(const char* json);
