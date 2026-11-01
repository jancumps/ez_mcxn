/* Minimal CDC-NCM transport for enumeration / link bring-up. No IP stack yet. */
#include <string.h>
#include <stdio.h>
#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"
#include "usb_device_ch9.h"
#include "ncm_control.h"
#include "ncm_device.h"
#include "ncm_ntb16.h"
#include "ncm_lwip.h"

#define NCM_NOTIFY_EP 0x82U
#define NCM_IN_EP 0x84U
#define NCM_OUT_EP 0x04U
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t s_rx[2048];
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t s_reply[32];
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t s_notification[16];
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t s_tx[2048];
static volatile uint16_t s_rxLength;
static volatile uint8_t s_rxPending;
static uint16_t s_sequence;
static volatile uint8_t s_txBusy;
#define NCM_TX_QUEUE 4U
static uint8_t s_txFrames[NCM_TX_QUEUE][1514];
static uint16_t s_txLengths[NCM_TX_QUEUE];
static uint8_t s_txHead, s_txTail, s_txCount;

static ncm_transport_diag_t s_transportDiag;
/* Foreground-only last-32 USB IN submissions; retained until reset. */
#define NCM_TRACE_DEPTH 32U
typedef struct {
    uint32_t ordinal, tcpSeq, tcpAck;
    uint16_t ntbSeq, ntbLen, frameLen;
    uint8_t tcpFlags;
} ncm_tx_trace_t;
static ncm_tx_trace_t s_txTrace[NCM_TRACE_DEPTH];
static uint32_t s_txTraceCount;
static uint32_t NCM_ReadBE32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static void NCM_RecordTx(uint16_t ntbSeq, uint16_t ntbLen, const uint8_t *f, uint16_t flen)
{
    ncm_tx_trace_t *t=&s_txTrace[s_txTraceCount % NCM_TRACE_DEPTH];
    uint32_t ihl, tcp;
    memset(t,0,sizeof(*t));
    t->ordinal=s_transportDiag.txFrames + 1U;
    t->ntbSeq=ntbSeq; t->ntbLen=ntbLen; t->frameLen=flen;
    if (flen < 54U || f[12]!=8U || f[13]!=0U || (f[14]>>4)!=4U || f[23]!=6U) goto done;
    ihl=(uint32_t)(f[14]&15U)*4U; tcp=14U+ihl;
    if (ihl<20U || flen<tcp+20U) goto done;
    t->tcpSeq=NCM_ReadBE32(f+tcp+4U);
    t->tcpAck=NCM_ReadBE32(f+tcp+8U);
    t->tcpFlags=f[tcp+13U];
done:
    ++s_txTraceCount;
}
void NCM_DeviceDumpTxTrace(void)
{
    uint32_t total=s_txTraceCount<NCM_TRACE_DEPTH?s_txTraceCount:NCM_TRACE_DEPTH;
    uint32_t n=total<8U?total:8U; /* Print at most 8 records to prevent overflowing CDC VCOM */
    uint32_t start=s_txTraceCount-n, i;
    for(i=0;i<n;++i)
    {
        const ncm_tx_trace_t *t=&s_txTrace[(start+i)%NCM_TRACE_DEPTH];
        printf("[TXTRACE] n=%lu ntbSeq=%u ntbLen=%u ethLen=%u tcpSeq=%lu tcpAck=%lu flags=%02x\r\n",
               (unsigned long)t->ordinal,(unsigned)t->ntbSeq,(unsigned)t->ntbLen,
               (unsigned)t->frameLen,(unsigned long)t->tcpSeq,
               (unsigned long)t->tcpAck,(unsigned)t->tcpFlags);
    }
}

static uint8_t s_configured, s_alt, s_notifyBusy;
static uint8_t s_notificationPhase;

static usb_status_t NCM_InDone(usb_device_handle h, usb_device_endpoint_callback_message_struct_t *m, void *p)
{
    (void)h; (void)p;
    s_txBusy = 0;
    if (m && m->length != USB_CANCELLED_TRANSFER_LENGTH)
    {
        ++s_transportDiag.txDone;
    }
    else
    {
        ++s_transportDiag.txSubmitFail;
    }
    return kStatus_USB_Success;
}
static usb_status_t NCM_OutDone(usb_device_handle h, usb_device_endpoint_callback_message_struct_t *m, void *p)
{
    (void)h; (void)p;
    /* Only capture the length here. lwIP must run in the foreground, never in the USB ISR. */
    ++s_transportDiag.rxDone;
    if (s_alt && m->length != USB_CANCELLED_TRANSFER_LENGTH && m->length <= sizeof(s_rx))
    {
        if (m->length >= 8U) s_transportDiag.rxSeqLast = (uint32_t)s_rx[6] | ((uint32_t)s_rx[7] << 8);
        s_rxLength = (uint16_t)m->length;
        s_rxPending = 1U;
    }
    else ++s_transportDiag.rxBadLen;
    return kStatus_USB_Success;
}

