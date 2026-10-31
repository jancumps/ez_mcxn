/*
 * Copyright 2017, 2019 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <stdio.h>
#include <stdlib.h>
/*${standard_header_anchor}*/
#include "fsl_device_registers.h"
#include "clock_config.h"
#include "fsl_debug_console.h"
#include "board.h"

#include "usb_device_config.h"
#include "usb.h"
#include "usb_device.h"

#include "usb_device_cdc_acm.h"
#include "usb_device_ch9.h"

#include "usb_device_descriptor.h"
#include "composite.h"
#include "usb_console.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

/* Line coding of cdc device */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t s_lineCoding[USB_DEVICE_CONFIG_CDC_ACM][LINE_CODING_SIZE] = {
    {/* E.g. 0x00,0xC2,0x01,0x00 : 0x0001C200 is 115200 bits per second */
     (LINE_CODING_DTERATE >> 0U) & 0x000000FFU, (LINE_CODING_DTERATE >> 8U) & 0x000000FFU,
     (LINE_CODING_DTERATE >> 16U) & 0x000000FFU, (LINE_CODING_DTERATE >> 24U) & 0x000000FFU, LINE_CODING_CHARFORMAT,
     LINE_CODING_PARITYTYPE, LINE_CODING_DATABITS},
    {/* E.g. 0x00,0xC2,0x01,0x00 : 0x0001C200 is 115200 bits per second */
     (LINE_CODING_DTERATE >> 0U) & 0x000000FFU, (LINE_CODING_DTERATE >> 8U) & 0x000000FFU,
     (LINE_CODING_DTERATE >> 16U) & 0x000000FFU, (LINE_CODING_DTERATE >> 24U) & 0x000000FFU, LINE_CODING_CHARFORMAT,
     LINE_CODING_PARITYTYPE, LINE_CODING_DATABITS},
};

/* Abstract state of cdc device */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t s_abstractState[USB_DEVICE_CONFIG_CDC_ACM][COMM_FEATURE_DATA_SIZE] = {
    {(STATUS_ABSTRACT_STATE >> 0U) & 0x00FFU, (STATUS_ABSTRACT_STATE >> 8U) & 0x00FFU},
    {(STATUS_ABSTRACT_STATE >> 0U) & 0x00FFU, (STATUS_ABSTRACT_STATE >> 8U) & 0x00FFU},
};

/* Country code of cdc device */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static uint8_t s_countryCode[USB_DEVICE_CONFIG_CDC_ACM][COMM_FEATURE_DATA_SIZE] = {
    {(COUNTRY_SETTING >> 0U) & 0x00FFU, (COUNTRY_SETTING >> 8U) & 0x00FFU},
    {(COUNTRY_SETTING >> 0U) & 0x00FFU, (COUNTRY_SETTING >> 8U) & 0x00FFU},
};

/* CDC ACM information */
USB_DMA_INIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE)
static usb_cdc_acm_info_t s_usbCdcAcmInfo[USB_DEVICE_CONFIG_CDC_ACM] = {
    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0, 0, 0, 0, 0},
    {{0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, 0, 0, 0, 0, 0},
};
/* Data buffer for receiving and sending*/
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t s_currRecvBuf[USB_DEVICE_CONFIG_CDC_ACM][DATA_BUFF_SIZE];
USB_DMA_NONINIT_DATA_ALIGN(USB_DATA_ALIGN_SIZE) static uint8_t s_currSendBuf[USB_DEVICE_CONFIG_CDC_ACM][DATA_BUFF_SIZE];
volatile static uint32_t s_recvSize[USB_DEVICE_CONFIG_CDC_ACM] = {0};
volatile static uint32_t s_sendSize[USB_DEVICE_CONFIG_CDC_ACM] = {0};

volatile static usb_device_composite_struct_t *g_deviceComposite;
/* CDC0 console: single main-loop producer/consumer, ISR only signals TX completion. */
#define CONSOLE_TX_CAPACITY 1024U
#define CONSOLE_RX_CAPACITY 512U
static uint8_t s_consoleTx[CONSOLE_TX_CAPACITY];
static uint8_t s_consoleRx[CONSOLE_RX_CAPACITY];
static volatile uint16_t s_txHead, s_txTail, s_rxHead, s_rxTail;
static volatile uint8_t s_consoleTxBusy;

/* CDC1: independent, nonblocking application-facing RX/TX FIFOs. */
#define PORT1_TX_CAPACITY 2048U
#define PORT1_RX_CAPACITY 2048U
static uint8_t s_port1Tx[PORT1_TX_CAPACITY];
static uint8_t s_port1Rx[PORT1_RX_CAPACITY];
static volatile uint16_t s_port1TxHead, s_port1TxTail;
static volatile uint16_t s_port1RxHead, s_port1RxTail;
static volatile uint8_t s_port1TxBusy;

