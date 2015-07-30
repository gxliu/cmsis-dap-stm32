#define LED_PORT		GPIOB
#define LED_BIT			GPIO12

#define USB_PULLUP_PORT	GPIOA
#define USB_PULLUP_BIT	GPIO8

#define led_Off()	gpio_clear(LED_PORT, LED_BIT)
#define led_On()	gpio_set(LED_PORT, LED_BIT)

#define usb_Off()	gpio_clear(USB_PULLUP_PORT, USB_PULLUP_BIT)
#define usb_On()	gpio_set(USB_PULLUP_PORT, USB_PULLUP_BIT)
