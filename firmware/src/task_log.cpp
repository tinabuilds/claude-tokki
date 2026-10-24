#include "task_log.h"
#include <Arduino.h>
#include <stdarg.h>

#ifndef BOARD_SIM
static QueueHandle_t q;
struct Line { char s[120]; };

void task_log_init(void) { q = xQueueCreate(16, sizeof(Line)); }

void task_log(const char* fmt, ...) {
    Line l;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(l.s, sizeof(l.s), fmt, ap);
    va_end(ap);
    if (q) xQueueSend(q, &l, pdMS_TO_TICKS(50));
}

void task_log_drain(void) {
    Line l;
    while (q && xQueueReceive(q, &l, 0) == pdTRUE) Serial.println(l.s);
}
#else
// Sim: one thread, print straight away.
void task_log_init(void) {}
void task_log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}
void task_log_drain(void) {}
#endif
