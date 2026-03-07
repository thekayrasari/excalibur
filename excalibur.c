// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/acpi.h>
#include <linux/bitfield.h>
#include <linux/dmi.h>
#include <linux/device.h>
#include <linux/hwmon.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wmi.h>

MODULE_AUTHOR("Kayra Sari <thekayrasari@gmail.com>");
MODULE_DESCRIPTION("Excalibur Laptop WMI driver");
MODULE_LICENSE("GPL");

#define EXCALIBUR_WMI_GUID		"644C5791-B7B0-4123-A90B-E93876E0DAAD"

/* WMI command codes */
#define EXCALIBUR_READ			0xfa00
#define EXCALIBUR_WRITE			0xfb00
#define EXCALIBUR_GET_HARDWAREINFO	0x0200
#define EXCALIBUR_SET_LED		0x0100
#define EXCALIBUR_POWERPLAN		0x0300

/* ================================================================
 * Keyboard RGB lighting — hardware protocol
 *
 * excalibur_set() takes (zone_id, data_word). The 32-bit data word
 * packs all LED attributes into four nibble/byte fields:
 *
 *   Bits [31:28]  mode      — animation mode  (nibble 1–6, see enum)
 *   Bits [27:24]  alpha     — brightness level 0–2
 *   Bits [23:16]  red       — 0–255
 *   Bits [15: 8]  green     — 0–255
 *   Bits [ 7: 0]  blue      — 0–255
 *
 * Zone IDs (zone_id arg to excalibur_set):
 *   0x03  left keyboard zone
 *   0x04  middle keyboard zone
 *   0x05  right keyboard zone
 *   0x06  all keyboard zones simultaneously (firmware broadcast)
 *   0x07  corner LEDs (independent brightness from keyboard zones)
 *
 * Animation modes (mode nibble values) — confirmed on G870 hardware:
 *   0x0  off        — LEDs off entirely
 *   0x1  static     — solid color, no animation
 *   0x2  blink      — on/off flashing
 *   0x3  fade       — smooth breathing (fade out then fade in)
 *   0x4  heartbeat  — double-pulse pattern
 *   0x5  wave       — color sweeps left to right (jumpy, not smooth)
 *   0x6  random     — random color per zone every ~1 second
 *   0x7  rainbow    — smooth rainbow cycle across all zones
 *   0x8+ overflow back into rainbow; 0x7 is the real ceiling
 *
 * Firmware notes:
 *   - Setting brightness on any keyboard zone propagates to all three.
 *   - Corner LED brightness is fully independent.
 *   - Firmware has no read-back for LED state; driver must cache it.
 * ================================================================ */

/* LED data word bit-fields */
#define EXCALIBUR_LED_MODE		GENMASK(31, 28)
#define EXCALIBUR_LED_ALPHA		GENMASK(27, 24)
#define EXCALIBUR_LED_RED		GENMASK(23, 16)
#define EXCALIBUR_LED_GREEN		GENMASK(15, 8)
#define EXCALIBUR_LED_BLUE		GENMASK(7, 0)

/* Zone identifiers */
#define EXCALIBUR_ZONE_LEFT		0x03
#define EXCALIBUR_ZONE_MIDDLE		0x04
#define EXCALIBUR_ZONE_RIGHT		0x05
#define EXCALIBUR_ZONE_ALL_KBD		0x06
#define EXCALIBUR_ZONE_CORNERS		0x07

#define EXCALIBUR_KBD_ZONE_COUNT	3  /* left, middle, right */
#define EXCALIBUR_ZONE_COUNT		4  /* keyboard zones + corners */
#define EXCALIBUR_KBD_MAX_BRIGHTNESS	2

/* Power plan values */
enum excalibur_power_plan {
	EXCALIBUR_PLAN_HIGH_POWER	= 1,
	EXCALIBUR_PLAN_GAMING		= 2,
	EXCALIBUR_PLAN_TEXT_MODE	= 3,
	EXCALIBUR_PLAN_LOW_POWER	= 4,
};

/*
 * Animation mode nibble values — confirmed by hardware brute-force on G870.
 * 0x0 turns LEDs off entirely.
 * Values 0x8–0xf overflow back into rainbow; 0x7 is the real ceiling.
 */