int USB_Port1Ready(void)
{
    return g_deviceComposite != NULL &&
           g_deviceComposite->cdcVcom[1].attach &&
           g_deviceComposite->cdcVcom[1].startTransactions &&
           g_deviceComposite->cdcVcom[1].usbCdcAcmInfo->dtePresent;
}

int USB_Port1Write(const uint8_t *data, unsigned int length)
{
    unsigned int n = 0U;
    if (data == NULL || !USB_Port1Ready()) return 0;
    while (n < length)
    {
        uint16_t next = (uint16_t)((s_port1TxHead + 1U) % PORT1_TX_CAPACITY);
        if (next == s_port1TxTail) break;
        s_port1Tx[s_port1TxHead] = data[n++];
        s_port1TxHead = next;
    }
    return (int)n;
}

int USB_Port1GetChar(void)
{
    int c;
    if (s_port1RxTail == s_port1RxHead) return -1;
    c = s_port1Rx[s_port1RxTail];
    s_port1RxTail = (uint16_t)((s_port1RxTail + 1U) % PORT1_RX_CAPACITY);
    return c;
}


int USB_ConsoleReady(void)
{
    return g_deviceComposite != NULL &&
           g_deviceComposite->cdcVcom[0].attach &&
           g_deviceComposite->cdcVcom[0].startTransactions &&
           g_deviceComposite->cdcVcom[0].usbCdcAcmInfo->dtePresent;
}

int USB_ConsoleWrite(const uint8_t *data, unsigned int length)
{
    unsigned int n = 0;
    if (data == NULL || !USB_ConsoleReady()) return 0;
    while (n < length)
    {
        uint16_t next = (uint16_t)((s_txHead + 1U) % CONSOLE_TX_CAPACITY);
        if (next == s_txTail) break; /* full: caller can retry */
        s_consoleTx[s_txHead] = data[n++];
        s_txHead = next;
    }
    return (int)n;
}

int USB_ConsoleGetChar(void)
{
    int c;
    if (s_rxTail == s_rxHead) return -1;
    c = s_consoleRx[s_rxTail];
    s_rxTail = (uint16_t)((s_rxTail + 1U) % CONSOLE_RX_CAPACITY);
    return c;
}


/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief Interrupt in pipe callback function.
 *
 * This function serves as the callback function for interrupt in pipe.
 *
 * @param handle The USB device handle.
 * @param message The endpoint callback message
 * @param callbackParam The parameter of the callback.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCdcAcmInterruptIn(usb_device_handle handle,
                                         usb_device_endpoint_callback_message_struct_t *message,
                                         void *callbackParam)
{
    usb_status_t error = kStatus_USB_Error;
    uint8_t i;

    if (message->length == USB_CANCELLED_TRANSFER_LENGTH)
    {
        return kStatus_USB_Success;
    }

    for (i = 0; i < USB_DEVICE_CONFIG_CDC_ACM; i++)
    {
        if (*((uint8_t *)callbackParam) == g_deviceComposite->cdcVcom[i].communicationInterfaceNumber)
        {
            break;
        }
    }
    if (i >= USB_DEVICE_CONFIG_CDC_ACM)
    {
        return error;
    }

    g_deviceComposite->cdcVcom[i].hasSentState = 0;
    return kStatus_USB_Success;
}

/*!
 * @brief Bulk in pipe callback function.
 *
 * This function serves as the callback function for bulk in pipe.
 *
 * @param handle The USB device handle.
 * @param message The endpoint callback message
 * @param callbackParam The parameter of the callback.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCdcAcmBulkIn(usb_device_handle handle,
                                    usb_device_endpoint_callback_message_struct_t *message,
                                    void *callbackParam)
{
    volatile usb_cdc_vcom_struct_t *vcomInstance;
    usb_status_t error = kStatus_USB_Error;
    uint8_t i;

    if (message->length == USB_CANCELLED_TRANSFER_LENGTH)
    {
        return kStatus_USB_Success;
    }

    for (i = 0; i < USB_DEVICE_CONFIG_CDC_ACM; i++)
    {
        if (*((uint8_t *)callbackParam) == g_deviceComposite->cdcVcom[i].dataInterfaceNumber)
        {
            break;
        }
    }
    if (i >= USB_DEVICE_CONFIG_CDC_ACM)
    {
        return error;
    }
    vcomInstance = &g_deviceComposite->cdcVcom[i];

    if (message->length == 0U ||
        (message->length % vcomInstance->bulkInEndpointMaxPacketSize) != 0U)
    {
        if (i == 0U) s_consoleTxBusy = 0U;
        if (i == 1U) s_port1TxBusy = 0U;
    }

    if ((message->length != 0) && (!(message->length % vcomInstance->bulkInEndpointMaxPacketSize)))
    {
        /* If the last packet is the size of endpoint, then send also zero-ended packet,
         ** meaning that we want to inform the host that we do not have any additional
         ** data, so it can flush the output.
         */
        USB_DeviceSendRequest(handle, vcomInstance->bulkInEndpoint, NULL, 0);
    }
    else if ((1 == vcomInstance->attach) && (1 == vcomInstance->startTransactions))
    {
        if ((message->buffer != NULL) || ((message->buffer == NULL) && (message->length == 0)))
        {
            /* User: add your own code for send complete event */
            /* Schedule buffer for next receive event */
        }
    }
    else
    {
    }
    return error;
}

