#include <linux/usb.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/slab.h>

#define FTDI_SIO_SET_BITMODE_REQUEST_TYPE 0x40
#define FTDI_SIO_SET_BITMODE_REQUEST 0x0b

#define FTDI_SIO_READ_PINS_REQUEST_TYPE 0xc0
#define FTDI_SIO_READ_PINS_REQUEST 0x0c

#define FTDI_READ_EEPROM_REQUEST_TYPE 0xc0
#define FTDI_READ_EEPROM_REQUEST 0x90

#define FTDI_SIO_SET_BITMODE_CBUS 0x20

#define WDR_TIMEOUT 5000 /* default urb timeout */

#define MAX_GPIO 16

#define CBUS_IOMODE		0x0a
#define FTDI_MAX_EEPROM_SIZE	256

static const struct usb_device_id id_table[] = {
	{ USB_DEVICE(0x0403, 0x6001) },	/* FT232R */
	{ },
};
MODULE_DEVICE_TABLE(usb, id_table);

struct ftdi_gpio_private {
	int val;
	struct usb_device *usb_dev;
	struct gpio_chip *gpio;
	__u16 interface;
	
	int state;			// last char: high nibble = direction, low nibble = ouput state
	unsigned char map[MAX_GPIO];	// Map GPIO to CBUS-PinNumber
	
	char eeprom[FTDI_MAX_EEPROM_SIZE];
	int eeprom_size;
};

static int ftdi_gpio_get(struct gpio_chip *chip, unsigned offset) {
	struct ftdi_gpio_private *priv = dev_get_drvdata(chip->dev);
	unsigned char cbus_id = priv->map[offset];
	unsigned char *pins;
	int rv;
	
        pins = kmalloc(1, GFP_KERNEL);	// Need to be malloced!
        if (!pins)
                return -ENOMEM;

	rv = usb_control_msg(priv->usb_dev,
			usb_rcvctrlpipe(priv->usb_dev, 0),
			FTDI_SIO_READ_PINS_REQUEST,
			FTDI_SIO_READ_PINS_REQUEST_TYPE,
			0, priv->interface,
			pins, 1, WDR_TIMEOUT);
	
	if (rv < 0) {
		dev_err(chip->dev, "%s - read pin state failed (ret=%d)\n", __func__, rv);
		rv=0; // FIXME
	} else {
		rv = (pins[0] & cbus_id)?1:0;
	}
	
	kfree(pins);
	return rv;
}

static void ftdi_gpio_set(struct gpio_chip *chip, unsigned offset, int value) {

	struct ftdi_gpio_private *priv = dev_get_drvdata(chip->dev);
	unsigned char cbus_id = priv->map[offset];
	
	if (value == 0) {
		priv->state &= ~cbus_id;
	} else {
		priv->state |= cbus_id;
	}
	
	priv->state |= (FTDI_SIO_SET_BITMODE_CBUS<<8);
	usb_control_msg(priv->usb_dev,
			usb_sndctrlpipe(priv->usb_dev, 0),
			FTDI_SIO_SET_BITMODE_REQUEST,
			FTDI_SIO_SET_BITMODE_REQUEST_TYPE,
			priv->state, priv->interface,
			NULL, 0, WDR_TIMEOUT);
};

static int ftdi_gpio_direction_input(struct gpio_chip *chip, unsigned offset) {
	struct ftdi_gpio_private *priv = dev_get_drvdata(chip->dev);
	unsigned char cbus_id=priv->map[offset];
	
	priv->state &= ~(cbus_id<<4 & cbus_id);	// Set as input, and clear output state
	ftdi_gpio_set(chip, offset, 1);		// doit
	return 0;
}

static int ftdi_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value) {
	struct ftdi_gpio_private *priv = dev_get_drvdata(chip->dev);
	unsigned char cbus_id=priv->map[offset];
	
	priv->state |= (cbus_id<<4);		// mark as output
	ftdi_gpio_set(chip, offset, value);	// doit and set low
	return 0;
};