enum excalibur_led_mode {
	EXCALIBUR_MODE_OFF		= 0,
	EXCALIBUR_MODE_STATIC		= 1,
	EXCALIBUR_MODE_BLINK		= 2,
	EXCALIBUR_MODE_FADE		= 3,
	EXCALIBUR_MODE_HEARTBEAT	= 4,
	EXCALIBUR_MODE_WAVE		= 5, /* jumpy left-to-right color sweep */
	EXCALIBUR_MODE_RANDOM		= 6,
	EXCALIBUR_MODE_RAINBOW		= 7,
};

static const char * const excalibur_mode_names[] = {
	[EXCALIBUR_MODE_OFF]		= "off",
	[EXCALIBUR_MODE_STATIC]		= "static",
	[EXCALIBUR_MODE_BLINK]		= "blink",
	[EXCALIBUR_MODE_FADE]		= "fade",
	[EXCALIBUR_MODE_HEARTBEAT]	= "heartbeat",
	[EXCALIBUR_MODE_WAVE]		= "wave",
	[EXCALIBUR_MODE_RANDOM]		= "random",
	[EXCALIBUR_MODE_RAINBOW]	= "rainbow",
};

/* ================================================================
 * Data structures
 * ================================================================ */

struct excalibur_wmi_args {
	u16 a0, a1;
	u32 a2, a3, a4, a5, a6, rev0, rev1;
};

/**
 * struct excalibur_zone - per-zone LED state cache
 * @cdev:    LED class device (exposes standard brightness sysfs attr)
 * @zone_id: hardware zone identifier (EXCALIBUR_ZONE_*)
 * @mode:    cached animation mode (excalibur_led_mode nibble 1–6)
 * @r/g/b:   cached color components (0–255 each)
 *
 * The cdev.brightness field (0–2) caches the current brightness.
 * All fields are protected by excalibur_wmi_data.lock.
 */
struct excalibur_zone {
	struct led_classdev	cdev;
	u8			zone_id;
	u8			mode;
	u8			r, g, b;
};

/**
 * struct excalibur_wmi_data - driver state container (state pattern)
 * @wdev:             WMI device handle
 * @has_raw_fanspeed: false on older models that need byte-swap
 * @lock:             mutex protecting all zone state + HW access
 * @zones:            0=left 1=middle 2=right 3=corners
 */
struct excalibur_wmi_data {
	struct wmi_device	*wdev;
	bool			 has_raw_fanspeed;
	struct mutex		 lock;
	struct excalibur_zone	 zones[EXCALIBUR_ZONE_COUNT];
};

static const u8 excalibur_zone_ids[EXCALIBUR_ZONE_COUNT] = {
	EXCALIBUR_ZONE_LEFT,
	EXCALIBUR_ZONE_MIDDLE,
	EXCALIBUR_ZONE_RIGHT,
	EXCALIBUR_ZONE_CORNERS,
};

static const char * const excalibur_zone_names[EXCALIBUR_ZONE_COUNT] = {
	"excalibur::kbd_backlight-left",
	"excalibur::kbd_backlight-middle",
	"excalibur::kbd_backlight-right",
	"excalibur::kbd_backlight-corners",
};

/* ================================================================
 * DMI matching
 * ================================================================ */

static bool excalibur_has_raw_fanspeed = true;

static int dmi_matched(const struct dmi_system_id *dmi)
{
	excalibur_has_raw_fanspeed = (bool)(uintptr_t)dmi->driver_data;
	pr_info("excalibur-wmi: identified model '%s'\n", dmi->ident);
	return 1;
}

