#pragma once
#include "data.h"
#include "ble.h"
#include "printer.h"
#include "makerworld.h"

enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    SCREEN_PRINTER,
    SCREEN_COUNT,
};

void ui_init(void);
void ui_update(const UsageData* data);
void ui_tick_anim(void);
void ui_show_screen(screen_t screen);
void ui_toggle_splash(void);
screen_t ui_get_current_screen(void);
void ui_update_ble_status(ble_state_t state, const char* name, const char* mac);
void ui_update_battery(int percent, bool charging, bool usb_power);  // percent < 0 = no battery
// Attention: mascot walk-on plus the message on the status line. Ends on a
// tap, after 2 minutes, or ui_hide_alert() when the session moves on.
void ui_show_alert(const char* message);
void ui_hide_alert(void);
// Printer page (Bambu status over LAN). ui_printer_event() jumps to it and
// pulses the ring — called when a print finishes or fails.
void ui_update_printer(const PrinterState* ps);
void ui_printer_event(void);
// MakerWorld stats live on the printer page (title-slot line while printing,
// the big number when idle, an envelope for unread comments + replies).
// ui_makerworld_event() brings that page up when a new comment lands.
void ui_update_makerworld(const MakerWorldState* ms);
void ui_makerworld_event(void);
