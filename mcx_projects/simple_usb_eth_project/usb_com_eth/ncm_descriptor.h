#ifndef NCM_DESCRIPTOR_H
#define NCM_DESCRIPTOR_H
#include <stdint.h>
/* Reference USB configuration descriptor for CDC-ACM (IF 0,1) + CDC-NCM (IF 2,3).
 * The active firmware now uses the same descriptor.
 * The MAC address string descriptor is index 4: ASCII 02:00:00:00:00:01, no colons. */
extern const uint8_t g_ncmCompositeConfiguration[];
extern const uint16_t g_ncmCompositeConfigurationLength;
extern const uint8_t g_ncmMacStringDescriptor[];
#endif