static const struct dmi_system_id excalibur_dmi_list[] = {
	{
		.callback    = dmi_matched,
		.ident       = "EXCALIBUR G650",
		.matches     = {
			DMI_MATCH(DMI_SYS_VENDOR,   "CASPER BILGISAYAR SISTEMLERI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G650"),
		},
		.driver_data = (void *)false,
	},
	{
		.callback    = dmi_matched,
		.ident       = "EXCALIBUR G750",
		.matches     = {
			DMI_MATCH(DMI_SYS_VENDOR,   "CASPER BILGISAYAR SISTEMLERI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G750"),
		},
		.driver_data = (void *)false,
	},
	{
		.callback    = dmi_matched,
		.ident       = "EXCALIBUR G670",
		.matches     = {
			DMI_MATCH(DMI_SYS_VENDOR,   "CASPER BILGISAYAR SISTEMLERI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G670"),
		},
		.driver_data = (void *)false,
	},
	{
		.callback    = dmi_matched,
		.ident       = "EXCALIBUR G900",
		.matches     = {
			DMI_MATCH(DMI_SYS_VENDOR,   "CASPER BILGISAYAR SISTEMLERI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G900"),
			DMI_MATCH(DMI_BIOS_VERSION, "CP131"),
		},
		.driver_data = (void *)false,
	},
	{
		.callback    = dmi_matched,
		.ident       = "EXCALIBUR G870",
		.matches     = {
			DMI_MATCH(DMI_SYS_VENDOR,   "CASPER BILGISAYAR SISTEMLERI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G870"),
			DMI_MATCH(DMI_BIOS_VERSION, "CQ141"),
		},
		.driver_data = (void *)true,
	},
	{
		.callback    = dmi_matched,
		.ident       = "EXCALIBUR G770",
		.matches     = {
			DMI_MATCH(DMI_SYS_VENDOR,   "CASPER BILGISAYAR SISTEMLERI"),
			DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR G770"),
			DMI_MATCH(DMI_BIOS_VERSION, "CP221"),
		},
		.driver_data = (void *)true,
	},
	{ }
};

/* ================================================================
 * WMI low-level helpers
 * ================================================================ */

static int excalibur_set(struct excalibur_wmi_data *drv, u16 cmd,
			 u8 zone_id, u32 data)
{
	struct excalibur_wmi_args args = {
		.a0 = EXCALIBUR_WRITE,
		.a1 = cmd,
		.a2 = zone_id,
		.a3 = data,
	};
	struct acpi_buffer input = { sizeof(args), &args };

	if (ACPI_FAILURE(wmidev_block_set(drv->wdev, 0, &input)))
		return -EIO;
	return 0;
}

static int excalibur_query(struct excalibur_wmi_data *drv, u16 cmd,
			   struct excalibur_wmi_args *out)
{
	struct excalibur_wmi_args args = {
		.a0 = EXCALIBUR_READ,
		.a1 = cmd,
	};
	struct acpi_buffer input = { sizeof(args), &args };
	union acpi_object *obj;

	if (ACPI_FAILURE(wmidev_block_set(drv->wdev, 0, &input)))
		return -EIO;

	obj = wmidev_block_query(drv->wdev, 0);
	if (!obj)
		return -EIO;

	if (obj->type != ACPI_TYPE_BUFFER ||
	    obj->buffer.length != sizeof(*out)) {
		kfree(obj);
		return -EIO;
	}

	memcpy(out, obj->buffer.pointer, sizeof(*out));
	kfree(obj);
	return 0;
}

/* ================================================================
 * Keyboard LED — core commit
 *
 * Assembles the 32-bit data word from a zone's full cached state
 * (mode + brightness + RGB) and sends it to hardware.
 * Must be called with drv->lock held.
 * ================================================================ */

static int excalibur_commit_zone(struct excalibur_wmi_data *drv,
				 struct excalibur_zone *zone)
{
	u32 data = FIELD_PREP(EXCALIBUR_LED_MODE,  zone->mode)           |
		   FIELD_PREP(EXCALIBUR_LED_ALPHA, zone->cdev.brightness) |
		   FIELD_PREP(EXCALIBUR_LED_RED,   zone->r)               |
		   FIELD_PREP(EXCALIBUR_LED_GREEN, zone->g)               |
		   FIELD_PREP(EXCALIBUR_LED_BLUE,  zone->b);

	return excalibur_set(drv, EXCALIBUR_SET_LED, zone->zone_id, data);
}

/* ================================================================
 * LED class device helpers
 * ================================================================ */

static struct excalibur_wmi_data *lcdev_to_drv(struct led_classdev *cdev)
{
	/* The LED core sets dev->parent to the device we registered under. */
	return dev_get_drvdata(cdev->dev->parent);
}

static struct excalibur_zone *lcdev_to_zone(struct led_classdev *cdev)
{
	return container_of(cdev, struct excalibur_zone, cdev);
}

/* ================================================================
 * LED brightness callbacks
 *
 * Keyboard zones: hardware propagates brightness to all three zones
 * whenever any one zone is written. Use ZONE_ALL_KBD to broadcast
 * the new brightness, and sync the cache for all kbd zones.
 *
 * Corners: independent, always commit just the corners zone.
 * ================================================================ */

