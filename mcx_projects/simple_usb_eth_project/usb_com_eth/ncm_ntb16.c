#include "ncm_ntb16.h"
#include <string.h>
static void w16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void w32(uint8_t *p, uint32_t v) { w16(p,(uint16_t)v); w16(p+2,(uint16_t)(v>>16)); }
static uint16_t r16(const uint8_t *p) { return (uint16_t)(p[0]|((uint16_t)p[1]<<8)); }
static uint32_t r32(const uint8_t *p) { return (uint32_t)r16(p)|((uint32_t)r16(p+2)<<16); }
uint32_t NCM_NTB16_Encode(uint8_t *out, uint32_t capacity, const uint8_t *frame, uint32_t len, uint16_t seq)
{
    uint32_t total;
    if (!out || !frame || len < 14U || len > NCM_NTB16_MAX_FRAME) return 0;
    total=32U+len;
    if (capacity < total) return 0;
    memset(out,0,32U);
    w32(out,0x484D434EU); /* NCMH */
    w16(out+4,12); w16(out+6,seq); w16(out+8,(uint16_t)total); w16(out+10,12);
    w32(out+12,0x304D434EU); /* NCM0 */
    w16(out+16,16); w16(out+18,0);
    w16(out+20,32); w16(out+22,(uint16_t)len);
    memcpy(out+32,frame,len);
    return total;
}
/* Two passes: reject malformed NDPs/entries before delivering any frames. */
static uint32_t ncm_walk(const uint8_t *ntb, uint32_t length,
                         ncm_ntb16_frame_visitor_t visitor, void *context)
{
    uint32_t block, ndp, count = 0, chain = 0;
    if (!ntb || length < 46U || r32(ntb) != 0x484D434EU || r16(ntb + 4) != 12U)
        return 0;
    block = r16(ntb + 8);
    ndp = r16(ntb + 10);
    if (block > length || block < 46U) return 0;
    while (ndp != 0U)
    {
        uint32_t ndp_len, next, entry;
        if (++chain > block / 16U || ndp < 12U || ndp > block || block - ndp < 16U)
            return 0;
        if (r32(ntb + ndp) != 0x304D434EU) return 0; /* NCM0, no CRC */
        ndp_len = r16(ntb + ndp + 4);
        next = r16(ntb + ndp + 6);
        if (ndp_len < 16U || ndp_len > block - ndp || ((ndp_len - 8U) % 4U) != 0U)
            return 0;
        /* Require a terminating (0,0) entry within the NDP length. */
        for (entry = ndp + 8U; entry + 4U <= ndp + ndp_len; entry += 4U)
        {
            uint32_t offset = r16(ntb + entry);
            uint32_t size = r16(ntb + entry + 2U);
            if (offset == 0U && size == 0U) break;
            if (offset < 12U || size < 14U || size > NCM_NTB16_MAX_FRAME ||
                offset > block || size > block - offset)
                return 0;
            if (visitor && !visitor(ntb + offset, (uint16_t)size, context)) return 0;
            ++count;
        }
        if (entry + 4U > ndp + ndp_len || (r16(ntb + entry) | r16(ntb + entry + 2U)) != 0U)
            return 0;
        ndp = next;
    }
    return count;
}
uint32_t NCM_NTB16_ForEach(const uint8_t *ntb, uint32_t length,
                           ncm_ntb16_frame_visitor_t visitor, void *context)
{
    uint32_t count = ncm_walk(ntb, length, NULL, NULL);
    if (!count) return 0;
    return visitor ? ncm_walk(ntb, length, visitor, context) : count;
}
uint32_t NCM_NTB16_Decode(uint8_t *out, uint32_t capacity, const uint8_t *ntb, uint32_t len)
{
    uint32_t ndp, offset, size;
    if (!out || !NCM_NTB16_ForEach(ntb, len, NULL, NULL)) return 0;
    ndp = r16(ntb + 10);
    offset = r16(ntb + ndp + 8);
    size = r16(ntb + ndp + 10);
    if (size > capacity) return 0;
    memcpy(out, ntb + offset, size);
    return size;
}
