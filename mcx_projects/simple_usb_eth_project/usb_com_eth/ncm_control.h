#ifndef NCM_CONTROL_H
#define NCM_CONTROL_H
#include <stdint.h>
/* USB CDC-NCM class requests. These helpers are transport-independent;
 * the USB EP0 adapter must validate direction, interface and setup stage. */
#define NCM_REQ_SET_ETHERNET_PACKET_FILTER 0x43u
#define NCM_REQ_GET_NTB_PARAMETERS 0x80u
#define NCM_REQ_GET_NTB_INPUT_SIZE 0x85u
#define NCM_REQ_SET_NTB_INPUT_SIZE 0x86u
#define NCM_REQ_GET_NTB_FORMAT 0x83u
#define NCM_REQ_SET_NTB_FORMAT 0x84u
#define NCM_REQ_GET_CRC_MODE 0x89u
#define NCM_REQ_SET_CRC_MODE 0x8Au
#define NCM_NTB_IN_MAX 2048u
#define NCM_NTB_OUT_MAX 2048u
#define NCM_NTB_MIN 64u
#define NCM_NTB_PARAMETERS_LENGTH 28u
/* Return response length, 0 for unsupported/invalid requests. */
uint16_t NCM_ControlGet(uint8_t request, uint8_t *reply, uint16_t capacity);
/* Return 1 on accepted setting, 0 on invalid/unsupported. */
int NCM_ControlSet(uint8_t request, const uint8_t *payload, uint16_t length);
uint32_t NCM_ControlInputSize(void);
uint16_t NCM_ControlPacketFilter(void);
void NCM_ControlReset(void);
#endif
