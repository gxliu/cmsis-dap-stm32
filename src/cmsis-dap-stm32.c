/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2009 Uwe Hermann <uwe@hermann-uwe.de>,
 * Copyright (C) 2011 Piotr Esden-Tempski <piotr@esden.net>
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
#include <libopencm3/stm32/usart.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>
#include <libopencm3/usb/cdc.h>

#include <stdio.h>
#include <errno.h>

#include "hwconfig.h"
#include "usb_config.h"

uint8_t usbd_control_buffer[128];


int _write(int file, char *ptr, int len);

int _write(int file, char *ptr, int len)
{
	int i;

	if (file == 1) {
		for (i = 0; i < len; i++)
			usart_send_blocking(USART1, ptr[i]);
		return i;
	}

	errno = EIO;
	return -1;
}

static void rcc_clock_setup_in_hse_8mhz_out_48mhz(void)
{
	/* Enable internal high-speed oscillator. */
	rcc_osc_on(HSI);
	rcc_wait_for_osc_ready(HSI);

	/* Select HSI as SYSCLK source. */
	rcc_set_sysclk_source(RCC_CFGR_SW_SYSCLKSEL_HSICLK);
	
	/* Turn on external clock */
	rcc_osc_on(HSE);
	rcc_wait_for_osc_ready(HSE);
	rcc_set_sysclk_source(RCC_CFGR_SW_SYSCLKSEL_HSECLK);
	/*
	 * Set prescalers for AHB, ADC, ABP1, ABP2.
	 * Do this before touching the PLL (TODO: why?).
	 */
	rcc_set_hpre(RCC_CFGR_HPRE_SYSCLK_NODIV);	/*Set.48MHz Max.72MHz */
	rcc_set_adcpre(RCC_CFGR_ADCPRE_PCLK2_DIV8);	/*Set. 6MHz Max.14MHz */
	rcc_set_ppre1(RCC_CFGR_PPRE1_HCLK_DIV2);	/*Set.24MHz Max.36MHz */
	rcc_set_ppre2(RCC_CFGR_PPRE2_HCLK_NODIV);	/*Set.48MHz Max.72MHz */
	rcc_set_usbpre(RCC_CFGR_USBPRE_PLL_CLK_NODIV);  /*Set.48MHz Max.48MHz */

	/*
	 * Sysclk runs with 48MHz -> 1 waitstates.
	 * 0WS from 0-24MHz
	 * 1WS from 24-48MHz
	 * 2WS from 48-72MHz
	 */
	flash_set_ws(FLASH_ACR_LATENCY_1WS);

	/*
	 * Set the PLL multiplication factor to 6
	 * 8MHz (external) * 6 (multiplier) = 48MHz
	 */
	rcc_set_pll_multiplication_factor(RCC_CFGR_PLLMUL_PLL_CLK_MUL6);

	/* Select HSE as PLL source. */
	rcc_set_pll_source(RCC_CFGR_PLLSRC_HSE_CLK);

	/* Enable PLL oscillator and wait for it to stabilize. */
	rcc_osc_on(PLL);
	rcc_wait_for_osc_ready(PLL);

	/* Select PLL as SYSCLK source. */
	rcc_set_sysclk_source(RCC_CFGR_SW_SYSCLKSEL_PLLCLK);

	/* Set the peripheral clock frequencies used */
	rcc_ahb_frequency = 48000000;
	rcc_apb1_frequency = 24000000;
	rcc_apb2_frequency = 48000000;
}

static void clock_setup(void)
{
	rcc_clock_setup_in_hse_8mhz_out_48mhz();

	/* Enable GPIOB clock (for LED GPIOs). */
	rcc_periph_clock_enable(RCC_GPIOB);

	/* Enable clocks for GPIO port A (for GPIO_USART1_TX) and USART1. */
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_AFIO);
	AFIO_MAPR |= AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_ON;
	rcc_periph_clock_enable(RCC_USART1);
}