/* Called from foreground only. lwIP copies each frame into its own pbuf. */
static int NCM_ReceiveFrame(const uint8_t *frame, uint16_t length, void *context)
{
    (void)context;
    ++s_transportDiag.rxFrames;
    /* Ethernet II / IPv4 / TCP: count ACKs even if lwIP later rejects them. */
    if (length >= 54U && frame[12] == 0x08U && frame[13] == 0x00U &&
        (frame[14] >> 4) == 4U && frame[23] == 6U)
    {
        uint32_t ihl = (uint32_t)(frame[14] & 15U) * 4U;
        if (ihl >= 20U && length >= 14U + ihl + 20U)
        {
            uint32_t tcp = 14U + ihl;
            if (frame[tcp + 13U] & 0x10U) ++s_transportDiag.rxTcpAck;
            if (length > tcp + (uint32_t)(frame[tcp + 12U] >> 4) * 4U)
                ++s_transportDiag.rxTcpData;
        }
    }
    NCM_LwipReceive(frame, length);
    return 1;
}

void NCM_DevicePoll(usb_device_handle handle)
{
    uint16_t len;
    if (!s_alt) return;
    if (s_rxPending)
    {
        uint32_t frames;
        len = s_rxLength;
        s_rxPending = 0U;
        frames = NCM_NTB16_ForEach(s_rx, len, NCM_ReceiveFrame, NULL);
        if (!frames) ++s_transportDiag.rxDecodeFail;
        else
        {
            ++s_transportDiag.rxNtbs;
            if (frames > 1U) { ++s_transportDiag.rxMultiNtbs; s_transportDiag.rxExtraFrames += frames - 1U; }
        }
        if (s_alt && USB_DeviceRecvRequest(handle, NCM_OUT_EP, s_rx, sizeof(s_rx)) != kStatus_USB_Success)
            ++s_transportDiag.rxSubmitFail;
    }
    if (!s_txBusy && s_txCount)
    {
        uint32_t ntbLen = NCM_NTB16_Encode(s_tx, sizeof(s_tx),
            s_txFrames[s_txTail], s_txLengths[s_txTail], s_sequence);
        if (ntbLen)
        {
            s_txBusy = 1U;
            if (USB_DeviceSendRequest(handle,NCM_IN_EP,s_tx,ntbLen)==kStatus_USB_Success)
            {
                NCM_RecordTx(s_sequence,(uint16_t)ntbLen,s_txFrames[s_txTail],s_txLengths[s_txTail]);
                s_transportDiag.txSeqLast = s_sequence;
                ++s_transportDiag.txFrames;
                ++s_sequence;
                s_txTail=(uint8_t)((s_txTail+1U)%NCM_TX_QUEUE);
                --s_txCount;
            }
            else { s_txBusy=0U; ++s_transportDiag.txSubmitFail; }
        }
    }
}

int NCM_DeviceSendFrame(usb_device_handle handle, const uint8_t *frame, uint16_t len)
{
    if (!s_alt || !handle || !frame || len > 1514U || !len || s_txCount == NCM_TX_QUEUE)
        return 0;
    memcpy(s_txFrames[s_txHead],frame,len);
    s_txLengths[s_txHead]=len;
    s_txHead=(uint8_t)((s_txHead+1U)%NCM_TX_QUEUE);
    ++s_txCount;
    if (s_txCount > s_transportDiag.txQueuePeak) s_transportDiag.txQueuePeak = s_txCount;
    return 1;
}

int NCM_DeviceLinkActive(void) { return s_alt != 0U; }
void NCM_DeviceGetDiagnostics(ncm_transport_diag_t *out)
{
    if (out) *out = s_transportDiag;
}