static void excalibur_kbd_brightness_set(struct led_classdev *cdev,
					 enum led_brightness brightness)
{
	struct excalibur_wmi_data *drv = lcdev_to_drv(cdev);
	struct excalibur_zone *zone = lcdev_to_zone(cdev);
	u32 data;
	int i;

	mutex_lock(&drv->lock);

	/* Sync brightness cache for all keyboard zones. */
	for (i = 0; i < EXCALIBUR_KBD_ZONE_COUNT; i++)
		drv->zones[i].cdev.brightness = brightness;

	/*
	 * Broadcast to all keyboard zones at once.  We use this zone's
	 * color/mode since the firmware treats them as shared per broadcast.
	 */
	data = FIELD_PREP(EXCALIBUR_LED_MODE,  zone->mode)  |
	       FIELD_PREP(EXCALIBUR_LED_ALPHA, brightness)   |
	       FIELD_PREP(EXCALIBUR_LED_RED,   zone->r)      |
	       FIELD_PREP(EXCALIBUR_LED_GREEN, zone->g)      |
	       FIELD_PREP(EXCALIBUR_LED_BLUE,  zone->b);

	excalibur_set(drv, EXCALIBUR_SET_LED, EXCALIBUR_ZONE_ALL_KBD, data);

	mutex_unlock(&drv->lock);
}

static enum led_brightness excalibur_kbd_brightness_get(struct led_classdev *cdev)
{
	/* Firmware has no read-back; return cached value. */
	return lcdev_to_zone(cdev)->cdev.brightness;
}

static void excalibur_corner_brightness_set(struct led_classdev *cdev,
					    enum led_brightness brightness)
{
	struct excalibur_wmi_data *drv = lcdev_to_drv(cdev);
	struct excalibur_zone *zone = lcdev_to_zone(cdev);

	mutex_lock(&drv->lock);
	zone->cdev.brightness = brightness;
	excalibur_commit_zone(drv, zone);
	mutex_unlock(&drv->lock);
}

static enum led_brightness excalibur_corner_brightness_get(struct led_classdev *cdev)
{
	return lcdev_to_zone(cdev)->cdev.brightness;
}

/* ================================================================
 * Per-zone sysfs attributes: color, mode, available_modes
 *
 * These appear under each zone's LED device directory:
 *   /sys/class/leds/excalibur::kbd_backlight-{left|middle|right|corners}/
 *
 * color (write-only):
 *   Accepts a 6-digit RRGGBB hex string.
 *   Example: echo FF0080 > color
 *
 * mode (read-write):
 *   Accepts an animation mode name string.
 *   Example: echo fade > mode
 *   Reading returns the current mode name.
 *
 * available_modes (read-only):
 *   Lists all valid mode names separated by spaces.
 * ================================================================ */

static ssize_t color_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct excalibur_wmi_data *drv = lcdev_to_drv(cdev);
	struct excalibur_zone *zone = lcdev_to_zone(cdev);
	unsigned int rgb;
	int ret;

	ret = kstrtouint(skip_spaces(buf), 16, &rgb);
	if (ret)
		return ret;
	if (rgb > 0xFFFFFF)
		return -ERANGE;

	mutex_lock(&drv->lock);
	zone->r = (rgb >> 16) & 0xFF;
	zone->g = (rgb >>  8) & 0xFF;
	zone->b =  rgb        & 0xFF;
	ret = excalibur_commit_zone(drv, zone);
	mutex_unlock(&drv->lock);

	return ret ? ret : count;
}

static DEVICE_ATTR_WO(color);

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct excalibur_zone *zone = lcdev_to_zone(cdev);

	if (zone->mode > EXCALIBUR_MODE_RAINBOW)
		return sysfs_emit(buf, "unknown\n");

	return sysfs_emit(buf, "%s\n", excalibur_mode_names[zone->mode]);
}

