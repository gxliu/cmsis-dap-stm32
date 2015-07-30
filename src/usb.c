/* CMSIS-DAP-STM32 libre using libopencm3 library 
 * 
 * 
 * Copyright (C) 2009 Uwe Hermann <uwe@hermann-uwe.de>,
 * Copyright (C) 2011 Piotr Esden-Tempski <piotr@esden.net>
 * Copyright (C) 2015 Zhiyuan Wan <rgwan@rocaloid.org>
 * 
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>
#include <libopencm3/usb/cdc.h>
#include <libopencm3/usb/msc.h>

#include <stdio.h>
#include <errno.h>

#include "hwconfig.h"
#include "usb.h"
#include "uart.h"
#include "common.h"

uint8_t usbd_control_buffer[256];
const struct usb_device_descriptor dev = {
	.bLength = USB_DT_DEVICE_SIZE,
	.bDescriptorType = USB_DT_DEVICE,
	.bcdUSB = 0x0200,
	.bDeviceClass = 0,
	.bDeviceSubClass = 0,
	.bDeviceProtocol = 0,
	.bMaxPacketSize0 = 64,
	.idVendor = 0xC251,
	.idProduct = 0xF001,
	.bcdDevice = 0x100,
	.iManufacturer = 1,
	.iProduct = 2,
	.iSerialNumber = 3,
	.bNumConfigurations = 1,
};

/*
 * This notification endpoint isn't implemented. According to CDC spec its
 * optional, but its absence causes a NULL pointer dereference in Linux
 * cdc_acm driver.
 *  --- CDC configuration start---
 */
static const struct usb_endpoint_descriptor comm_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_ACM_CONTROL_EP,
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
	.wMaxPacketSize = 16,
	.bInterval = 25,
}};

static const struct usb_endpoint_descriptor data_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_ACM_BULK_OUT_EP,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 1,
}, {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_ACM_BULK_IN_EP,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 1,
}};

static const struct {
	struct usb_cdc_header_descriptor header;
	struct usb_cdc_call_management_descriptor call_mgmt;
	struct usb_cdc_acm_descriptor acm;
	struct usb_cdc_union_descriptor cdc_union;
} __attribute__((packed)) cdcacm_functional_descriptors = {
	.header = {
		.bFunctionLength = sizeof(struct usb_cdc_header_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_HEADER,
		.bcdCDC = 0x0110,
	},
	.call_mgmt = {
		.bFunctionLength =
			sizeof(struct usb_cdc_call_management_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_CALL_MANAGEMENT,
		.bmCapabilities = 0,
		.bDataInterface = 2,
	},
	.acm = {
		.bFunctionLength = sizeof(struct usb_cdc_acm_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_ACM,
		.bmCapabilities = 0,
	},
	.cdc_union = {
		.bFunctionLength = sizeof(struct usb_cdc_union_descriptor),
		.bDescriptorType = CS_INTERFACE,
		.bDescriptorSubtype = USB_CDC_TYPE_UNION,
		.bControlInterface = 1,
		.bSubordinateInterface0 = 2,
	 },
};

static const struct usb_interface_descriptor comm_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 1,
	.bAlternateSetting = 0,
	.bNumEndpoints = 1,
	.bInterfaceClass = USB_CLASS_CDC,
	.bInterfaceSubClass = USB_CDC_SUBCLASS_ACM,
	.bInterfaceProtocol = USB_CDC_PROTOCOL_AT,
	.iInterface = 0,

	.endpoint = comm_endp,

	.extra = &cdcacm_functional_descriptors,
	.extralen = sizeof(cdcacm_functional_descriptors),
}};

static const struct usb_interface_descriptor data_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 2,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_DATA,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,

	.endpoint = data_endp,
}};

static const struct usb_iface_assoc_descriptor cdc_iad[]=
{{
	.bLength =  USB_DT_INTERFACE_ASSOCIATION_SIZE,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface = 0x01,
	.bInterfaceCount = 0x02,
	.bFunctionClass = 0x02, /* CDC */
	.bFunctionSubClass = 0x02,
	.bFunctionProtocol = 0x01,
	.iFunction = 0x05,
}};

/*** HID Section ***/

static const uint8_t hid_report_descriptor[] = {
	0x05, 0x8c, /* USAGE_PAGE (ST Page) */
	0x09, 0x01, /* USAGE (Demo Kit) */
	0xa1, 0x01, /* COLLECTION (Application) */
	 
	// The Input report
	0x09,0x03, // USAGE ID - Vendor defined
	0x15,0x00, // LOGICAL_MINIMUM (0)
	0x26,0x00, 0xFF, // LOGICAL_MAXIMUM (255)
	0x75,0x08, // REPORT_SIZE (8bit)
	0x95,0x40, // REPORT_COUNT (64Byte)
	0x81,0x02, // INPUT (Data,Var,Abs)
 
	// The Output report
	0x09,0x04, // USAGE ID - Vendor defined
	0x15,0x00, // LOGICAL_MINIMUM (0)
	0x26,0x00,0xFF, // LOGICAL_MAXIMUM (255)
	0x75,0x08, // REPORT_SIZE (8bit)
	0x95,0x40, // REPORT_COUNT (64Byte)
	0x91,0x02, // OUTPUT (Data,Var,Abs)
 
	0xc0 /* END_COLLECTION */
};