/*!
 * @brief Bulk out pipe callback function.
 *
 * This function serves as the callback function for bulk out pipe.
 *
 * @param handle The USB device handle.
 * @param message The endpoint callback message
 * @param callbackParam The parameter of the callback.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCdcAcmBulkOut(usb_device_handle handle,
                                     usb_device_endpoint_callback_message_struct_t *message,
                                     void *callbackParam)
{
    volatile usb_cdc_vcom_struct_t *vcomInstance;
    usb_status_t error = kStatus_USB_Error;
    uint8_t i;

    if (message->length == USB_CANCELLED_TRANSFER_LENGTH)
    {
        return kStatus_USB_Success;
    }

    for (i = 0; i < USB_DEVICE_CONFIG_CDC_ACM; i++)
    {
        if (*((uint8_t *)callbackParam) == g_deviceComposite->cdcVcom[i].dataInterfaceNumber)
        {
            break;
        }
    }
    if (i >= USB_DEVICE_CONFIG_CDC_ACM)
    {
        return error;
    }
    vcomInstance = &g_deviceComposite->cdcVcom[i];

    if ((1 == vcomInstance->attach) && (1 == vcomInstance->startTransactions))
    {
        vcomInstance->recvSize = message->length;

        if (!vcomInstance->recvSize)
        {
            /* Schedule buffer for next receive event */
            USB_DeviceRecvRequest(handle, vcomInstance->bulkOutEndpoint, vcomInstance->currRecvBuf,
                                  vcomInstance->bulkOutEndpointMaxPacketSize);
        }
    }
    return error;
}

