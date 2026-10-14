// Sim stand-in for printer.cpp: no Wi-Fi/MQTT. SIM_PRINTER=<file> loads one
// captured {"print":{...}} report through the real parser; unset = unconfigured.
// SIM_PRINTER_LINK=<n> forces a printer_link_t (e.g. 4 = auth failed).
#include "printer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PrinterState st = {};
static bool fresh = true;

void printer_init(void) {
    st.tray_now = -1;
    const char* path = getenv("SIM_PRINTER");
    if (path) {
        FILE* f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            char* buf = (char*)malloc(n);
            if (buf && fread(buf, 1, n, f) == (size_t)n && printer_parse_report(buf, n, &st))
                st.link = PRINTER_ONLINE;
            else
                printf("[sim] printer: could not parse %s\n", path);
            free(buf);
            fclose(f);
        }
    }
    const char* link = getenv("SIM_PRINTER_LINK");
    if (link) st.link = (printer_link_t)atoi(link);
}

bool printer_get(PrinterState* out) {
    *out = st;
    bool changed = fresh;
    fresh = false;
    return changed;
}

bool printer_config_cmd(const char* json) { (void)json; return false; }
