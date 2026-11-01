#ifndef NCM_DEVICE_H
#define NCM_DEVICE_H
#include "usb_device.h"
#include "usb_device_ch9.h"
void NCM_DeviceReset(usb_device_handle handle);
usb_status_t NCM_DeviceConfigure(usb_device_handle handle, uint8_t speed, uint8_t configuration);
usb_status_t NCM_DeviceSetAlt(usb_device_handle handle, uint8_t speed, uint8_t alt);
usb_status_t NCM_DeviceClassRequest(usb_setup_struct_t *setup, uint32_t *length, uint8_t **buffer);
void NCM_DevicePoll(usb_device_handle handle);
int NCM_DeviceSendFrame(usb_device_handle handle, const uint8_t *frame, uint16_t len);
int NCM_DeviceLinkActive(void);
typedef struct {
    uint32_t rxDone, rxBadLen, rxDecodeFail, rxFrames, rxTcpAck, rxTcpData;
    uint32_t rxSubmitFail, txDone, txSubmitFail, txFrames, txQueuePeak;
    uint32_t rxSeqLast, txSeqLast;
    uint32_t rxNtbs, rxMultiNtbs, rxExtraFrames;
} ncm_transport_diag_t;
void NCM_DeviceGetDiagnostics(ncm_transport_diag_t *out);
void NCM_DeviceDumpTxTrace(void);
#endif
