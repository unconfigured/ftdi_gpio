ftdi_gpio
=========

This kernel module enables you to access the CBUS pins of the FT232R chip as GPIs. The CBUS-Pins are 5 additional pins beside the UART Pins which are normaly used on the chip to get a USB-Serial console.

This is a first attempt to make this pins available as gpio kernel module - but I am not a kernel hacker. So feel free to improve and extend. Code is borought from libfti, other gpio kernel modules and the ftdi_sio module. What helped me to get started was the knowledge, that the usb calls from libfti (uses libusb api) are nearly the same as the ones used in kernel, so porting the usb calls is very easy.

It should easily be possible to extend the module for other FTDI-USB converters and/or for other bitbang modes of the chip (i.e. use the serial pins for bitbanging) but currently only FT232R-CBUS bitbanging is implemented.

Tested on Linux 3.13.3 with an FT232RL Chip.

Prerequisites
-------------

You need to prepare the EEPROM of the FTDI Chip for CBUS Bitbang mode. To do so, install libftdi1 and use the ftdi_eeprom tool to wirte a new eeprom. You can use the following example config for this purpose. This config will enable cbus2 and cbus3 as IO Pins, leaving cbus0,1 and 4 as default. cbus4 is not available for CBUS-Bitbanging (see datasheet).

	vendor_id=0x0403        # Vendor ID
	product_id=0x6001       # Product ID

	max_power=500

	product="FT232R USB UART"
	serial=""
	manufacturer=""
	
	self_powered=false
	remote_wakeup=false
	use_serial=true

	cbus0="TXLED"
	cbus1="RXLED"
	cbus2="IO_MODE"
	cbus3="IO_MODE"
	cbus4="SLEEP"


Loading the Module
-------------------

Before loading the module, make sure ftdi_sio is not loaded. The modules cannot coexist. Maybe some day someone joins the modules, so that we have cbus- and serial support at the same time. 
When the module gets loaded, you should see the GPIO assignment in dmesg, otherwise the module will tell you to configure your eeprom. The GPIO Numbers are relative to the base address the GPIOS will be assigned to, see dmesg.


Using GPIO from sysfs
---------------------

After the module is loaded, have a look in /sys/class/gpio. You should find a new bus entry. 

# ls /sys/class/gpio/    
export  gpiochip254@  unexport

Now you can export the pins you wish to use with sysfs. In this example the new chip has base address 254, which relates to the first gpio on this chip.

# echo 254 > /sys/class/gpio/export

Set the direction and query / set the pin value

# echo in > /sys/class/gpio/gpio254/direction
# cat /sys/class/gpio/gpio254/value
# echo out > /sys/class/gpio/gpio254/direction
# echo 0 > /sys/class/gpio/gpio254/value


I2C Bitbanging
--------------

This module works with the gpio-i2c module allowing you to get additional two I2C busses on your FTDI. Be sure that the pins are not exported when using this module, otherwise it won't work. To make adding and removin i2c busses / gpios for i2c easier have a look at the configuration module at https://github.com/kadamski/i2c-gpio-param.
