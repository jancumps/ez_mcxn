#ifndef NCM_NTB16_H
#define NCM_NTB16_H
#include <stdint.h>
/* NTB16, no CRC. TX emits one frame; RX accepts multiple NDP entries. */
#define NCM_NTB16_MAX_FRAME 1514U
#define NCM_NTB16_OVERHEAD 32U
#define NCM_NTB16_MAX_BLOCK (NCM_NTB16_MAX_FRAME + NCM_NTB16_OVERHEAD)
/* Returns encoded NTB length, or 0 on invalid input/capacity. */
uint32_t NCM_NTB16_Encode(uint8_t *out, uint32_t capacity, const uint8_t *frame, uint32_t frame_length, uint16_t sequence);
/* Returns Ethernet frame length, or 0 for malformed NTB/capacity. */
uint32_t NCM_NTB16_Decode(uint8_t *out, uint32_t capacity, const uint8_t *ntb, uint32_t ntb_length);
/* Validates the complete NTB (including chained NDPs) before calling visitor.
 * The frame pointers are valid only during the callback; do not retain them.
 * Returns number of Ethernet frames, or 0 if malformed or visitor rejects a frame.
 * A null visitor performs validation only. */
typedef int (*ncm_ntb16_frame_visitor_t)(const uint8_t *frame, uint16_t length, void *context);
uint32_t NCM_NTB16_ForEach(const uint8_t *ntb, uint32_t length,
                           ncm_ntb16_frame_visitor_t visitor, void *context);
#endif