/*!
 * @brief USB configure endpoint function.
 *
 * This function configure endpoint status.
 *
 * @param handle The USB device handle.
 * @param ep Endpoint address.
 * @param status A flag to indicate whether to stall the endpoint. 1: stall, 0: unstall.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCdcVcomConfigureEndpointStatus(usb_device_handle handle, uint8_t ep, uint8_t status)
{
    usb_status_t error = kStatus_USB_Error;
    if (status)
    {
        if ((USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT == (ep & USB_ENDPOINT_NUMBER_MASK)) &&
            (ep & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK))
        {
            error = USB_DeviceStallEndpoint(handle, ep);
        }
        else if ((USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT == (ep & USB_ENDPOINT_NUMBER_MASK)) &&
                 (!(ep & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)))
        {
            error = USB_DeviceStallEndpoint(handle, ep);
        }
        else if ((USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT_2 == (ep & USB_ENDPOINT_NUMBER_MASK)) &&
                 (ep & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK))
        {
            error = USB_DeviceStallEndpoint(handle, ep);
        }
        else if ((USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT_2 == (ep & USB_ENDPOINT_NUMBER_MASK)) &&
                 (!(ep & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)))
        {
            error = USB_DeviceStallEndpoint(handle, ep);
        }
        else
        {
        }
    }
    else
    {
        if ((USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT == (ep & USB_ENDPOINT_NUMBER_MASK)) &&
            (ep & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK))
        {
            error = USB_DeviceUnstallEndpoint(handle, ep);
        }
        else if ((USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT == (ep & USB_ENDPOINT_NUMBER_MASK)) &&
                 (!(ep & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)))
        {
            error = USB_DeviceUnstallEndpoint(handle, ep);
        }
        else if ((USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT_2 == (ep & USB_ENDPOINT_NUMBER_MASK)) &&
                 (ep & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK))
        {
            error = USB_DeviceUnstallEndpoint(handle, ep);
        }
        else if ((USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT_2 == (ep & USB_ENDPOINT_NUMBER_MASK)) &&
                 (!(ep & USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_MASK)))
        {
            error = USB_DeviceUnstallEndpoint(handle, ep);
        }
        else
        {
        }
    }
    return error;
}

/*!
 * @brief CDC class specific callback function.
 *
 * This function handles the CDC class specific requests.
 *
 * @param handle The USB device handle.
 * @param setup The pointer to the setup packet.
 * @param length The pointer to the length of the data buffer.
 * @param buffer The pointer to the address of setup packet data buffer.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCdcVcomClassRequest(usb_device_handle handle,
                                           usb_setup_struct_t *setup,
                                           uint32_t *length,
                                           uint8_t **buffer)
{
    usb_status_t error = kStatus_USB_InvalidRequest;
    volatile usb_cdc_vcom_struct_t *vcomInstance;
    usb_cdc_acm_info_t *acmInfo;
    uint32_t len;
    uint8_t *uartBitmap;
    uint8_t i;

    for (i = 0; i < USB_DEVICE_CONFIG_CDC_ACM; i++)
    {
        if (setup->wIndex == g_deviceComposite->cdcVcom[i].communicationInterfaceNumber)
        {
            break;
        }
    }
    if (i >= USB_DEVICE_CONFIG_CDC_ACM)
    {
        return error;
    }
    vcomInstance = &g_deviceComposite->cdcVcom[i];
    acmInfo      = vcomInstance->usbCdcAcmInfo;
    switch (setup->bRequest)
    {
        case USB_DEVICE_CDC_REQUEST_SEND_ENCAPSULATED_COMMAND:
            break;
        case USB_DEVICE_CDC_REQUEST_GET_ENCAPSULATED_RESPONSE:
            break;
        case USB_DEVICE_CDC_REQUEST_SET_COMM_FEATURE:
            if (((setup->bmRequestType & USB_REQUEST_TYPE_DIR_MASK) == USB_REQUEST_TYPE_DIR_OUT) &&
                (setup->wLength != 0U))
            {
                if (USB_DEVICE_CDC_FEATURE_ABSTRACT_STATE == setup->wValue)
                {
                    (void)memcpy(vcomInstance->abstractState, *buffer, COMM_FEATURE_DATA_SIZE);
                    error = kStatus_USB_Success;
                }
                else if (USB_DEVICE_CDC_FEATURE_COUNTRY_SETTING == setup->wValue)
                {
                    (void)memcpy(vcomInstance->countryCode, *buffer, COMM_FEATURE_DATA_SIZE);
                    error = kStatus_USB_Success;
                }
                else
                {
                }
            }
            break;
        case USB_DEVICE_CDC_REQUEST_GET_COMM_FEATURE:
            if (((setup->bmRequestType & USB_REQUEST_TYPE_DIR_MASK) == USB_REQUEST_TYPE_DIR_IN) &&
                (setup->wLength != 0U))
            {
                if (USB_DEVICE_CDC_FEATURE_ABSTRACT_STATE == setup->wValue)
                {
                    *buffer = vcomInstance->abstractState;
                    *length = COMM_FEATURE_DATA_SIZE;
                    error   = kStatus_USB_Success;
                }
                else if (USB_DEVICE_CDC_FEATURE_COUNTRY_SETTING == setup->wValue)
                {
                    *buffer = vcomInstance->countryCode;
                    *length = COMM_FEATURE_DATA_SIZE;
                    error   = kStatus_USB_Success;
                }
                else
                {
                }
            }
            break;
        case USB_DEVICE_CDC_REQUEST_CLEAR_COMM_FEATURE:
            break;
        case USB_DEVICE_CDC_REQUEST_GET_LINE_CODING:
            if (((setup->bmRequestType & USB_REQUEST_TYPE_DIR_MASK) == USB_REQUEST_TYPE_DIR_IN) &&
                (setup->wLength != 0U))
            {
                *buffer = vcomInstance->lineCoding;
                *length = LINE_CODING_SIZE;
                error   = kStatus_USB_Success;
            }
            break;
        case USB_DEVICE_CDC_REQUEST_SET_LINE_CODING:
            if (((setup->bmRequestType & USB_REQUEST_TYPE_DIR_MASK) == USB_REQUEST_TYPE_DIR_OUT) &&
                (setup->wLength != 0U))
            {
                (void)memcpy(vcomInstance->lineCoding, *buffer, LINE_CODING_SIZE);
                error = kStatus_USB_Success;
            }
            break;
        case USB_DEVICE_CDC_REQUEST_SET_CONTROL_LINE_STATE:
        {
            if (((setup->bmRequestType & USB_REQUEST_TYPE_DIR_MASK) == USB_REQUEST_TYPE_DIR_OUT) &&
                (setup->wLength == 0U))
            {
                error              = kStatus_USB_Success;
                acmInfo->dteStatus = setup->wValue;
                /* activate/deactivate Tx carrier */
                if (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_CARRIER_ACTIVATION)
                {
                    acmInfo->uartState |= USB_DEVICE_CDC_UART_STATE_TX_CARRIER;
                }
                else
                {
                    acmInfo->uartState &= (uint16_t)~USB_DEVICE_CDC_UART_STATE_TX_CARRIER;
                }

                /* activate carrier and DTE. Com port of terminal tool running on PC is open now */
                if (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_DTE_PRESENCE)
                {
                    acmInfo->uartState |= USB_DEVICE_CDC_UART_STATE_RX_CARRIER;
                }
                /* Com port of terminal tool running on PC is closed now */
                else
                {
                    acmInfo->uartState &= (uint16_t)~USB_DEVICE_CDC_UART_STATE_RX_CARRIER;
                }

                /* Indicates to DCE if DTE is present or not */
                acmInfo->dtePresent =
                    (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_DTE_PRESENCE) ? true : false;

                /* Initialize the serial state buffer */
                acmInfo->serialStateBuf[0] = NOTIF_REQUEST_TYPE;                        /* bmRequestType */
                acmInfo->serialStateBuf[1] = USB_DEVICE_CDC_REQUEST_SERIAL_STATE_NOTIF; /* bNotification */
                acmInfo->serialStateBuf[2] = 0x00;                                      /* wValue */
                acmInfo->serialStateBuf[3] = 0x00;
                acmInfo->serialStateBuf[4] = 0x00; /* wIndex */
                acmInfo->serialStateBuf[5] = 0x00;
                acmInfo->serialStateBuf[6] = UART_BITMAP_SIZE; /* wLength */
                acmInfo->serialStateBuf[7] = 0x00;
                /* Notify to host the line state */
                acmInfo->serialStateBuf[4] = setup->wIndex;
                /* Lower byte of UART BITMAP */
                uartBitmap    = (uint8_t *)&acmInfo->serialStateBuf[NOTIF_PACKET_SIZE + UART_BITMAP_SIZE - 2];
                uartBitmap[0] = acmInfo->uartState & 0xFFu;
                uartBitmap[1] = (acmInfo->uartState >> 8) & 0xFFu;
                len           = (uint32_t)(NOTIF_PACKET_SIZE + UART_BITMAP_SIZE);
                if (0 == vcomInstance->hasSentState)
                {
                    error =
                        USB_DeviceSendRequest(handle, vcomInstance->interruptEndpoint, acmInfo->serialStateBuf, len);
                    if (kStatus_USB_Success != error)
                    {
                        usb_echo("kUSB_DeviceCdcEventSetControlLineState error!");
                    }
                    vcomInstance->hasSentState = 1;
                }
                else
                {
                }
                /* Update status */
                if (acmInfo->dteStatus & USB_DEVICE_CDC_CONTROL_SIG_BITMAP_CARRIER_ACTIVATION)
                {
                    /*    To do: CARRIER_ACTIVATED */
                }
                else
                {
                    /* To do: CARRIER_DEACTIVATED */
                }

                if (1U == vcomInstance->attach)
                {
                    vcomInstance->startTransactions = 1;
                }
                error = kStatus_USB_Success;
            }
        }
        break;
        case USB_DEVICE_CDC_REQUEST_SEND_BREAK:
            break;
        default:
            break;
    }

    return error;
}

