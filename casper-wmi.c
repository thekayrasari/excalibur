#include <linux/acpi.h>
#include <linux/leds.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/wmi.h>
#include <linux/device.h>
#include <linux/dev_printk.h>
#include <linux/hwmon.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/dmi.h>
#include <acpi/acexcep.h>

MODULE_AUTHOR("Mustafa Ekşi <mustafa.eskieksi@gmail.com>");
MODULE_DESCRIPTION("Casper Excalibur Laptop WMI driver");
MODULE_LICENSE("GPL");

#define CASPER_WMI_GUID "644C5791-B7B0-4123-A90B-E93876E0DAAD"

#define CASPER_KEYBOARD_LED_1 0x03
#define CASPER_KEYBOARD_LED_2 0x04
#define CASPER_KEYBOARD_LED_3 0x05
#define CASPER_ALL_KEYBOARD_LEDS 0x06
#define CASPER_CORNER_LEDS 0x07

#define CASPER_READ 0xfa00
#define CASPER_WRITE 0xfb00
#define CASPER_GET_HARDWAREINFO 0x0200
#define CASPER_GET_BIOSVER 0x0201
#define CASPER_SET_LED 0x0100
#define CASPER_POWERPLAN 0x0300

// I don't know why but I can't use some of new api in wmi.h
#ifndef to_wmi_device
#define to_wmi_device(device)	container_of(device, struct wmi_device, dev)
#endif

struct casper_wmi_args {
	u16 a0, a1;
	u32 a2, a3, a4, a5, a6, rev0, rev1;
};

static u32 last_keyboard_led_change;
static u32 last_keyboard_led_zone;
static bool *casper_raw_fanspeed;

static int dmi_matched(const struct dmi_system_id *dmi)
{
	pr_info("Identified laptop model '%s'\n", dmi->ident);
	casper_raw_fanspeed = dmi->driver_data;
	return 1;
}

static bool has_raw_fanspeed = true;
static bool no_raw_fanspeed = false;

static const struct dmi_system_id casper_dmi_list[] = {
	{
	 .callback = dmi_matched,
	 .ident = "CASPER EXCALIBUR G650",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "CASPER BILGISAYAR SISTEMLERI"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G650")
		     },
	 .driver_data = &no_raw_fanspeed,
        },
        {
	 .callback = dmi_matched,
	 .ident = "CASPER EXCALIBUR G750",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "CASPER BILGISAYAR SISTEMLERI"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G750")
		     },
	 .driver_data = &no_raw_fanspeed,
        },
        {
	 .callback = dmi_matched,
	 .ident = "CASPER EXCALIBUR G670",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "CASPER BILGISAYAR SISTEMLERI"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G670")
		     },
	 .driver_data = &no_raw_fanspeed,
        },
        {
	 .callback = dmi_matched,
	 .ident = "CASPER EXCALIBUR G900",
	 .matches = {
		     DMI_MATCH(DMI_SYS_VENDOR, "CASPER BILGISAYAR SISTEMLERI"),
		     DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G900"),
                     DMI_MATCH(DMI_BIOS_VERSION, "CP131")
		     },
	 .driver_data = &no_raw_fanspeed,
	  },
	{ }
};

/*
 * Function to set led value of specified zone, zone id should be between 3 and 7.
 * */
static acpi_status casper_set(u16 a1, u32 zone_id, u32 data)
{
	struct casper_wmi_args wmi_args = { 0 };
	wmi_args.a0 = CASPER_WRITE;
	wmi_args.a1 = a1;
	wmi_args.a2 = zone_id;
	wmi_args.a3 = data;

	struct acpi_buffer input = {
		(acpi_size) sizeof(struct casper_wmi_args),
		&wmi_args
	};
	return wmi_set_block(CASPER_WMI_GUID, 0, &input);
}

static ssize_t led_control_show(struct device *dev, struct device_attribute
				*attr, char *buf)
{
	return -EOPNOTSUPP;
}

/*
 * input should start with zone id and then u32 led data (same as casper_led_data).
 * */
