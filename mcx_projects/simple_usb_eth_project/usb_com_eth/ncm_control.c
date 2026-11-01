#include "ncm_control.h"
#include <string.h>
static uint32_t input_size = NCM_NTB_IN_MAX;
static uint16_t packet_filter;
static void put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) { put16(p,(uint16_t)v); put16(p+2,(uint16_t)(v>>16)); }
static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
void NCM_ControlReset(void) { input_size=NCM_NTB_IN_MAX; packet_filter=0; }
uint32_t NCM_ControlInputSize(void) { return input_size; }
uint16_t NCM_ControlPacketFilter(void) { return packet_filter; }
uint16_t NCM_ControlGet(uint8_t request, uint8_t *reply, uint16_t capacity)
{
    if (!reply) return 0;
    switch (request) {
    case NCM_REQ_GET_NTB_PARAMETERS:
        if (capacity < NCM_NTB_PARAMETERS_LENGTH) return 0;
        memset(reply,0,NCM_NTB_PARAMETERS_LENGTH);
        put16(reply, NCM_NTB_PARAMETERS_LENGTH);
        put16(reply+2, 1); /* NTB16 only */
        put32(reply+4, NCM_NTB_IN_MAX);
        put16(reply+8, 4); /* IN divisor */
        put16(reply+10, 0); /* IN remainder */
        put16(reply+12, 4); /* IN alignment */
        put16(reply+14, 0); /* reserved */
        put32(reply+16, NCM_NTB_OUT_MAX);
        put16(reply+20, 4); /* OUT divisor */
        put16(reply+22, 0); /* OUT remainder */
        put16(reply+24, 4); /* OUT alignment */
        put16(reply+26, 1); /* max OUT datagrams */
        return NCM_NTB_PARAMETERS_LENGTH;
    case NCM_REQ_GET_NTB_INPUT_SIZE:
        if (capacity < 4) return 0;
        put32(reply,input_size); return 4;
    case NCM_REQ_GET_NTB_FORMAT:
    case NCM_REQ_GET_CRC_MODE:
        if (capacity < 2) return 0;
        put16(reply,0); return 2;
    default: return 0;
    }
}
int NCM_ControlSet(uint8_t request, const uint8_t *payload, uint16_t length)
{
    uint32_t v;
    switch (request) {
    case NCM_REQ_SET_NTB_INPUT_SIZE:
        if (!payload || (length != 4 && length != 8)) return 0;
        v=get32(payload);
        if (v < NCM_NTB_MIN || v > NCM_NTB_IN_MAX) return 0;
        if (length == 8 && (payload[4] || payload[5] || payload[6] || payload[7])) return 0;
        input_size=v; return 1;
    case NCM_REQ_SET_NTB_FORMAT:
    case NCM_REQ_SET_CRC_MODE:
        return payload && length==2 && payload[0]==0 && payload[1]==0;
    default: return 0;
    }
}