/*!
 * @brief Application task function.
 *
 * This function runs the task for application.
 *
 * @return None.
 */
void USB_DeviceCdcVcomTask(void)
{
    usb_status_t error = kStatus_USB_Error;
    volatile usb_cdc_vcom_struct_t *vcomInstance;

    for (uint8_t i = 0; i < USB_DEVICE_CONFIG_CDC_ACM; i++)
    {
        vcomInstance = &g_deviceComposite->cdcVcom[i];
        if ((1 == vcomInstance->attach) && (1 == vcomInstance->startTransactions))
        {
            if (i == 0U)
            {
                if (vcomInstance->recvSize != 0U &&
                    vcomInstance->recvSize != USB_CANCELLED_TRANSFER_LENGTH)
                {
                    uint32_t count = vcomInstance->recvSize;
                    for (uint32_t j = 0; j < count; ++j)
                    {
                        uint16_t next = (uint16_t)((s_rxHead + 1U) % CONSOLE_RX_CAPACITY);
                        if (next != s_rxTail)
                        {
                            s_consoleRx[s_rxHead] = vcomInstance->currRecvBuf[j];
                            s_rxHead = next;
                        }
                    }
                    vcomInstance->recvSize = 0U;
                    USB_DeviceRecvRequest(g_deviceComposite->deviceHandle,
                                          vcomInstance->bulkOutEndpoint,
                                          vcomInstance->currRecvBuf,
                                          vcomInstance->bulkOutEndpointMaxPacketSize);
                }
                if (!s_consoleTxBusy && s_txHead != s_txTail && USB_ConsoleReady())
                {
                    uint32_t size = 0U;
                    while (s_txTail != s_txHead && size < DATA_BUFF_SIZE &&
                           size < vcomInstance->bulkInEndpointMaxPacketSize)
                    {
                        vcomInstance->currSendBuf[size++] = s_consoleTx[s_txTail];
                        s_txTail = (uint16_t)((s_txTail + 1U) % CONSOLE_TX_CAPACITY);
                    }
                    s_consoleTxBusy = 1U;
                    error = USB_DeviceSendRequest(g_deviceComposite->deviceHandle,
                                                  vcomInstance->bulkInEndpoint,
                                                  vcomInstance->currSendBuf, size);
                    if (error != kStatus_USB_Success)
                    {
                        /* Requeue is not safe after consuming the ring; count as dropped. */
                        s_consoleTxBusy = 0U;
                    }
                }
                continue;
            }
            /* CDC1: transfer completed RX packets to the application FIFO. */
            if (vcomInstance->recvSize != 0U &&
                vcomInstance->recvSize != USB_CANCELLED_TRANSFER_LENGTH)
            {
                uint32_t count = vcomInstance->recvSize;
                for (uint32_t j = 0U; j < count; ++j)
                {
                    uint16_t next = (uint16_t)((s_port1RxHead + 1U) % PORT1_RX_CAPACITY);
                    if (next != s_port1RxTail)
                    {
                        s_port1Rx[s_port1RxHead] = vcomInstance->currRecvBuf[j];
                        s_port1RxHead = next;
                    }
                    /* If RX FIFO is full, excess bytes are dropped. */
                }
                vcomInstance->recvSize = 0U;
                USB_DeviceRecvRequest(g_deviceComposite->deviceHandle,
                                      vcomInstance->bulkOutEndpoint,
                                      vcomInstance->currRecvBuf,
                                      vcomInstance->bulkOutEndpointMaxPacketSize);
            }
            if (!s_port1TxBusy && s_port1TxHead != s_port1TxTail && USB_Port1Ready())
            {
                uint32_t size = 0U;
                while (s_port1TxTail != s_port1TxHead &&
                       size < DATA_BUFF_SIZE &&
                       size < vcomInstance->bulkInEndpointMaxPacketSize)
                {
                    vcomInstance->currSendBuf[size++] = s_port1Tx[s_port1TxTail];
                    s_port1TxTail = (uint16_t)((s_port1TxTail + 1U) % PORT1_TX_CAPACITY);
                }
                s_port1TxBusy = 1U;
                error = USB_DeviceSendRequest(g_deviceComposite->deviceHandle,
                                              vcomInstance->bulkInEndpoint,
                                              vcomInstance->currSendBuf, size);
                if (error != kStatus_USB_Success)
                {
                    /* Failed transfer: bytes already dequeued are lost. */
                    s_port1TxBusy = 0U;
                }
            }
        }
    }
}