static ssize_t mode_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct excalibur_wmi_data *drv = lcdev_to_drv(cdev);
	struct excalibur_zone *zone = lcdev_to_zone(cdev);
	char name[16];
	int i, ret;

	strscpy(name, skip_spaces(buf), sizeof(name));
	name[strcspn(name, " \n\t")] = '\0';

	for (i = EXCALIBUR_MODE_OFF; i <= EXCALIBUR_MODE_RAINBOW; i++) {
		if (strcmp(name, excalibur_mode_names[i]) != 0)
			continue;
		mutex_lock(&drv->lock);
		zone->mode = i;
		ret = excalibur_commit_zone(drv, zone);
		mutex_unlock(&drv->lock);
		return ret ? ret : count;
	}

	return -EINVAL;
}

static DEVICE_ATTR_RW(mode);

static ssize_t available_modes_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	int i, len = 0;

	for (i = EXCALIBUR_MODE_OFF; i <= EXCALIBUR_MODE_RAINBOW; i++)
		len += sysfs_emit_at(buf, len, "%s%s",
				     excalibur_mode_names[i],
				     i < EXCALIBUR_MODE_RAINBOW ? " " : "\n");
	return len;
}

static DEVICE_ATTR_RO(available_modes);

/*
 * raw (debug, write-only):
 *   Sends a full 32-bit hex data word straight to this zone's hardware
 *   register, bypassing all field parsing. Use this to probe unknown
 *   mode/layout values on new hardware.
 *
 *   Format: 8 hex digits, e.g. "12FF0000"
 *     [31:28] mode nibble
 *     [27:24] brightness nibble
 *     [23:16] red
 *     [15: 8] green
 *     [ 7: 0] blue
 *
 *   Example — probe mode nibble 0x7 with full brightness and red:
 *     echo 72FF0000 | sudo tee .../raw
 */
static ssize_t raw_store(struct device *dev, struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct led_classdev *cdev = dev_get_drvdata(dev);
	struct excalibur_wmi_data *drv = lcdev_to_drv(cdev);
	struct excalibur_zone *zone = lcdev_to_zone(cdev);
	u32 data;
	int ret;

	ret = kstrtou32(skip_spaces(buf), 16, &data);
	if (ret)
		return ret;

	mutex_lock(&drv->lock);
	ret = excalibur_set(drv, EXCALIBUR_SET_LED, zone->zone_id, data);
	mutex_unlock(&drv->lock);

	dev_info(dev, "raw: zone=0x%02x data=0x%08x ret=%d\n",
		 zone->zone_id, data, ret);

	return ret ? ret : count;
}

static DEVICE_ATTR_WO(raw);

static struct attribute *excalibur_zone_attrs[] = {
	&dev_attr_color.attr,
	&dev_attr_mode.attr,
	&dev_attr_available_modes.attr,
	&dev_attr_raw.attr,
	NULL,
};

ATTRIBUTE_GROUPS(excalibur_zone);

/* ================================================================
 * hwmon — fan speed monitoring + power plan control
 * ================================================================ */

static u16 excalibur_decode_fanspeed(struct excalibur_wmi_data *drv, u32 raw)
{
	u16 val = (u16)raw;

	if (!drv->has_raw_fanspeed)
		val = (val << 8) | (raw >> 8);
	return val;
}

static umode_t excalibur_hwmon_is_visible(const void *drvdata,
					  enum hwmon_sensor_types type,
					  u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:	return 0444;
	case hwmon_pwm:	return 0644;
	default:	return 0;
	}
}

static int excalibur_hwmon_read(struct device *dev, enum hwmon_sensor_types type,
				u32 attr, int channel, long *val)
{
	struct excalibur_wmi_data *drv = dev_get_drvdata(dev->parent);
	struct excalibur_wmi_args out = { 0 };
	int ret;

	switch (type) {
	case hwmon_fan:
		if (channel > 1)
			return -EINVAL;
		ret = excalibur_query(drv, EXCALIBUR_GET_HARDWAREINFO, &out);
		if (ret)
			return ret;
		*val = excalibur_decode_fanspeed(drv,
						 channel == 0 ? out.a4 : out.a5);
		return 0;

	case hwmon_pwm:
		if (channel != 0)
			return -EOPNOTSUPP;
		ret = excalibur_query(drv, EXCALIBUR_POWERPLAN, &out);
		if (ret)
			return ret;
		*val = (long)out.a2;
		return 0;

	default:
		return -EOPNOTSUPP;
	}
}