static const struct {
	struct usb_hid_descriptor hid_descriptor;
	struct {
		uint8_t bReportDescriptorType;
		uint16_t wDescriptorLength;
	} __attribute__((packed)) hid_report;
} __attribute__((packed)) hid_function = {
	.hid_descriptor = {
		.bLength = sizeof(hid_function),
		.bDescriptorType = USB_DT_HID,
		.bcdHID = 0x0100,
		.bCountryCode = 0,
		.bNumDescriptors = 1,
	},
	.hid_report = {
		.bReportDescriptorType = USB_DT_REPORT,
		.wDescriptorLength = sizeof(hid_report_descriptor),
	},
};

const struct usb_endpoint_descriptor hid_endpoint[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = HID_EP_IN,
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
	.wMaxPacketSize = 64,
	.bInterval = 0x01,
},{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = HID_EP_OUT,
	.bmAttributes = USB_ENDPOINT_ATTR_INTERRUPT,
	.wMaxPacketSize = 64,
	.bInterval = 0x01,
}};

const struct usb_interface_descriptor hid_iface = {
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 0,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_HID,
	.bInterfaceSubClass = 0,
	.bInterfaceProtocol = 0,
	.iInterface = 0,

	.endpoint = hid_endpoint,

	.extra = &hid_function,
	.extralen = sizeof(hid_function),
};

static const struct usb_iface_assoc_descriptor hid_iad[]=
{{
	.bLength =  USB_DT_INTERFACE_ASSOCIATION_SIZE,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface = 0x00,
	.bInterfaceCount = 0x01,
	.bFunctionClass = USB_CLASS_HID, /* HID */
	.bFunctionSubClass = 0x00,
	.bFunctionProtocol = 0x00,
	.iFunction = 0x04,
}};

/*** MSC Section ***/
#ifdef USB_MSC_ENABLE
static const struct usb_endpoint_descriptor msc_endp[] = {{
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_MSC_BULK_OUT_EP,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 0,
}, {
	.bLength = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,
	.bEndpointAddress = USB_MSC_BULK_IN_EP,
	.bmAttributes = USB_ENDPOINT_ATTR_BULK,
	.wMaxPacketSize = 64,
	.bInterval = 0,
}};

static const struct usb_interface_descriptor msc_iface[] = {{
	.bLength = USB_DT_INTERFACE_SIZE,
	.bDescriptorType = USB_DT_INTERFACE,
	.bInterfaceNumber = 3,
	.bAlternateSetting = 0,
	.bNumEndpoints = 2,
	.bInterfaceClass = USB_CLASS_MSC,
	.bInterfaceSubClass = USB_MSC_SUBCLASS_SCSI,
	.bInterfaceProtocol = USB_MSC_PROTOCOL_BBB,
	.iInterface = 0,
	.endpoint = msc_endp,
	.extra = NULL,
	.extralen = 0
}};

static const struct usb_iface_assoc_descriptor msc_iad[]=
{{
	.bLength =  USB_DT_INTERFACE_ASSOCIATION_SIZE,
	.bDescriptorType = USB_DT_INTERFACE_ASSOCIATION,
	.bFirstInterface = 0x00,
	.bInterfaceCount = 0x01,
	.bFunctionClass = USB_CLASS_MSC, /* MSC */
	.bFunctionSubClass = USB_MSC_SUBCLASS_SCSI,
	.bFunctionProtocol = USB_MSC_PROTOCOL_BBB,
	.iFunction = 0x06,
}};
#endif

const struct usb_interface ifaces[] = {{
	.num_altsetting = 1,
	.altsetting = &hid_iface,
	.iface_assoc = hid_iad,
#ifdef USB_ACM_ENABLE
},{
	.num_altsetting = 1,
	.altsetting = comm_iface,
	.iface_assoc = cdc_iad,
}, {
	.num_altsetting = 1,
	.altsetting = data_iface,
#endif
#ifdef USB_MSC_ENABLE
},{
	.num_altsetting = 1,
	.altsetting = msc_iface,
	.iface_assoc = msc_iad,
#endif
}};

const struct usb_config_descriptor config = {
	.bLength = USB_DT_CONFIGURATION_SIZE,
	.bDescriptorType = USB_DT_CONFIGURATION,
	.wTotalLength = 0,
#ifdef USB_ACM_ENABLE
#ifdef USB_MSC_ENABLE
	.bNumInterfaces = 4,
#else
	.bNumInterfaces = 3,
#endif
#else
	.bNumInterfaces = 1,
#endif
	.bConfigurationValue = 1,
	.iConfiguration = 0,
	.bmAttributes = 0xC0,
	.bMaxPower = 0x32,

	.interface = ifaces,
};