static struct gpio_chip ftdi_gpio_chip = {
	.label			= "ftdi_gpio",
	.direction_input	= ftdi_gpio_direction_input,
	.direction_output	= ftdi_gpio_direction_output,
	.set			= ftdi_gpio_set,
	.get			= ftdi_gpio_get,
	.base			= -1,
	.ngpio			= 0,
};


static int ftdi_usb_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	int ret;
	int i;
	struct ftdi_gpio_private *priv;
	
	// Register some private data storage
	priv = kzalloc(sizeof(struct ftdi_gpio_private), GFP_KERNEL);
	if (!priv) {
		dev_err(&interface->dev, "%s- kmalloc(%Zd) failed.\n", __func__,
					sizeof(struct ftdi_gpio_private));
		return -ENOMEM;
	}
	memset(priv, 0, sizeof(struct ftdi_gpio_private));
	
	priv->gpio=&ftdi_gpio_chip;
	priv->gpio->dev=&interface->dev; 		// GLUE, make usb-dev available to access priv when we only have the priv->gpio in gpio callbacks
	priv->usb_dev=interface_to_usbdev(interface);	// store usb device
	priv->state=0;
	memset(priv->eeprom, 0, sizeof(priv->eeprom));
	dev_set_drvdata(&interface->dev, priv);		// store as usb drvdata
	
	// Read EEPROM
	for (i = 0; i < FTDI_MAX_EEPROM_SIZE/2; i++)
	{
		ret = usb_control_msg(priv->usb_dev,
			usb_rcvctrlpipe(priv->usb_dev, 0),
			FTDI_READ_EEPROM_REQUEST,
			FTDI_READ_EEPROM_REQUEST_TYPE,
			0, i, (priv->eeprom)+(i*2), 2, WDR_TIMEOUT);
		if(ret != 2) {
			dev_info(&interface->dev, "error reading eeprom\n");
			kfree(priv);
			return -EINVAL;
		}
	}
	
	// FT232R
	priv->eeprom_size = 0x80;
	priv->interface = 0;
	// Only add gpio pin when pin is in CBUS-State
	for (i = 0; i < 5; i++) {
		if ( ( (priv->eeprom[0x14 + i/2] >> ((i%2==0)?0:4) ) & 0x0f) == CBUS_IOMODE) {
			dev_info(&interface->dev, "GPIO <base>+%d is CBUS %d\n", priv->gpio->ngpio, i);
			priv->map[priv->gpio->ngpio]=2^i;
			priv->gpio->ngpio++;
		}
	}
	
	if (priv->gpio->ngpio == 0) {
		dev_info(&interface->dev, "No GPIO pins found, you need set the pins to iomode (modify eeprom)\n");
		kfree(priv);
		return -EOPNOTSUPP;
	}

	// Register GPIO
	ret=gpiochip_add(priv->gpio);
	
	return ret;
};

static void ftdi_usb_disconnect(struct usb_interface *interface) {
	
	int ret;
	int gpio;
	
	struct ftdi_gpio_private *priv = dev_get_drvdata(&interface->dev);

	for (gpio = priv->gpio->base; gpio < (priv->gpio->base + priv->gpio->ngpio); gpio++) {
		gpio_free(gpio);
	}
	ret=gpiochip_remove(priv->gpio);
	kfree(priv);
	
	dev_info(&interface->dev, "device disconnected\n");
}

static struct usb_driver ftdi_gpio_device = {
	.name			= "ftdi_gpio_usb",
	.probe			= ftdi_usb_probe,
	.disconnect		= ftdi_usb_disconnect,
	.id_table		= id_table,
	.no_dynamic_id =	1,
	.supports_autosuspend =	1,
};

module_usb_driver(ftdi_gpio_device);

MODULE_AUTHOR("Friedolin Barth");
MODULE_DESCRIPTION("FTDI CBUS support");
MODULE_LICENSE("GPL");