static ssize_t led_control_store(struct device *dev, struct device_attribute
				 *attr, const char *buf, size_t count)
{
	u64 tmp;
	int ret;

	ret = kstrtou64(buf, 16, &tmp);
	if (ret)
		return ret;

	u32 led_zone = (tmp >> (8 * 4));
	
	ret = casper_set(CASPER_SET_LED, led_zone, (u32) (tmp & 0xFFFFFFFF)
	    );
	if (ACPI_FAILURE(ret)) {
		dev_err(dev, "casper-wmi ACPI status: %d\n", ret);
		return ret;
	}
	if (led_zone != 7) {
		last_keyboard_led_change = (u32) (tmp & 0xFFFFFFFF);
		last_keyboard_led_zone = led_zone;
	}
	return count;
}

static DEVICE_ATTR_RW(led_control);

static struct attribute *casper_kbd_led_attrs[] = {
	&dev_attr_led_control.attr,
	NULL,
};

ATTRIBUTE_GROUPS(casper_kbd_led);

static void set_casper_backlight_brightness(struct led_classdev *led_cdev,
				     enum led_brightness brightness)
{
	// Setting any of the keyboard leds' brightness sets brightness of all
	acpi_status ret = casper_set(CASPER_SET_LED,
				     CASPER_KEYBOARD_LED_1,
				     (last_keyboard_led_change & 0xF0FFFFFF) |
				     (((u32) brightness) << 24)
	    );

	if (ret != 0)
		dev_err(led_cdev->dev,
			"Couldn't set brightness acpi status: %d\n", ret);
}

// Corner leds' brightness can be different from keyboard leds' but this is discarded.
static enum led_brightness get_casper_backlight_brightness(struct led_classdev
						    *led_cdev)
{
	return (last_keyboard_led_change & 0x0F000000) >> 24;
}

static struct led_classdev casper_kbd_led = {
	.name = "casper::kbd_backlight",
	.brightness = 0,
	.brightness_set = set_casper_backlight_brightness,
	.brightness_get = get_casper_backlight_brightness,
	.max_brightness = 2,
	.groups = casper_kbd_led_groups,
};

// HWMON PART v

enum casper_power_plan {
	HIGH_POWER = 1,
	GAMING = 2,
	TEXT_MODE = 3,
	LOW_POWER = 4
};

static acpi_status casper_query(struct wmi_device *wdev, u16 a1,
				struct casper_wmi_args *out)
{
	struct casper_wmi_args wmi_args = { 0 };
	wmi_args.a0 = CASPER_READ;
	wmi_args.a1 = a1;

	struct acpi_buffer input = {
		(acpi_size) sizeof(struct casper_wmi_args),
		&wmi_args
	};
	acpi_status ret = wmi_set_block(CASPER_WMI_GUID, 0, &input);
	if (ACPI_FAILURE(ret)) {
		dev_err(&wdev->dev,
			"Could not query (set phase), acpi status: %u", ret);
		return ret;
	}

	union acpi_object *obj = wmidev_block_query(wdev, 0);
	if (obj == NULL) {
		dev_err(&wdev->dev,
			"Could not query (query) hardware information");
		return AE_ERROR;
	}
	if (obj->type != ACPI_TYPE_BUFFER) {
		dev_err(&wdev->dev, "Return type is not a buffer");
		return AE_TYPE;
	}

	if (obj->buffer.length != 32) {
		dev_err(&wdev->dev, "Return buffer is not long enough");
		return AE_ERROR;
	}
	memcpy(out, obj->buffer.pointer, 32);
	kfree(obj);
	return ret;
}

static umode_t casper_wmi_hwmon_is_visible(const void *drvdata,
					   enum hwmon_sensor_types type,
					   u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:
		return 0444;	// read only
	case hwmon_pwm:
		return 0644;	// read and write
	default:
		return 0;
	}
	return 0;
}