static usb_status_t NCM_NotifyDone(usb_device_handle h, usb_device_endpoint_callback_message_struct_t *m, void *p)
{
    (void)m; (void)p;
    s_notifyBusy = 0;
    if (s_notificationPhase == 1U && s_alt)
    {
        /* CDC NETWORK_CONNECTION is followed by CDC CONNECTION_SPEED_CHANGE. */
        static const uint8_t speedNotice[16] = {0xA1,0x2A,0,0,2,0,8,0,0,0,0,0,0,0,0,0};
        memcpy(s_notification, speedNotice, sizeof(speedNotice));
        /* Advertise 100 Mbps in both directions (USB HS link, not measured throughput). */
        s_notification[8]=0x00; s_notification[9]=0xE1; s_notification[10]=0xF5; s_notification[11]=0x05;
        s_notification[12]=0x00; s_notification[13]=0xE1; s_notification[14]=0xF5; s_notification[15]=0x05;
        s_notificationPhase = 2U;
        s_notifyBusy = 1U;
        USB_DeviceSendRequest(h, NCM_NOTIFY_EP, s_notification, 16U);
    }
    return kStatus_USB_Success;
}
void NCM_DeviceReset(usb_device_handle handle)
{
    (void)handle; s_configured = s_alt = s_notifyBusy = s_notificationPhase = s_txBusy = 0; s_sequence=0; s_txTraceCount=0; s_rxPending=0; s_txHead=s_txTail=s_txCount=0;
    NCM_ControlReset();
}
usb_status_t NCM_DeviceSetAlt(usb_device_handle handle, uint8_t speed, uint8_t alt)
{
    usb_device_endpoint_init_struct_t ep;
    usb_device_endpoint_callback_struct_t cb;
    usb_status_t status;
    if (!s_configured || alt > 1U) return kStatus_USB_InvalidRequest;
    if (s_alt) { USB_DeviceDeinitEndpoint(handle, NCM_IN_EP); USB_DeviceDeinitEndpoint(handle, NCM_OUT_EP); s_alt=0; }
    s_txBusy=0; s_rxPending=0; s_txHead=s_txTail=s_txCount=0;
    if (!alt) return kStatus_USB_Success;
    ep.zlt=0; ep.interval=0; ep.transferType=USB_ENDPOINT_BULK;
    ep.maxPacketSize = (speed == USB_SPEED_HIGH) ? 512U : 64U;
    ep.endpointAddress=NCM_IN_EP; cb.callbackFn=NCM_InDone; cb.callbackParam=NULL;
    status=USB_DeviceInitEndpoint(handle,&ep,&cb); if (status != kStatus_USB_Success) return status;
    ep.endpointAddress=NCM_OUT_EP; cb.callbackFn=NCM_OutDone;
    status=USB_DeviceInitEndpoint(handle,&ep,&cb);
    if (status != kStatus_USB_Success) { USB_DeviceDeinitEndpoint(handle,NCM_IN_EP); return status; }
    s_alt=1;
    status=USB_DeviceRecvRequest(handle,NCM_OUT_EP,s_rx,sizeof(s_rx));
    if (status != kStatus_USB_Success) return status;
    if (!s_notifyBusy)
    {
        memset(s_notification,0,16);
        s_notification[0]=0xA1; s_notification[1]=0x00;
        s_notification[2]=1; s_notification[4]=2;
        s_notifyBusy=1; s_notificationPhase=1;
        USB_DeviceSendRequest(handle,NCM_NOTIFY_EP,s_notification,8);
    }
    return kStatus_USB_Success;
}
usb_status_t NCM_DeviceConfigure(usb_device_handle handle, uint8_t speed, uint8_t configuration)
{
    usb_device_endpoint_init_struct_t ep;
    usb_device_endpoint_callback_struct_t cb;
    if (s_configured) { NCM_DeviceSetAlt(handle,speed,0); USB_DeviceDeinitEndpoint(handle,NCM_NOTIFY_EP); }
    s_configured=0; s_alt=0;
    if (!configuration) return kStatus_USB_Success;
    ep.zlt=0; ep.transferType=USB_ENDPOINT_INTERRUPT; ep.endpointAddress=NCM_NOTIFY_EP;
    ep.maxPacketSize=16; ep.interval=(speed==USB_SPEED_HIGH)?7:8;
    cb.callbackFn=NCM_NotifyDone; cb.callbackParam=NULL;
    if (USB_DeviceInitEndpoint(handle,&ep,&cb)!=kStatus_USB_Success) return kStatus_USB_Error;
    s_configured=1;
    return kStatus_USB_Success;
}
usb_status_t NCM_DeviceClassRequest(usb_setup_struct_t *setup, uint32_t *length, uint8_t **buffer)
{
    uint16_t n;
    if ((setup->wIndex & 0xffU)!=2U || (setup->wIndex >> 8)!=0U) return kStatus_USB_InvalidRequest;
    if ((setup->bmRequestType & 0x1fU)!=1U) return kStatus_USB_InvalidRequest;
    if (setup->bmRequestType & 0x80U)
    {
        n=NCM_ControlGet(setup->bRequest,s_reply,sizeof(s_reply));
        if (!n) return kStatus_USB_InvalidRequest;
        *length=n; *buffer=s_reply; return kStatus_USB_Success;
    }
    if ((setup->bRequest==NCM_REQ_SET_ETHERNET_PACKET_FILTER ||
         setup->bRequest==NCM_REQ_SET_NTB_FORMAT || setup->bRequest==NCM_REQ_SET_CRC_MODE) &&
        setup->wLength==0U && setup->wValue==0U)
    { *length=0; return kStatus_USB_Success; }
    if (setup->wLength && *buffer && *length==setup->wLength &&
        NCM_ControlSet(setup->bRequest,*buffer,(uint16_t)*length))
    { *length=0; return kStatus_USB_Success; }
    return kStatus_USB_InvalidRequest;
}