static void usart_setup(void)
{
	/* Setup GPIO pin GPIO_USART1_TX. */
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_50_MHZ,
		      GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO_USART1_TX);

	/* Setup UART parameters. */
	usart_set_baudrate(USART1, 115200);
	usart_set_databits(USART1, 8);
	usart_set_stopbits(USART1, USART_STOPBITS_1);
	usart_set_mode(USART1, USART_MODE_TX);
	usart_set_parity(USART1, USART_PARITY_NONE);
	usart_set_flow_control(USART1, USART_FLOWCONTROL_NONE);

	/* Finally enable the USART. */
	usart_enable(USART1);
}

static void gpio_setup(void)
{
	led_Off();
	usb_Off();

	gpio_set_mode(LED_PORT, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, LED_BIT);
	
	gpio_set_mode(USB_PULLUP_PORT, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL,USB_PULLUP_BIT);
}

static int cdc_control_request(usbd_device *usbd_dev, struct usb_setup_data *req, uint8_t **buf,
		uint16_t *len, void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{
	(void)complete;
	(void)buf;
	(void)usbd_dev;
	printf("CDCControl : bReq=%d, bReqT=%d\r\n", req->bRequest, req->bmRequestType);
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
			printf("baudrate = %d, databits = %d\r\n", 
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
	printf("HIDControl : bReq=%d, bReqT=%d, wValue=%x\r\n", req->bRequest, req->bmRequestType, req->wValue);
	if ((req->bmRequestType == 0x81) &&
	   (req->bRequest == USB_REQ_GET_DESCRIPTOR) &&
	   (req->wValue == 0x2200))
	{
		printf("HID Get report descriptor\r\n");
		*buf = (uint8_t *)hid_report_descriptor;
		*len = sizeof(hid_report_descriptor);
		return 1;
	}

	return 0;
}

static void cdcacm_data_1_rx_cb(usbd_device *usbd_dev, uint8_t ep)
{
	(void)ep;
	(void)usbd_dev;

	char buf[64];
	int len = usbd_ep_read_packet(usbd_dev, 0x01, buf, 64);

	if (len) {
		usbd_ep_write_packet(usbd_dev, 0x82, buf, len);
		buf[len] = 0;
	}
}

static void set_config(usbd_device *usbd_dev, uint16_t wValue)
{
	(void)wValue;
	(void)usbd_dev;

	usbd_ep_setup(usbd_dev, 0x01, USB_ENDPOINT_ATTR_BULK, 64, cdcacm_data_1_rx_cb);
	usbd_ep_setup(usbd_dev, 0x82, USB_ENDPOINT_ATTR_BULK, 64, NULL);
	usbd_ep_setup(usbd_dev, 0x83, USB_ENDPOINT_ATTR_INTERRUPT, 8, NULL);
	usbd_ep_setup(usbd_dev, 0x84, USB_ENDPOINT_ATTR_INTERRUPT, 64, NULL);
	usbd_ep_setup(usbd_dev, 0x04, USB_ENDPOINT_ATTR_INTERRUPT, 64, NULL);	

	usbd_register_control_callback(
				usbd_dev,
				USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				(usbd_control_callback)cdc_control_request);
				
	usbd_register_control_callback(
				usbd_dev,
				USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				(usbd_control_callback)hid_control_request);
}

int main(void)
{
	int i, sp;
	usbd_device *usbd_dev;
	
	clock_setup();
	gpio_setup();
	usart_setup();
	
	usbd_dev = usbd_init(&stm32f103_usb_driver, &dev, &config, usb_strings,
						5, usbd_control_buffer, sizeof(usbd_control_buffer));
	usbd_register_set_config_callback(usbd_dev, set_config);
	
	for (i = 0; i < 0x80000; i++)
		__asm__("nop");
	
	i = 0;
	led_On();
	usb_On();
	
	asm volatile ("mrs     %0, MSP" : "=r" (sp) : : "memory");
	printf("Hello World! Stack = %8X\r\n", sp);
	
	while (1) {
		usbd_poll(usbd_dev);
		i++;
		if(i == 0x80000)
		{
			printf("I 'm alive\r\n");
			i = 0;
		}
	}

	return 0;
}
