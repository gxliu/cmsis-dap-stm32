/*
 * 
 */

#include <stdlib.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>

#include "usb_config.h"


/* Buffer to be used for control requests. */
uint8_t usbd_control_buffer[128];

static int hid_control_request(usbd_device *usbd_dev, struct usb_setup_data *req, uint8_t **buf, uint16_t *len,
			void (**complete)(usbd_device *usbd_dev, struct usb_setup_data *req))
{
	(void)complete;
	(void)usbd_dev;

	if ((req->bmRequestType != 0x81) ||
	   (req->bRequest != USB_REQ_GET_DESCRIPTOR) ||
	   (req->wValue != 0x2200))
		return 0;

	/* Handle the HID report descriptor. */
	*buf = (uint8_t *)hid_report_descriptor;
	*len = sizeof(hid_report_descriptor);

	return 1;
}


static void hid_set_config(usbd_device *usbd_dev, uint16_t wValue)
{
	(void)wValue;
	(void)usbd_dev;

	usbd_ep_setup(usbd_dev, 0x81, USB_ENDPOINT_ATTR_INTERRUPT, 4, NULL);

	usbd_register_control_callback(
				usbd_dev,
				USB_REQ_TYPE_STANDARD | USB_REQ_TYPE_INTERFACE,
				USB_REQ_TYPE_TYPE | USB_REQ_TYPE_RECIPIENT,
				hid_control_request);

	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB_DIV8);
	/* SysTick interrupt every N clock pulses: set reload to N-1 */
	systick_set_reload(99999);
	systick_interrupt_enable();
	systick_counter_enable();
}


#if 0 /* is this used? */
void sys_tick_handler(void)
{
	static int x = 0;
	static int dir = 1;
	uint8_t buf[4] = {0, 0, 0, 0};

	buf[1] = dir;
	x += dir;
	if (x > 30)
		dir = -dir;
	if (x < -30)
		dir = -dir;

	usbd_ep_write_packet(usbd_dev, 0x81, buf, 4);
}
#endif

static void clock_setup(void)
{
	rcc_clock_setup_in_hsi_out_48mhz();

	rcc_periph_clock_enable(RCC_AFIO);
	AFIO_MAPR |= AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_ON;
	
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	
}

static void gpio_setup(void)
{
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO12);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ,
		      GPIO_CNF_OUTPUT_PUSHPULL, GPIO15);
		      
	gpio_clear(GPIOB, GPIO12);
	gpio_clear(GPIOA, GPIO15);
}


int main(void)
{
	int i;

	usbd_device *usbd_dev;

	clock_setup();
	gpio_setup();

	usbd_dev = usbd_init(&stm32f103_usb_driver, &dev, &config, usb_strings, 3, usbd_control_buffer, sizeof(usbd_control_buffer));
	usbd_register_set_config_callback(usbd_dev, hid_set_config);

	for (i = 0; i < 0x80000; i++)
		__asm__("nop");

	gpio_set(GPIOA, GPIO15);
	gpio_set(GPIOB, GPIO12);
	while (1)
	{
		usbd_poll(usbd_dev);
	}
}

