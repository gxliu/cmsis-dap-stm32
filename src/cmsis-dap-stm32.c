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
#include <libopencm3/stm32/usart.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/flash.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/hid.h>
#include <libopencm3/usb/cdc.h>

#include <stdio.h>
#include <errno.h>

#include "hwconfig.h"
#include "usb.h"
#include "uart.h"
#include "common.h"

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
	dbg("Hello World! Stack = %8X\r\n", sp);
	
	while (1) {
		usbd_poll(usbd_dev);
		i++;
		if(i == 0x80000)
		{
			dbg("I 'm alive\r\n");
			i = 0;
		}
	}

	return 0;
}