static int excalibur_hwmon_read_string(struct device *dev,
				       enum hwmon_sensor_types type, u32 attr,
				       int channel, const char **str)
{
	static const char * const fan_labels[] = { "cpu_fan", "gpu_fan" };

	if (type != hwmon_fan || channel >= ARRAY_SIZE(fan_labels))
		return -EOPNOTSUPP;

	*str = fan_labels[channel];
	return 0;
}

static int excalibur_hwmon_write(struct device *dev, enum hwmon_sensor_types type,
				 u32 attr, int channel, long val)
{
	struct excalibur_wmi_data *drv = dev_get_drvdata(dev->parent);

	if (type != hwmon_pwm || channel != 0)
		return -EOPNOTSUPP;

	if (val < EXCALIBUR_PLAN_HIGH_POWER || val > EXCALIBUR_PLAN_LOW_POWER)
		return -EINVAL;

	return excalibur_set(drv, EXCALIBUR_POWERPLAN, (u32)val, 0);
}

static const struct hwmon_ops excalibur_hwmon_ops = {
	.is_visible  = excalibur_hwmon_is_visible,
	.read        = excalibur_hwmon_read,
	.read_string = excalibur_hwmon_read_string,
	.write       = excalibur_hwmon_write,
};

static const struct hwmon_channel_info *const excalibur_hwmon_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	HWMON_CHANNEL_INFO(pwm, HWMON_PWM_MODE),
	NULL
};

static const struct hwmon_chip_info excalibur_hwmon_chip_info = {
	.ops  = &excalibur_hwmon_ops,
	.info = excalibur_hwmon_info,
};

/* ================================================================
 * Driver probe
 * ================================================================ */

static int excalibur_wmi_probe(struct wmi_device *wdev, const void *context)
{
	struct excalibur_wmi_data *drv;
	struct device *hwmon_dev;
	int i, ret;

	if (!wmi_has_guid(EXCALIBUR_WMI_GUID))
		return -ENODEV;

	drv = devm_kzalloc(&wdev->dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drv->wdev = wdev;
	dev_set_drvdata(&wdev->dev, drv);

	ret = devm_mutex_init(&wdev->dev, &drv->lock);
	if (ret)
		return ret;

	dmi_check_system(excalibur_dmi_list);
	drv->has_raw_fanspeed = excalibur_has_raw_fanspeed;

	if (drv->has_raw_fanspeed)
		dev_warn(&wdev->dev,
			 "Unrecognised model — if you have an Intel CPU older "
			 "than 10th gen, contact the driver maintainer.\n");

	for (i = 0; i < EXCALIBUR_ZONE_COUNT; i++) {
		struct excalibur_zone *zone = &drv->zones[i];
		bool is_corner = (i == EXCALIBUR_ZONE_COUNT - 1);

		zone->zone_id          = excalibur_zone_ids[i];
		zone->mode             = EXCALIBUR_MODE_STATIC;
		zone->r                = 0xFF; /* default: white */
		zone->g                = 0xFF;
		zone->b                = 0xFF;
		zone->cdev.name        = excalibur_zone_names[i];
		zone->cdev.max_brightness = EXCALIBUR_KBD_MAX_BRIGHTNESS;
		zone->cdev.brightness  = 0;
		zone->cdev.groups      = excalibur_zone_groups;

		if (is_corner) {
			zone->cdev.brightness_set = excalibur_corner_brightness_set;
			zone->cdev.brightness_get = excalibur_corner_brightness_get;
		} else {
			zone->cdev.brightness_set = excalibur_kbd_brightness_set;
			zone->cdev.brightness_get = excalibur_kbd_brightness_get;
		}

		ret = devm_led_classdev_register(&wdev->dev, &zone->cdev);
		if (ret)
			return ret;
	}

	hwmon_dev = devm_hwmon_device_register_with_info(&wdev->dev,
							 "excalibur_wmi", drv,
							 &excalibur_hwmon_chip_info,
							 NULL);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct wmi_device_id excalibur_wmi_id_table[] = {
	{ .guid_string = EXCALIBUR_WMI_GUID },
	{ }
};

static struct wmi_driver excalibur_wmi_driver = {
	.driver   = { .name = "excalibur-wmi" },
	.id_table = excalibur_wmi_id_table,
	.probe    = excalibur_wmi_probe,
};

module_wmi_driver(excalibur_wmi_driver);

MODULE_DEVICE_TABLE(wmi, excalibur_wmi_id_table);