/*!
 * @brief Virtual COM device set configuration function.
 *
 * This function sets configuration for CDC class.
 *
 * @param handle The CDC ACM class handle.
 * @param configure The CDC ACM class configure index.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCdcVcomSetConfigure(usb_device_handle handle, uint8_t configure)
{
    usb_device_endpoint_init_struct_t epInitStruct;
    usb_device_endpoint_callback_struct_t epCallback;
    usb_status_t error = kStatus_USB_Error;

    if (g_deviceComposite->currentConfiguration == configure)
    {
        return error;
    }
    if (g_deviceComposite->currentConfiguration)
    {
        for (uint8_t i = 0; i < USB_DEVICE_CONFIG_CDC_ACM; i++)
        {
            USB_DeviceDeinitEndpoint(g_deviceComposite->deviceHandle,
                                     ((g_deviceComposite->cdcVcom[i].interruptEndpoint) |
                                      (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT)));
            USB_DeviceDeinitEndpoint(g_deviceComposite->deviceHandle,
                                     ((g_deviceComposite->cdcVcom[0].bulkInEndpoint) |
                                      (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT)));
            USB_DeviceDeinitEndpoint(g_deviceComposite->deviceHandle,
                                     ((g_deviceComposite->cdcVcom[0].bulkOutEndpoint) |
                                      (USB_OUT << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT)));
        }
    }
    if (USB_COMPOSITE_CONFIGURE_INDEX == configure)
    {
        /* Initiailize cdc 1 endpoint*/
        g_deviceComposite->cdcVcom[0].attach = 1;

        /* Initiailize endpoint for interrupt pipe */
        epCallback.callbackFn    = USB_DeviceCdcAcmInterruptIn;
        epCallback.callbackParam = (void *)&g_deviceComposite->cdcVcom[0].communicationInterfaceNumber;

        epInitStruct.zlt          = 0;
        epInitStruct.transferType = USB_ENDPOINT_INTERRUPT;
        epInitStruct.endpointAddress =
            USB_CDC_VCOM_CIC_INTERRUPT_IN_ENDPOINT | (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT);
        if (USB_SPEED_HIGH == g_deviceComposite->speed)
        {
            epInitStruct.maxPacketSize = HS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE;
            epInitStruct.interval      = HS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
        }
        else
        {
            epInitStruct.maxPacketSize = FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE;
            epInitStruct.interval      = FS_CDC_VCOM_INTERRUPT_IN_INTERVAL;
        }
        g_deviceComposite->cdcVcom[0].interruptEndpoint              = USB_CDC_VCOM_CIC_INTERRUPT_IN_ENDPOINT;
        g_deviceComposite->cdcVcom[0].interruptEndpointMaxPacketSize = epInitStruct.maxPacketSize;
        g_deviceComposite->cdcVcom[0].communicationInterfaceNumber   = USB_CDC_VCOM_CIC_INTERFACE_INDEX;
        USB_DeviceInitEndpoint(handle, &epInitStruct, &epCallback);

        /* Initiailize endpoints for bulk in pipe */
        epCallback.callbackFn    = USB_DeviceCdcAcmBulkIn;
        epCallback.callbackParam = (void *)&g_deviceComposite->cdcVcom[0].dataInterfaceNumber;

        epInitStruct.zlt          = 0;
        epInitStruct.interval     = 0;
        epInitStruct.transferType = USB_ENDPOINT_BULK;
        epInitStruct.endpointAddress =
            USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT | (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT);
        if (USB_SPEED_HIGH == g_deviceComposite->speed)
        {
            epInitStruct.maxPacketSize = HS_CDC_VCOM_BULK_IN_PACKET_SIZE;
        }
        else
        {
            epInitStruct.maxPacketSize = FS_CDC_VCOM_BULK_IN_PACKET_SIZE;
        }
        g_deviceComposite->cdcVcom[0].bulkInEndpoint              = USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT;
        g_deviceComposite->cdcVcom[0].bulkInEndpointMaxPacketSize = epInitStruct.maxPacketSize;
        USB_DeviceInitEndpoint(handle, &epInitStruct, &epCallback);
        /* Initiailize endpoints for bulk out pipe */
        epCallback.callbackFn    = USB_DeviceCdcAcmBulkOut;
        epCallback.callbackParam = (void *)&g_deviceComposite->cdcVcom[0].dataInterfaceNumber;

        epInitStruct.zlt          = 0;
        epInitStruct.interval     = 0;
        epInitStruct.transferType = USB_ENDPOINT_BULK;
        epInitStruct.endpointAddress =
            USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT | (USB_OUT << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT);
        if (USB_SPEED_HIGH == g_deviceComposite->speed)
        {
            epInitStruct.maxPacketSize = HS_CDC_VCOM_BULK_OUT_PACKET_SIZE;
        }
        else
        {
            epInitStruct.maxPacketSize = FS_CDC_VCOM_BULK_OUT_PACKET_SIZE;
        }
        g_deviceComposite->cdcVcom[0].bulkOutEndpoint              = USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT;
        g_deviceComposite->cdcVcom[0].bulkOutEndpointMaxPacketSize = epInitStruct.maxPacketSize;
        USB_DeviceInitEndpoint(handle, &epInitStruct, &epCallback);

        g_deviceComposite->cdcVcom[0].dataInterfaceNumber = USB_CDC_VCOM_DIC_INTERFACE_INDEX;

        /* Schedule buffer for receive */
        USB_DeviceRecvRequest(handle, g_deviceComposite->cdcVcom[0].bulkOutEndpoint,
                              g_deviceComposite->cdcVcom[0].currRecvBuf,
                              g_deviceComposite->cdcVcom[0].bulkOutEndpointMaxPacketSize);

        /****** Initiailize cdc 2 endpoint*****/
        g_deviceComposite->cdcVcom[1].attach = 1;

        /* Initiailize endpoint for interrupt pipe */
        epCallback.callbackFn    = USB_DeviceCdcAcmInterruptIn;
        epCallback.callbackParam = (void *)&g_deviceComposite->cdcVcom[1].communicationInterfaceNumber;

        epInitStruct.zlt          = 0;
        epInitStruct.transferType = USB_ENDPOINT_INTERRUPT;
        epInitStruct.endpointAddress =
            USB_CDC_VCOM_CIC_INTERRUPT_IN_ENDPOINT_2 | (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT);
        if (USB_SPEED_HIGH == g_deviceComposite->speed)
        {
            epInitStruct.maxPacketSize = HS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE_2;
            epInitStruct.interval      = HS_CDC_VCOM_INTERRUPT_IN_INTERVAL_2;
        }
        else
        {
            epInitStruct.maxPacketSize = FS_CDC_VCOM_INTERRUPT_IN_PACKET_SIZE_2;
            epInitStruct.interval      = FS_CDC_VCOM_INTERRUPT_IN_INTERVAL_2;
        }
        g_deviceComposite->cdcVcom[1].interruptEndpoint              = USB_CDC_VCOM_CIC_INTERRUPT_IN_ENDPOINT_2;
        g_deviceComposite->cdcVcom[1].interruptEndpointMaxPacketSize = epInitStruct.maxPacketSize;
        g_deviceComposite->cdcVcom[1].communicationInterfaceNumber   = USB_CDC_VCOM_CIC_INTERFACE_INDEX_2;
        USB_DeviceInitEndpoint(handle, &epInitStruct, &epCallback);

        /* Initiailize endpoints for bulk in pipe */
        epCallback.callbackFn    = USB_DeviceCdcAcmBulkIn;
        epCallback.callbackParam = (void *)&g_deviceComposite->cdcVcom[1].dataInterfaceNumber;

        epInitStruct.zlt          = 0;
        epInitStruct.interval     = 0;
        epInitStruct.transferType = USB_ENDPOINT_BULK;
        epInitStruct.endpointAddress =
            USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT_2 | (USB_IN << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT);
        if (USB_SPEED_HIGH == g_deviceComposite->speed)
        {
            epInitStruct.maxPacketSize = HS_CDC_VCOM_BULK_IN_PACKET_SIZE_2;
        }
        else
        {
            epInitStruct.maxPacketSize = FS_CDC_VCOM_BULK_IN_PACKET_SIZE_2;
        }
        g_deviceComposite->cdcVcom[1].bulkInEndpoint              = USB_CDC_VCOM_DIC_BULK_IN_ENDPOINT_2;
        g_deviceComposite->cdcVcom[1].bulkInEndpointMaxPacketSize = epInitStruct.maxPacketSize;
        USB_DeviceInitEndpoint(handle, &epInitStruct, &epCallback);
        /* Initiailize endpoints for bulk out pipe */
        epCallback.callbackFn    = USB_DeviceCdcAcmBulkOut;
        epCallback.callbackParam = (void *)&g_deviceComposite->cdcVcom[1].dataInterfaceNumber;

        epInitStruct.zlt          = 0;
        epInitStruct.interval     = 0;
        epInitStruct.transferType = USB_ENDPOINT_BULK;
        epInitStruct.endpointAddress =
            USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT_2 | (USB_OUT << USB_DESCRIPTOR_ENDPOINT_ADDRESS_DIRECTION_SHIFT);
        if (USB_SPEED_HIGH == g_deviceComposite->speed)
        {
            epInitStruct.maxPacketSize = HS_CDC_VCOM_BULK_OUT_PACKET_SIZE_2;
        }
        else
        {
            epInitStruct.maxPacketSize = FS_CDC_VCOM_BULK_OUT_PACKET_SIZE_2;
        }
        g_deviceComposite->cdcVcom[1].bulkOutEndpoint              = USB_CDC_VCOM_DIC_BULK_OUT_ENDPOINT_2;
        g_deviceComposite->cdcVcom[1].bulkOutEndpointMaxPacketSize = epInitStruct.maxPacketSize;
        USB_DeviceInitEndpoint(handle, &epInitStruct, &epCallback);

        g_deviceComposite->cdcVcom[1].dataInterfaceNumber = USB_CDC_VCOM_DIC_INTERFACE_INDEX_2;

        /* Schedule buffer for receive */
        USB_DeviceRecvRequest(handle, g_deviceComposite->cdcVcom[1].bulkOutEndpoint,
                              g_deviceComposite->cdcVcom[1].currRecvBuf,
                              g_deviceComposite->cdcVcom[1].bulkOutEndpointMaxPacketSize);
    }
    return kStatus_USB_Success;
}

