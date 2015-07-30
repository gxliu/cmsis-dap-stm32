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
#ifndef HWCONFIG_H
#define HWCONFIG_H

#define CUSTOM_HW
#ifdef CUSTOM_HW
	#define LED_PORT		GPIOB
	#define LED_BIT			GPIO12

	#define USB_PULLUP_PORT	GPIOA
	#define USB_PULLUP_BIT	GPIO8

	#define led_Off()	gpio_clear(LED_PORT, LED_BIT)
	#define led_On()	gpio_set(LED_PORT, LED_BIT)

	#define usb_Off()	gpio_clear(USB_PULLUP_PORT, USB_PULLUP_BIT)
	#define usb_On()	gpio_set(USB_PULLUP_PORT, USB_PULLUP_BIT)
#else
	#error Please define a specific Debugger hardware!
#endif

#define DEBUG /*I haven't any debugger to use, so I uses UART1 to Debug */

#ifdef DEBUG
	#define dbg(...)	printf("DEBUG : "__VA_ARGS__)
	#include <stdio.h>
	#include <errno.h>
	int _write(int file, char *ptr, int len); /* newlib stub function */
	void hexdump(char *buffer, int len);
#else
	#define dbg(...)
	#define hexdump(...)
#endif

#endif
