#include "ncm_descriptor.h"
/* All multi-byte fields are little endian. FS endpoint sizes are patched to HS at reset.
 * NCM 1.0, NTB16 only; CDC Ethernet descriptor carries the MAC string index. */
const uint8_t g_ncmCompositeConfiguration[] = {
  /* Configuration: four interfaces, 100 mA, self-powered */
  9,2, 160,0, 4,1,0, 0xC0,50,
  /* CDC NCM function (interfaces 0 and 1) */
  8,11, 0,2, 2,0x0D,0,0,
  9,4, 0,0,1, 2,0x0D,0,0,
  5,0x24,0, 0x20,0x01, /* CDC header v1.20 */
  5,0x24,6, 0,1,       /* Union */
  13,0x24,0x0F, 4, 0,0,0,0, 0xEA,0x05, 0,0,0, /* Ethernet, 1514-byte segments */
  6,0x24,0x1A, 0x00,0x01, 0x00, /* NCM 1.0, NTB16 */
  7,5, 0x82,3, 16,0,8, /* NCM notifications */
  9,4, 1,0,0, 10,0,1,0, /* NCM data alt 0: no endpoints */
  9,4, 1,1,2, 10,0,1,0, /* NCM data alt 1: EP4 bulk pair */
  7,5, 0x84,2, 64,0,0,
  7,5, 0x04,2, 64,0,0,
  /* CDC ACM function (interfaces 2 and 3) */
  8,11, 2,2, 2,2,0,0,
  9,4, 2,0,1, 2,2,0,0,
  5,0x24,0, 0x10,0x01,
  5,0x24,1, 1,3,
  4,0x24,2, 6,
  5,0x24,6, 2,3,
  7,5, 0x81,3, 16,0,8,
  9,4, 3,0,2, 10,0,0,0,
  7,5, 0x83,2, 64,0,0,
  7,5, 0x03,2, 64,0,0,
};
const uint16_t g_ncmCompositeConfigurationLength = sizeof(g_ncmCompositeConfiguration);
// if changed, check the following works on Android!
const uint8_t g_ncmMacStringDescriptor[] = {
  26,3, '0',0,'0',0,'0',0,'4',0,'9',0,'F',0,'0',0,'0',0,'0',0,'0',0,'0',0,'1',0
};