/*!
 * @brief Virtual COM device initialization function.
 *
 * This function initializes the device with the composite device class information.
 *
 * @param deviceComposite The pointer to the composite device structure.
 *
 * @return A USB error code or kStatus_USB_Success.
 */
usb_status_t USB_DeviceCdcVcomInit(usb_device_composite_struct_t *deviceComposite)
{
    g_deviceComposite = deviceComposite;
    s_txHead = s_txTail = s_rxHead = s_rxTail = 0U;
    s_consoleTxBusy = 0U;
    for (uint8_t i = 0; i < USB_DEVICE_CONFIG_CDC_ACM; i++)
    {
        g_deviceComposite->cdcVcom[i].lineCoding    = (uint8_t *)&s_lineCoding[i];
        g_deviceComposite->cdcVcom[i].abstractState = (uint8_t *)&s_abstractState[i];
        g_deviceComposite->cdcVcom[i].countryCode   = (uint8_t *)&s_countryCode[i];
        g_deviceComposite->cdcVcom[i].usbCdcAcmInfo = &s_usbCdcAcmInfo[i];
        g_deviceComposite->cdcVcom[i].currRecvBuf   = (uint8_t *)&s_currRecvBuf[i][0];
        ;
        g_deviceComposite->cdcVcom[i].currSendBuf = (uint8_t *)&s_currSendBuf[i][0];
    }
    return kStatus_USB_Success;
}
