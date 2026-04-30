# excalibur wmi driver

Linux kernel WMI driver for Excalibur gaming laptops.  
Provides per-zone RGB keyboard control, fan speed monitoring, and power plan management via the ACPI/WMI interface.

---

## Table of Contents

- [Supported Models](#supported-models)
- [Prerequisites](#prerequisites)
- [Installation](#installation)
- [Driver Architecture](#driver-architecture)
- [Hardware Protocol](#hardware-protocol)
- [Sysfs Interface](#sysfs-interface)
- [Keyboard Lighting](#keyboard-lighting)
- [Fan Monitoring](#fan-monitoring)
- [Power Plans](#power-plans)
- [Profile Scripts](#profile-scripts)
- [Persistent Boot Configuration](#persistent-boot-configuration)
- [Control Panel](#control-panel)
- [Debugging](#debugging)
- [Adding New Models](#adding-new-models)
- [Known Limitations](#known-limitations)

---

## Supported Models

| Model | BIOS | `has_raw_fanspeed` | Status |
|---|---|---|---|
| EXCALIBUR G650 | any | `false` | Supported |
| EXCALIBUR G670 | any | `false` | Supported |
| EXCALIBUR G750 | any | `false` | Supported |
| EXCALIBUR G900 | CP131 | `false` | Supported |
| EXCALIBUR G870 | CQ141 | `true` | Supported |
| EXCALIBUR G770 | CP221 | `true` | Supported |

If your model is not listed, the driver will still load and function but will emit a warning in `dmesg`. See [Adding New Models](#adding-new-models).

The `has_raw_fanspeed` flag controls whether the fan speed register value is used as-is (`true`) or needs a byte-swap (`false`). Older models with Intel 10th gen CPUs or earlier return fan speed in big-endian byte order; newer models return it natively. The DMI table encodes the correct value per model so no user configuration is needed.

---

## Prerequisites

**Kernel version:** 5.15 minimum. `devm_mutex_init()` requires 6.4+. If you are on an older kernel, replace `devm_mutex_init()` with `mutex_init()` and add a manual `mutex_destroy()` in a `remove` callback.

**Build tools:**

```bash
# Ubuntu / Debian
sudo apt install build-essential linux-headers-$(uname -r)

# Arch / Manjaro
sudo pacman -S base-devel linux-headers

# Fedora
sudo dnf install make gcc kernel-devel
```

**Clang-built kernels (CachyOS, some Arch configs):**

The installer detects the compiler used to build the running kernel from `/proc/version` and sets the correct flags automatically. If you are building manually on a clang-built kernel, use:

```bash
make CC=clang LLVM=1 LLVM_IAS=1
```

You will also need the `clang` and `llvm` packages in addition to `make`. Using `gcc` on a clang-built kernel risks ABI issues and is not supported.

**Verify WMI GUID is exposed by firmware:**

```bash
ls /sys/bus/wmi/devices/ | grep 644C5791
```

If nothing is returned, the firmware does not expose the WMI interface and the driver will not bind. This is a firmware limitation and cannot be worked around in software.

---

## Installation

### Arch-based distros (CachyOS, Manjaro, EndeavourOS)

The headers package name must match your kernel variant. Find yours with `uname -r` and install the corresponding package before running the installer:

| `uname -r` contains | Headers package |
|---|---|
| `cachyos` | `linux-cachyos-headers` |
| `cachyos-lts` | `linux-cachyos-lts-headers` |
| `cachyos-bore` | `linux-cachyos-bore-headers` |
| `cachyos-hardened` | `linux-cachyos-hardened-headers` |
| `manjaro` | `linux-headers` (via Manjaro Settings Manager) |

```bash
# Example for the default CachyOS kernel
sudo pacman -S linux-cachyos-headers
```

Then proceed with the standard install:

```bash
sudo ./install.sh install
```

The installer detects that CachyOS kernels are built with clang and passes `CC=clang LLVM=1 LLVM_IAS=1` to `make` automatically.

### Temporary (current session only, safe for testing)

```bash
git clone https://github.com/thekayrasari/excalibur
cd excalibur
make
sudo insmod excalibur.ko
```

The module will be unloaded on reboot. Nothing is written to `/lib/modules/`.

### Permanent

```bash
sudo ./install.sh install
```

This builds the module, copies it to `/lib/modules/$(uname -r)/extra/`, runs `depmod -a`, writes `/etc/modules-load.d/excalibur.conf` for auto-loading, and updates initramfs.

### Uninstall

```bash
sudo ./install.sh uninstall
```

Or manually:

```bash
sudo rmmod excalibur
sudo rm /lib/modules/$(uname -r)/extra/excalibur.ko
sudo rm /etc/modules-load.d/excalibur.conf
sudo depmod -a
```

---

## Driver Architecture

### State Container

All mutable driver state lives in `struct excalibur_wmi_data`, allocated with `devm_kzalloc` during probe and attached to the WMI device via `dev_set_drvdata`. This eliminates global variables and makes the driver safe for concurrent sysfs access.

```c
struct excalibur_wmi_data {
    struct wmi_device    *wdev;
    bool                  has_raw_fanspeed;
    struct mutex          lock;             /* protects zones[] + HW access */
    struct excalibur_zone zones[4];         /* left, middle, right, corners */
};
```

### Per-Zone LED State

Each of the four hardware LED zones has its own `struct excalibur_zone`, which embeds a `struct led_classdev` and caches the zone's current color, mode, and brightness. The LED core registers each zone as a separate device under `/sys/class/leds/`.

```c
struct excalibur_zone {
    struct led_classdev  cdev;     /* brightness + extra groups */
    u8                   zone_id;  /* hardware zone ID (0x03–0x07) */
    u8                   mode;     /* animation mode nibble (0–7) */
    u8                   r, g, b;  /* color cache (0–255 each) */
};
```

Brightness is stored in `cdev.brightness` (0–2) and is part of the same state.

### Commit Path

Any change to color, mode, or brightness goes through `excalibur_commit_zone()`, which assembles the complete 32-bit data word from cached zone state using `FIELD_PREP` and sends it to hardware via `excalibur_set()`. The mutex is always held across this operation.

```
user writes sysfs attr
    → validate input
    → mutex_lock
    → update zone cache
    → excalibur_commit_zone()
        → FIELD_PREP(mode | alpha | R | G | B)
        → excalibur_set()
            → wmidev_block_set()
    → mutex_unlock
```

### WMI Transport

`excalibur_set()` writes a command to the WMI block. `excalibur_query()` writes a read command then calls `wmidev_block_query()` to retrieve the result. Both operate on `struct excalibur_wmi_args`:

```c
struct excalibur_wmi_args {
    u16 a0;   /* direction: 0xfa00 READ, 0xfb00 WRITE */
    u16 a1;   /* command:   0x0100 SET_LED, 0x0200 GET_HARDWAREINFO, 0x0300 POWERPLAN */
    u32 a2;   /* zone_id (for LED) or plan value (for POWERPLAN) */
    u32 a3;   /* data word (for LED SET) */
    u32 a4;   /* CPU fan speed (in query response) */
    u32 a5;   /* GPU fan speed (in query response) */
    u32 a6, rev0, rev1;
};
```

---

## Hardware Protocol

### WMI GUID

```
644C5791-B7B0-4123-A90B-E93876E0DAAD
```

### LED Data Word Layout

The 32-bit data word passed to `excalibur_set()` for `EXCALIBUR_SET_LED` encodes all LED attributes in four consecutive fields:

```
 31      28 27      24 23    16 15     8 7      0
 ┌─────────┬──────────┬────────┬────────┬────────┐
 │  mode   │  alpha   │  red   │ green  │  blue  │
 │ [31:28] │ [27:24]  │[23:16] │ [15:8] │  [7:0] │
 └─────────┴──────────┴────────┴────────┴────────┘
   nibble    nibble     byte     byte     byte
```

Defined in the driver using `GENMASK`:

```c
#define EXCALIBUR_LED_MODE      GENMASK(31, 28)
#define EXCALIBUR_LED_ALPHA     GENMASK(27, 24)
#define EXCALIBUR_LED_RED       GENMASK(23, 16)
#define EXCALIBUR_LED_GREEN     GENMASK(15, 8)
#define EXCALIBUR_LED_BLUE      GENMASK(7, 0)
```

Example: red, full brightness, static mode:

```
mode=0x1, alpha=0x2, R=0xFF, G=0x00, B=0x00
→ data word = 0x12FF0000
```

### Zone IDs

| ID | Zone |
|---|---|
| `0x03` | Left keyboard zone |
| `0x04` | Middle keyboard zone |
| `0x05` | Right keyboard zone |
| `0x06` | All keyboard zones broadcast (firmware-side, sets all three at once) |
| `0x07` | Corner LEDs |

Zone `0x06` is used internally by the driver when a brightness change is issued on any keyboard zone, since the firmware propagates brightness to all three keyboard zones regardless of which one is written. The driver uses this to keep the cache consistent.

### Animation Mode Nibble Values

Confirmed by hardware brute-force on G870 (BIOS CQ141). The mode occupies bits [31:28] of the data word.

| Nibble | Name | Behavior |
|---|---|---|
| `0x0` | `off` | LEDs off entirely |
| `0x1` | `static` | Solid color, no animation |
| `0x2` | `blink` | On/off flashing |
| `0x3` | `fade` | Smooth breathing — fades out then back in |
| `0x4` | `heartbeat` | Double-pulse pattern |
| `0x5` | `wave` | Color sweeps left to right across zones (discrete steps, not smooth) |
| `0x6` | `random` | Each zone assigned a random color every ~1 second |
| `0x7` | `rainbow` | Smooth rainbow cycle across all zones |
| `0x8–0xf` | — | Overflow; hardware falls back to rainbow |

> **Note:** Mode `0x7` (rainbow) was discovered by hardware brute-force on the G870 and is not documented in any upstream patch or public firmware reference. All prior documentation of this WMI protocol stopped at mode `0x6`.

### Power Plan Values

Sent as `a2` in a `EXCALIBUR_POWERPLAN` write command:

| Value | Plan |
|---|---|
| `1` | High Power |
| `2` | Gaming |
| `3` | Text Mode |
| `4` | Low Power |

### Fan Speed Encoding

Fan speeds are returned in `a4` (CPU) and `a5` (GPU) of a `GET_HARDWAREINFO` query response, in RPM.

On older models (`has_raw_fanspeed = false`), the 16-bit value is returned in big-endian byte order and must be swapped:

```c
val = (val << 8) | (raw >> 8);
```

On newer models (`has_raw_fanspeed = true`), the value is used directly.

---

## Sysfs Interface

### LED Devices

Four LED class devices are registered, one per zone:

```
/sys/class/leds/excalibur::kbd_backlight-left/
/sys/class/leds/excalibur::kbd_backlight-middle/
/sys/class/leds/excalibur::kbd_backlight-right/
/sys/class/leds/excalibur::kbd_backlight-corners/
```

Each exposes the following attributes:

| Attribute | Access | Description |
|---|---|---|
| `brightness` | RW | Brightness level: `0`, `1`, or `2` |
| `max_brightness` | RO | Always `2` |
| `color` | WO | 6-digit hex RGB: `RRGGBB` |
| `mode` | RW | Animation mode name (see table above) |
| `available_modes` | RO | Space-separated list of valid mode names |
| `raw` | WO | Debug: send raw 32-bit hex data word directly to hardware |

### hwmon Device

Registered as `excalibur_wmi` under `/sys/class/hwmon/hwmon*/`:

| File | Access | Description |
|---|---|---|
| `fan1_input` | RO | CPU fan speed in RPM |
| `fan1_label` | RO | `cpu_fan` |
| `fan2_input` | RO | GPU fan speed in RPM |
| `fan2_label` | RO | `gpu_fan` |
| `pwm1` | RW | Power plan (1–4, see table above) |

---

## Keyboard Lighting

### Color

Write a 6-digit hex RGB value. No `#` prefix.

```bash
echo FF0000 | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/color   # red
echo 00FF00 | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/color   # green
echo 0000FF | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/color   # blue
echo FFFFFF | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/color   # white
echo 000000 | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/color   # black (off)
echo FF8000 | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/color   # orange
echo FF00FF | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/color   # magenta
echo 00FFFF | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/color   # cyan
echo 800080 | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/color   # purple
```

Color has no effect in `rainbow` or `random` mode since the firmware overrides it.

### Brightness

```bash
echo 0 | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/brightness  # off
echo 1 | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/brightness  # medium
echo 2 | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/brightness  # full
```

Writing brightness to any keyboard zone (`left`, `middle`, `right`) propagates to all three via zone ID `0x06`. The corners zone has fully independent brightness.

### Mode

```bash
echo off       | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/mode
echo static    | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/mode
echo blink     | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/mode
echo fade      | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/mode
echo heartbeat | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/mode
echo wave      | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/mode
echo random    | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/mode
echo rainbow   | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/mode

# Read current mode
cat /sys/class/leds/excalibur::kbd_backlight-left/mode

# List all valid modes
cat /sys/class/leds/excalibur::kbd_backlight-left/available_modes
```

### Raw Debug Attribute

Sends an arbitrary 32-bit data word directly to the hardware zone register, bypassing all validation and field parsing. Useful when testing new models or probing unknown mode values.

```bash
# Format: 8 hex digits = {mode nibble}{brightness nibble}{RR}{GG}{BB}
# Example: mode=0x7 (rainbow), brightness=0x2 (full), color=white
echo 72FFFFFF | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/raw

# Brute-force all mode nibbles
for mode in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
    echo ${mode}2FF0000 | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/raw > /dev/null
    read -p "Mode 0x$mode — what do you see? " answer
    echo "0x$mode = $answer"
done
```

Each `raw` write logs to `dmesg`:
```
excalibur-wmi: raw: zone=0x03 data=0x72FFFFFF ret=0
```

---

## Fan Monitoring

```bash
# Read CPU and GPU fan speeds in RPM
cat /sys/class/hwmon/hwmon*/fan1_input   # CPU
cat /sys/class/hwmon/hwmon*/fan2_input   # GPU

# Live monitoring
watch -n 1 "cat /sys/class/hwmon/hwmon*/fan1_input /sys/class/hwmon/hwmon*/fan2_input"

# Using lm-sensors (install with: sudo apt install lm-sensors)
sensors
```

Fan speed cannot be set directly. The firmware ties fan curves to the active power plan.

---

## Power Plans

```bash
# Read current plan
cat /sys/class/hwmon/hwmon*/pwm1

# Set plan
echo 1 | sudo tee /sys/class/hwmon/hwmon*/pwm1   # High Power
echo 2 | sudo tee /sys/class/hwmon/hwmon*/pwm1   # Gaming
echo 3 | sudo tee /sys/class/hwmon/hwmon*/pwm1   # Text Mode
echo 4 | sudo tee /sys/class/hwmon/hwmon*/pwm1   # Low Power
```

---

## Profile Scripts

Save these as executable scripts and call them as needed.

**Gaming** — High Power, red static:
```bash
#!/bin/bash
echo 1 | tee /sys/class/hwmon/hwmon*/pwm1
for zone in left middle right corners; do
    echo FF0000 | tee /sys/class/leds/excalibur::kbd_backlight-$zone/color
    echo static | tee /sys/class/leds/excalibur::kbd_backlight-$zone/mode
done
echo 2 | tee /sys/class/leds/excalibur::kbd_backlight-left/brightness
```

**Battery** — Low Power, dim blue fade:
```bash
#!/bin/bash
echo 4 | tee /sys/class/hwmon/hwmon*/pwm1
for zone in left middle right corners; do
    echo 0000FF | tee /sys/class/leds/excalibur::kbd_backlight-$zone/color
    echo fade   | tee /sys/class/leds/excalibur::kbd_backlight-$zone/mode
done
echo 1 | tee /sys/class/leds/excalibur::kbd_backlight-left/brightness
```

**Rainbow** — Gaming, full rainbow:
```bash
#!/bin/bash
echo 2 | tee /sys/class/hwmon/hwmon*/pwm1
for zone in left middle right corners; do
    echo rainbow | tee /sys/class/leds/excalibur::kbd_backlight-$zone/mode
done
echo 2 | tee /sys/class/leds/excalibur::kbd_backlight-left/brightness
```

**RGB Split** — three-zone red/green/blue:
```bash
#!/bin/bash
echo FF0000 | tee /sys/class/leds/excalibur::kbd_backlight-left/color
echo 00FF00 | tee /sys/class/leds/excalibur::kbd_backlight-middle/color
echo 0000FF | tee /sys/class/leds/excalibur::kbd_backlight-right/color
for zone in left middle right; do
    echo static | tee /sys/class/leds/excalibur::kbd_backlight-$zone/mode
done
echo 2 | tee /sys/class/leds/excalibur::kbd_backlight-left/brightness
```

**Off** — LEDs off, Low Power:
```bash
#!/bin/bash
echo 4 | tee /sys/class/hwmon/hwmon*/pwm1
for zone in left middle right corners; do
    echo off | tee /sys/class/leds/excalibur::kbd_backlight-$zone/mode
done
```

---

## Persistent Boot Configuration

Create a profile script and a systemd oneshot service to apply it at boot.

```bash
sudo nano /usr/local/bin/excalibur-profile.sh
# paste profile content, save

sudo chmod +x /usr/local/bin/excalibur-profile.sh

sudo tee /etc/systemd/system/excalibur.service > /dev/null <<EOF
[Unit]
Description=Excalibur laptop profile
After=multi-user.target

[Service]
Type=oneshot
ExecStart=/usr/local/bin/excalibur-profile.sh

[Install]
WantedBy=multi-user.target
EOF

sudo systemctl enable --now excalibur.service
```

---

## Control Panel

A Textual-based TUI control center is provided in `control-panel.py` for interactive management of lighting, power plans, and fan monitoring.

**Requirements:**

```bash
pip install textual
```

**Running the control panel:**

```bash
sudo python3 control-panel.py
```

(Sudo is required for LED and power plan changes; fan reading is read-only and works without elevation.)

**Features:**

### Dashboard Tab

- **Fan Speed Display:** Real-time monitoring of CPU and GPU fan speeds with color-coded RPM indicators:
  - **Green** (< 2000 RPM): Normal operation
  - **Yellow** (2000–3999 RPM): Elevated fan activity
  - **Red** (≥ 4000 RPM): High cooling demand
  - **Stopped** (0 RPM): Fan not spinning

- **Power Plan Quick Select:** One-click access to all four power plans (High Power, Gaming, Text Mode, Low Power) with visual feedback of the active plan.

### Lighting Tab

- **Zone Selection:** Target specific keyboard regions:
  - `left`, `middle`, `right` — individual keyboard zones
  - `corners` — corner RGB LEDs (fully independent)
  - `all` — all zones at once

- **Animation Modes:** Choose from `off`, `static`, `blink`, `fade`, `heartbeat`, `wave`, `random`, `rainbow`.

- **Color Presets:** 11 preconfigured colors (White, Red, Orange, Yellow, Green, Cyan, Blue, Magenta, Purple, Pink, Off) with real-time color preview widget showing selected hex value.

- **Brightness Levels:** Three-level brightness control (Off, Medium, Full).

- **Hardware Constraint Handling:** The control panel automatically handles a firmware limitation where writing brightness to a single keyboard zone (left/middle/right) would overwrite adjacent keyboard zones' colors. Brightness writes are skipped for single-zone operations and are only applied when using the "all" zones target. For per-zone brightness control, users should always apply lighting to all keyboard zones together.

### Power Tab

- **Power Plan Management:** Detailed descriptions of each power plan:
  - `High Power` — Maximum performance, maximum fan speed
  - `Gaming` — Balanced performance and thermals
  - `Text Mode` — Reduced performance, quiet operation
  - `Low Power` — Maximum battery life, minimal fan activity

### About Tab

- **System Information:** Displays driver load status, hwmon device path, and LED base directory for diagnostic purposes.
- **Source and License:** Quick reference to repository and GPL-2.0-or-later license.

**Keyboard Shortcuts:**

| Key | Action |
|---|---|
| `1` | Switch to Dashboard tab |
| `2` | Switch to Lighting tab |
| `3` | Switch to Power tab |
| `r` | Refresh fan readings and power plan status |
| `q` | Quit the application |

**Sysfs Integration:**

The control panel interfaces with the driver via sysfs:

- **LED Control:** Reads/writes `/sys/class/leds/excalibur::kbd_backlight-{zone}/{color,mode,brightness}`
- **Fan Monitoring:** Reads `/sys/class/hwmon/hwmon*/fan{1,2}_input` for CPU/GPU RPM
- **Power Plans:** Reads/writes `/sys/class/hwmon/hwmon*/pwm1` (1 = High Power, 2 = Gaming, 3 = Text Mode, 4 = Low Power)

**Permission Handling:**

If the control panel is run without sudo, a warning bar appears at the top indicating which operations require elevation. Read-only operations (fan speed display) function normally; writes to LED and power plan attributes fail gracefully with permission error messages.

**Building from Source:**

The control panel is a standalone Python script with no compilation step. Simply ensure Textual is installed and run directly.

---

## Debugging

```bash
# Check module is loaded
lsmod | grep excalibur

# Check kernel messages from driver
sudo dmesg | grep excalibur

# Verify WMI GUID is present
ls /sys/bus/wmi/devices/ | grep 644C5791

# List all sysfs nodes the driver created
find /sys/class/leds/excalibur* -maxdepth 1 2>/dev/null
find /sys/class/hwmon/hwmon* -maxdepth 1 -name "fan*" -o -name "pwm*" 2>/dev/null

# Watch kernel messages live while testing
sudo dmesg -w | grep excalibur

# Live status dashboard
watch -n 1 '
echo "=== Excalibur Status ==="
printf "Power Plan : "; cat /sys/class/hwmon/hwmon*/pwm1 \
    | sed "s/1/High Power/;s/2/Gaming/;s/3/Text Mode/;s/4/Low Power/"
printf "CPU Fan    : "; cat /sys/class/hwmon/hwmon*/fan1_input; echo " RPM"
printf "GPU Fan    : "; cat /sys/class/hwmon/hwmon*/fan2_input; echo " RPM"
echo ""
echo "=== Keyboard Zones ==="
for zone in left middle right corners; do
    printf "%s: mode="; cat /sys/class/leds/excalibur::kbd_backlight-$zone/mode | tr -d "\n"
    printf " brightness="; cat /sys/class/leds/excalibur::kbd_backlight-$zone/brightness
done
'
```

**Module loads but no LED sysfs nodes appear:**
The WMI GUID was found but `probe` returned an error. Check `dmesg` for the specific error code.

**"Unrecognised model" warning in dmesg:**
Your model is not in the DMI table. The driver functions normally but `has_raw_fanspeed` defaults to `true`. If your fan speeds look wrong, see [Adding New Models](#adding-new-models).

**Build fails on `devm_mutex_init`:**
Your kernel is older than 6.4. Replace with `mutex_init(&drv->lock)` and add a `remove` callback that calls `mutex_destroy(&drv->lock)`.

**Build fails on `wmidev_block_set`:**
Your kernel uses the older `wmi_set_block` name. Replace `wmidev_block_set(drv->wdev, 0, &input)` with `wmi_set_block(EXCALIBUR_WMI_GUID, 0, &input)`.

---

## Adding New Models

1. Get your DMI information:
```bash
sudo dmidecode -s system-product-name
sudo dmidecode -s system-version
sudo dmidecode -s bios-version
```

2. Add an entry to `excalibur_dmi_list[]` in `excalibur.c`:
```c
{
    .callback    = dmi_matched,
    .ident       = "EXCALIBUR GXXX",
    .matches     = {
        DMI_MATCH(DMI_SYS_VENDOR,   "EXCALIBUR BILGISAYAR SISTEMLERI"),
        DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR GXXX"),
        DMI_MATCH(DMI_BIOS_VERSION, "XXXXX"),   /* omit if not needed */
    },
    .driver_data = (void *)true,   /* true = raw fanspeed, false = byte-swap */
},
```

Set `driver_data` to `(void *)false` if your model has an Intel 10th gen CPU or older (needs byte-swap). Set to `(void *)true` for 11th gen and newer.

3. If your model's mode nibble values differ from the table above, use the `raw` attribute to brute-force them:
```bash
for mode in 0 1 2 3 4 5 6 7 8 9 a b c d e f; do
    echo ${mode}2FF0000 | sudo tee /sys/class/leds/excalibur::kbd_backlight-left/raw > /dev/null
    read -p "Mode 0x$mode — what do you see? " answer
    echo "0x$mode = $answer"
done
```

4. Rebuild, test, and open a pull request with your findings.

---

## Known Limitations

- **No LED state read-back from firmware.** The firmware does not support reading current LED state. If lighting is changed via a hardware hotkey without the driver knowing, the driver's cache becomes stale. The correct state will be restored the next time any sysfs attribute is written.
- **No per-key RGB.** The hardware exposes three keyboard zones and one corner zone. Per-key control is not supported by the WMI interface.
- **No direct fan speed control.** Fan curves are fully managed by the firmware and tied to the active power plan. Only power plan selection is exposed.
- **No brightness read-back.** Like LED state, brightness cannot be read from firmware. The driver returns cached values.
- **Mode nibble values may vary by model.** The values in this driver were confirmed on the G870. Earlier models may use different nibble mappings. Use the `raw` attribute to verify on untested hardware.

---