const char *usb_strings[] = {
	"ZhiYuan Wan",
	"STM32 CMSIS-DAP",
	"0001A0000000",
	"CMSIS-DAP HID",
	"CMSIS-DAP CDC",
	"CMSIS-DAP MSC"
};

static int cdc_control_request(usbd_device *usbd_dev, struct usb_setup_data *req, uint8_t **buf,
		uint16_t *len, void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{
	(void)complete;
	(void)buf;
	(void)usbd_dev;
	dbg("CDCControl : bReq=%d, bReqT=%d\r\n", req->bRequest, req->bmRequestType);
	switch (req->bRequest) {
		case USB_CDC_REQ_SET_CONTROL_LINE_STATE: 
		{
			/*
			 * This Linux cdc_acm driver requires this to be implemented
			 * even though it's optional in the CDC spec, and we don't
			 * advertise it in the ACM functional descriptor.
			 */
			/* Maybe I should implement some functions at this place,
			 * TODO: DTR/RTS control, CTS/DSR/RI recieve
			 */
			char local_buf[10];
			struct usb_cdc_notification *notif = (void *)local_buf;

			/* We echo signals back to host as notification. */
			notif->bmRequestType = 0xA1;
			notif->bNotification = USB_CDC_NOTIFY_SERIAL_STATE;
			notif->wValue = 0;
			notif->wIndex = 0;
			notif->wLength = 2;
			local_buf[8] = req->wValue & 3;
			local_buf[9] = 0;
			// usbd_ep_write_packet(0x83, buf, 10);
			return 1;
		}
		case USB_CDC_REQ_SET_LINE_CODING:
		{
			/*** The special baudrate can be entries some special function:
			 * TODO: Bootloader
			 * TODO: SPI Transmitter
			 * TODO: I2C Transmitter
			 * TODO: GPIO/PWM Expander
			*/
			struct usb_cdc_line_coding *coding = (void *)*buf;
			if (*len < sizeof(struct usb_cdc_line_coding))
				return 0;
			dbg("baudrate = %d, databits = %d\r\n", 
				(int) coding->dwDTERate, (int)coding->bDataBits);
			return 1;
		}
	}
	return 0;
}

static int hid_control_request(usbd_device *usbd_dev, struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
			void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{
	(void)complete;
	(void)usbd_dev;
	dbg("HIDControl : bReq=%d, bReqT=%d, wValue=0x%x\r\n", req->bRequest, 
				req->bmRequestType, req->wValue);
	if ((req->bmRequestType == HID_EP_IN) &&
	   (req->bRequest == USB_REQ_GET_DESCRIPTOR) &&
	   (req->wValue == 0x2200))
	{

		/* Handle the HID report descriptor. */
		*buf = (uint8_t *)hid_report_descriptor;
		*len = sizeof(hid_report_descriptor);
		dbg("HIDControl: Got report to the host\r\n");
		return 1;
	}

	return 0;
}

static void cdcacm_data_1_rx_cb(usbd_device *usbd_dev, uint8_t ep)
{
	(void)ep;
	(void)usbd_dev;

	char buf[64];
	int len = usbd_ep_read_packet(usbd_dev, USB_ACM_BULK_OUT_EP, buf, 64);

	if (len) {
		usbd_ep_write_packet(usbd_dev, USB_ACM_BULK_IN_EP, buf, len);
		buf[len] = 0;
	}
}


static void hid_rx(usbd_device *usbd_dev, uint8_t ep)
{
	(void)ep;
	(void)usbd_dev;

	char buf[64];
	int len = usbd_ep_read_packet(usbd_dev, HID_EP_OUT, buf, 64);
	if (len) {
		dbg("HID-Rx ,length = %d\r\n", len);
		hexdump(buf, len);
		usbd_ep_write_packet(usbd_dev, HID_EP_IN, "Hello world", 12);
	}
}


void set_config(usbd_device *usbd_dev, uint16_t wValue)
{
	(void)wValue;
	(void)usbd_dev;

	/*** Setting up CDC Endpoints ***/
	usbd_ep_setup(usbd_dev, USB_ACM_BULK_OUT_EP, USB_ENDPOINT_ATTR_BULK, 64, cdcacm_data_1_rx_cb);
	usbd_ep_setup(usbd_dev, USB_ACM_BULK_IN_EP, USB_ENDPOINT_ATTR_BULK, 64, NULL);
	usbd_ep_setup(usbd_dev, USB_ACM_CONTROL_EP, USB_ENDPOINT_ATTR_INTERRUPT, 8, NULL);
	
	/*** Setting up HID Endpoints ***/
	usbd_ep_setup(usbd_dev, HID_EP_IN, USB_ENDPOINT_ATTR_INTERRUPT, 64, NULL);
	usbd_ep_setup(usbd_dev, HID_EP_OUT, USB_ENDPOINT_ATTR_INTERRUPT, 64, hid_rx);
				
	usbd_register_control_callback(
				usbd_dev,
				USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				(usbd_control_callback)hid_control_request);
				
	usbd_register_control_callback(
				usbd_dev,
				USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				(usbd_control_callback)cdc_control_request);
	dbg("USB Linked up\r\n");
}
