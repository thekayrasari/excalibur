#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/types.h>
#include <linux/wmi.h>
#include <acpi/acexcep.h>

MODULE_AUTHOR("Kayra Sari <sarikayra@proton.me>");
MODULE_DESCRIPTION("Excalibur Laptop WMI driver");
MODULE_LICENSE("GPL");

#define EXCALIBUR_WMI_GUID "644C5791-B7B0-4123-A90B-E93876E0DAAD"

#define EXCALIBUR_KEYBOARD_LED_1 0x03
#define EXCALIBUR_KEYBOARD_LED_2 0x04
#define EXCALIBUR_KEYBOARD_LED_3 0x05
#define EXCALIBUR_ALL_KEYBOARD_LEDS 0x06
#define EXCALIBUR_CORNER_LEDS 0x07

#define EXCALIBUR_READ 0xfa00
#define EXCALIBUR_WRITE 0xfb00
#define EXCALIBUR_GET_HARDWAREINFO 0x0200
#define EXCALIBUR_GET_BIOSVER 0x0201
#define EXCALIBUR_SET_LED 0x0100
#define EXCALIBUR_POWERPLAN 0x0300

struct excalibur_wmi_args {
	u16 a0, a1;
	u32 a2, a3, a4, a5, a6, rev0, rev1;
};

static u32 last_keyboard_led_change;
static u32 last_keyboard_led_zone;
static bool excalibur_has_raw_fanspeed = true; /* Default to true if no DMI match */

static int dmi_matched(const struct dmi_system_id *dmi)
{
	excalibur_has_raw_fanspeed = (bool)(uintptr_t)dmi->driver_data;
	dev_info(NULL, "Identified laptop model '%s'\n", dmi->ident);
	return 1;
}

static const struct dmi_system_id excalibur_dmi_list[] = {
	{
		.callback = dmi_matched,
		.ident = "EXCALIBUR G650",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "EXCALIBUR BILGISAYAR SISTEMLERI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G650")
		},
		.driver_data = (void *)false,
	},
	{
		.callback = dmi_matched,
		.ident = "EXCALIBUR G750",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "EXCALIBUR BILGISAYAR SISTEMLERI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G750")
		},
		.driver_data = (void *)false,
	},
	{
		.callback = dmi_matched,
		.ident = "EXCALIBUR G670",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "EXCALIBUR BILGISAYAR SISTEMLERI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G670")
		},
		.driver_data = (void *)false,
	},
	{
		.callback = dmi_matched,
		.ident = "EXCALIBUR G900",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "EXCALIBUR BILGISAYAR SISTEMLERI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G900"),
			DMI_MATCH(DMI_BIOS_VERSION, "CP131")
		},
		.driver_data = (void *)false,
	},
	{ }
};

static acpi_status excalibur_set(u16 a1, u32 zone_id, u32 data)
{
	struct excalibur_wmi_args wmi_args = { 0 };
	wmi_args.a0 = EXCALIBUR_WRITE;
	wmi_args.a1 = a1;
	wmi_args.a2 = zone_id;
	wmi_args.a3 = data;

	struct acpi_buffer input = {
		(acpi_size)sizeof(struct excalibur_wmi_args),
		&wmi_args
	};
	return wmi_set_block(EXCALIBUR_WMI_GUID, 0, &input);
}

static ssize_t led_control_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return -EOPNOTSUPP;
}

static ssize_t led_control_store(struct device *dev, struct device_attribute *attr,
				 const char *buf, size_t count)
{
	u64 tmp;
	int ret;

	ret = kstrtou64(buf, 16, &tmp);
	if (ret)
		return ret;

	u32 led_zone = (tmp >> 32);
	u32 led_data = (u32)(tmp & 0xFFFFFFFF);

	acpi_status status = excalibur_set(EXCALIBUR_SET_LED, led_zone, led_data);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "Failed to set LED: ACPI status %u\n", status);
		return -EIO;
	}

	if (led_zone != EXCALIBUR_CORNER_LEDS) {
		last_keyboard_led_change = led_data;
		last_keyboard_led_zone = led_zone;
	}
	return count;
}

static DEVICE_ATTR_RW(led_control);

static struct attribute *excalibur_kbd_led_attrs[] = {
	&dev_attr_led_control.attr,
	NULL,
};

ATTRIBUTE_GROUPS(excalibur_kbd_led);

static void set_excalibur_backlight_brightness(struct led_classdev *led_cdev,
					       enum led_brightness brightness)
{
	acpi_status status = excalibur_set(EXCALIBUR_SET_LED,
					  EXCALIBUR_KEYBOARD_LED_1,
					  (last_keyboard_led_change & 0xF0FFFFFF) |
					  ((u32)brightness << 24));

	if (ACPI_FAILURE(status))
		dev_err(led_cdev->dev, "Failed to set brightness: ACPI status %u\n", status);
}

static enum led_brightness get_excalibur_backlight_brightness(struct led_classdev *led_cdev)
{
	return (last_keyboard_led_change & 0x0F000000) >> 24;
}

static struct led_classdev excalibur_kbd_led = {
	.name = "excalibur::kbd_backlight",
	.brightness = 0,
	.brightness_set = set_excalibur_backlight_brightness,
	.brightness_get = get_excalibur_backlight_brightness,
	.max_brightness = 2,
	.groups = excalibur_kbd_led_groups,
};

enum excalibur_power_plan {
	HIGH_POWER = 1,
	GAMING = 2,
	TEXT_MODE = 3,
	LOW_POWER = 4
};

