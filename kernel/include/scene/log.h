#ifndef SCENE_LOG_H
#define SCENE_LOG_H

#include <stdint.h>

void scene_log_init(void);
void scene_log_write(const char *text);
void scene_log_line(const char *text);
void scene_log_hex(const char *label, uint64_t value);
void scene_log_u64(const char *label, uint64_t value);
_Noreturn void scene_panic(const char *reason);

#endif

