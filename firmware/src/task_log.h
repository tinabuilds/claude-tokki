#pragma once

// Log lines from background tasks (printer, MakerWorld). USB serial isn't safe
// to write from two cores at once — lines came out cut short ("mw: cod"), which
// broke the setup page that waits for them — so tasks queue their lines and the
// main loop is the only writer.
void task_log_init(void);
void task_log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void task_log_drain(void);   // main loop: print whatever is queued