static acpi_status excalibur_query(struct wmi_device *wdev, u16 a1,
				   struct excalibur_wmi_args *out)
{
	struct excalibur_wmi_args wmi_args = { 0 };
	wmi_args.a0 = EXCALIBUR_READ;
	wmi_args.a1 = a1;

	struct acpi_buffer input = {
		(acpi_size)sizeof(struct excalibur_wmi_args),
		&wmi_args
	};

	acpi_status status = wmi_set_block(EXCALIBUR_WMI_GUID, 0, &input);
	if (ACPI_FAILURE(status)) {
		dev_err(&wdev->dev, "Failed to set query mode: ACPI status %u\n", status);
		return status;
	}

	union acpi_object *obj = wmidev_block_query(wdev, 0);
	if (!obj) {
		dev_err(&wdev->dev, "Failed to query WMI block\n");
		return AE_ERROR;
	}

	if (obj->type != ACPI_TYPE_BUFFER) {
		dev_err(&wdev->dev, "Query result is not a buffer\n");
		kfree(obj);
		return AE_TYPE;
	}

	if (obj->buffer.length != sizeof(*out)) {
		dev_err(&wdev->dev, "Query buffer length mismatch: got %u, expected %zu\n",
			obj->buffer.length, sizeof(*out));
		kfree(obj);
		return AE_ERROR;
	}

	memcpy(out, obj->buffer.pointer, sizeof(*out));
	kfree(obj);
	return AE_OK;
}

static umode_t excalibur_wmi_hwmon_is_visible(const void *drvdata,
					      enum hwmon_sensor_types type,
					      u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:
		return 0444; /* Read-only */
	case hwmon_pwm:
		return 0644; /* Read-write */
	default:
		return 0;
	}
}

static int excalibur_wmi_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
				    u32 attr, int channel, long *val)
{
	struct wmi_device *wdev = to_wmi_device(dev->parent);
	struct excalibur_wmi_args out = { 0 };
	acpi_status status;

	switch (type) {
	case hwmon_fan:
		status = excalibur_query(wdev, EXCALIBUR_GET_HARDWAREINFO, &out);
		if (ACPI_FAILURE(status))
			return -EIO;

		if (channel == 0) { /* CPU fan */
			u16 fanspeed = (u16)out.a4;
			if (!excalibur_has_raw_fanspeed) {
				fanspeed = (fanspeed << 8) | (out.a4 >> 8);
			}
			*val = fanspeed;
		} else if (channel == 1) { /* GPU fan */
			u16 fanspeed = (u16)out.a5;
			if (!excalibur_has_raw_fanspeed) {
				fanspeed = (fanspeed << 8) | (out.a5 >> 8);
			}
			*val = fanspeed;
		} else {
			return -EINVAL;
		}
		return 0;

	case hwmon_pwm:
		if (channel != 0)
			return -EOPNOTSUPP;

		status = excalibur_query(wdev, EXCALIBUR_POWERPLAN, &out);
		if (ACPI_FAILURE(status))
			return -EIO;

		*val = (long)out.a2;
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static int excalibur_wmi_hwmon_read_string(struct device *dev,
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

static int excalibur_wmi_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
				     u32 attr, int channel, long val)
{
	if (type != hwmon_pwm || channel != 0)
		return -EOPNOTSUPP;

	acpi_status status = excalibur_set(EXCALIBUR_POWERPLAN, (u32)val, 0);
	if (ACPI_FAILURE(status)) {
		dev_err(dev, "Failed to set power plan: ACPI status %u\n", status);
		return -EIO;
	}
	return 0;
}

static const struct hwmon_ops excalibur_wmi_hwmon_ops = {
	.is_visible = excalibur_wmi_hwmon_is_visible,
	.read = excalibur_wmi_hwmon_read,
	.read_string = excalibur_wmi_hwmon_read_string,
	.write = excalibur_wmi_hwmon_write,
};

static const struct hwmon_channel_info *const excalibur_wmi_hwmon_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_MODE),
	NULL
};

static const struct hwmon_chip_info excalibur_wmi_hwmon_chip_info = {
	.ops = &excalibur_wmi_hwmon_ops,
	.info = excalibur_wmi_hwmon_info,
};

static int excalibur_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct device *hwmon_dev;
	int ret;

	if (!wmi_has_guid(EXCALIBUR_WMI_GUID))
		return -ENODEV;

	dmi_check_system(excalibur_dmi_list);

	if (excalibur_has_raw_fanspeed)
		dev_warn(&wdev->dev,
			 "If you are using an Intel CPU older than 10th gen, contact the driver maintainer.\n");

	hwmon_dev = devm_hwmon_device_register_with_info(&wdev->dev, "excalibur_wmi",
							 wdev, &excalibur_wmi_hwmon_chip_info, NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	ret = devm_led_classdev_register(&wdev->dev, &excalibur_kbd_led);
	if (ret)
		return ret;

	return 0;
}

static void excalibur_wmi_remove(struct wmi_device *wdev)
{
	devm_led_classdev_unregister(&wdev->dev, &excalibur_kbd_led);
}

static const struct wmi_device_id excalibur_wmi_id_table[] = {
	{ .guid_string = EXCALIBUR_WMI_GUID },
	{ }
};

static struct wmi_driver excalibur_wmi_driver = {
	.driver = {
		.name = "excalibur-wmi",
	},
	.id_table = excalibur_wmi_id_table,
	.probe = excalibur_wmi_probe,
	.remove = excalibur_wmi_remove,
};

module_wmi_driver(excalibur_wmi_driver);

MODULE_DEVICE_TABLE(wmi, excalibur_wmi_id_table);

