#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Single instance. All calls must use the same serial worker queue.
bool X68SC55_Load(const uint8_t *program1, size_t size1,
                 const uint8_t *program2, size_t size2,
                 const uint8_t *wave1, const uint8_t *wave2,
                 const uint8_t *wave3, size_t waveSize);
void X68SC55_Reset(void);
bool X68SC55_Send(const uint8_t *bytes, size_t count);
bool X68SC55_Render(float *left, float *right, size_t frames);
#ifdef __cplusplus
}
#endif