static int casper_wmi_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long *val)
{
	struct casper_wmi_args out = { 0 };
	switch (type) {
	case hwmon_fan:
		acpi_status ret = casper_query(to_wmi_device(dev->parent),
					       CASPER_GET_HARDWAREINFO, &out);
		if (ACPI_FAILURE(ret))
			return ret;

		if (channel == 0) {	// CPU fan
			u16 cpu_fanspeed = (u16) out.a4;
                        if (!(*casper_raw_fanspeed)) {
                                cpu_fanspeed <<= (u16) 8;
                                cpu_fanspeed += (u16) (out.a4 >> 8);
                        }
			*val = cpu_fanspeed;
		} else if (channel == 1) {	// GPU fan
			u16 gpu_fanspeed = (u16) out.a5;
                        if (!(*casper_raw_fanspeed)) {
                                gpu_fanspeed <<= (u16) 8;
                                gpu_fanspeed += (u16) (out.a5 >> 8);
                        }
			*val = gpu_fanspeed;
		}
		return 0;
	case hwmon_pwm:
		casper_query(to_wmi_device(dev->parent), CASPER_POWERPLAN,
			     &out);
		if (channel == 0) {
			*val = (long)out.a2;
		} else {
			return -EOPNOTSUPP;
		}
		return 0;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int casper_wmi_hwmon_read_string(struct device *dev,
				 enum hwmon_sensor_types type, u32 attr,
				 int channel, const char **str)
{
	switch (type) {
	case hwmon_fan:
		switch (channel) {
		case 0:
			*str = "cpu_fan_speed";
			break;
		case 1:
			*str = "gpu_fan_speed";
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int casper_wmi_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long val)
{
	acpi_status ret;
	switch (type) {
	case hwmon_pwm:
		if (channel != 0)
			return -EOPNOTSUPP;
		ret = casper_set(CASPER_POWERPLAN, val, 0);
		printk("Writing started: %ld", val);
		if (ACPI_FAILURE(ret)) {
			dev_err(dev, "Couldn't set power plan, acpi_status: %d",
				ret);
			return -EINVAL;
		}
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static struct hwmon_ops casper_wmi_hwmon_ops = {
	.is_visible = &casper_wmi_hwmon_is_visible,
	.read = &casper_wmi_hwmon_read,
	.read_string = &casper_wmi_hwmon_read_string,
	.write = &casper_wmi_hwmon_write
};

static const struct hwmon_channel_info *const casper_wmi_hwmon_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_MODE),
	NULL
};

static const struct hwmon_chip_info casper_wmi_hwmon_chip_info = {
	.ops = &casper_wmi_hwmon_ops,
	.info = casper_wmi_hwmon_info,
};

static int casper_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct device *hwmon_dev;

	// All Casper Excalibur Laptops use this GUID
	if (!wmi_has_guid(CASPER_WMI_GUID))
		return -ENODEV;

	dmi_check_system(casper_dmi_list);
        
        if (casper_raw_fanspeed) {
                // This is to add their BIOS version to the dmi list
                dev_warn(&wdev->dev,
			 "If you are using an intel CPU older than 10th gen, contact driver maintainer.");
        }

	hwmon_dev =
	    devm_hwmon_device_register_with_info(&wdev->dev, "casper_wmi", wdev,
						 &casper_wmi_hwmon_chip_info,
						 NULL);

	acpi_status result = led_classdev_register(&wdev->dev, &casper_kbd_led);
	if (result != 0)
		return -ENODEV;

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static void casper_wmi_remove(struct wmi_device *wdev)
{
	led_classdev_unregister(&casper_kbd_led);
}

static const struct wmi_device_id casper_wmi_id_table[] = {
	{ CASPER_WMI_GUID, NULL },
	{ }
};

static struct wmi_driver casper_wmi_driver = {
	.driver = {
		   .name = "casper-wmi",
		    },
	.id_table = casper_wmi_id_table,
	.probe = casper_wmi_probe,
	.remove = &casper_wmi_remove
	    //void (*remove)(struct wmi_device *wdev);
};

module_wmi_driver(casper_wmi_driver);

MODULE_DEVICE_TABLE(wmi, casper_wmi_id_table);
