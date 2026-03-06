# Excalibur WMI Kernel Module

Linux driver for Excalibur gaming laptops providing hardware control through the WMI interface.

---

## Overview

The Excalibur WMI Kernel Module enables direct hardware control on Excalibur gaming laptops from Linux, including RGB keyboard lighting, fan speed monitoring, and power profile management.

---

## Features

- **RGB Keyboard Control** — Per-zone color customization, 3-level brightness, corner LED support
- **Fan Monitoring** — Real-time CPU and GPU fan speed via hwmon
- **Power Profiles** — High Power, Gaming, Text Mode, and Low Power modes
- **Hardware Info** — BIOS version query, DMI system identification, ACPI/WMI integration

---

## Supported Models

| Model | Status | Notes |
|---|---|---|
| Excalibur G650 | Fully Supported | Tested & Verified |
| Excalibur G750 | Fully Supported | Tested & Verified |
| Excalibur G670 | Fully Supported | Tested & Verified |
| Excalibur G900 | Fully Supported | BIOS CP131 |
| Excalibur G870 | Fully Supported | Tested & Verified |
| Excalibur G770 | Compatible | Requires DMI table entry |

To add an unlisted model, see [Adding New Models](#adding-new-models).

---

## Prerequisites

### Build Tools

**Ubuntu / Debian / Linux Mint**
```bash
sudo apt install build-essential linux-headers-$(uname -r) dkms
```

**Arch Linux / Manjaro**
```bash
sudo pacman -S base-devel linux-headers
```

**Fedora / RHEL / CentOS**
```bash
sudo dnf install make gcc kernel-devel dkms
```

**openSUSE**
```bash
sudo zypper install make gcc kernel-devel dkms
```

### System Requirements

- Linux kernel 5.0+ with ACPI/WMI support
- GCC and Make
- Kernel headers matching the running kernel
- DKMS (optional, for automatic rebuilds on kernel updates)

---

## Installation

### Standard Installation

```bash
git clone https://github.com/thekayrasari/excalibur
cd excalibur
sudo ./install.sh install
```

Verifying:
```bash
lsmod | grep excalibur
dmesg | grep excalibur
```

This builds the module, installs it to `/lib/modules/$(uname -r)/extra/`, configures auto-loading on boot, and updates initramfs.

### DKMS Installation

DKMS automatically rebuilds the module after kernel updates.

```bash
sudo ./install.sh dkms
```

---

## Usage

### RGB Keyboard

**Set zone color:**
```bash
echo "301000000" | sudo tee /sys/class/leds/excalibur::kbd_backlight/led_control  # Zone 3
```

**Set brightness:**
```bash
echo 1 | sudo tee /sys/class/leds/excalibur::kbd_backlight/brightness  # Level 1
echo 2 | sudo tee /sys/class/leds/excalibur::kbd_backlight/brightness  # Level 2
```

### Fan Speed Monitoring

```bash
cat /sys/class/hwmon/hwmon*/fan1_input  # CPU fan
cat /sys/class/hwmon/hwmon*/fan2_input  # GPU fan
```

### Power Profiles

```bash
cat /sys/class/hwmon/hwmon*/pwm1                        # Read current plan
echo 2 | sudo tee /sys/class/hwmon/hwmon*/pwm1          # Set to Gaming mode
```

---

## Debugging

```bash
# Load with debug output
sudo modprobe excalibur debug=1

# View kernel messages
dmesg | grep excalibur

# Check WMI GUID availability
ls -la /sys/bus/wmi/devices/644C5791-B7B0-4123-A90B-E93876E0DAAD/
```

---

## Uninstallation

**Standard:**
```bash
sudo ./install.sh uninstall
```

**DKMS:**
```bash
sudo dkms remove -m excalibur -v 1.0
sudo rm -rf /usr/src/excalibur-1.0
```

---

## Troubleshooting

**Module not loading**

Check kernel messages for errors:
```bash
dmesg | grep excalibur
```
Verify the WMI GUID exists, confirm your kernel version is 5.0+, and ensure kernel headers are installed.

**Backlight not working**

Confirm WMI device is available:
```bash
sudo cat /sys/bus/wmi/devices/644C5791-B7B0-4123-A90B-E93876E0DAAD/guid
```
Verify your model is supported and that WMI is not disabled in BIOS.

**Script syntax errors**

Convert line endings if the script was created on Windows:
```bash
dos2unix install.sh
chmod +x install.sh
```

**Build failures**

Reinstall build dependencies and rebuild:
```bash
# Ubuntu/Debian
sudo apt install build-essential linux-headers-$(uname -r)

make clean && make
```

---

## Adding New Models

1. Edit `excalibur.c` and add an entry to the DMI table:

```c
{
    .callback = dmi_matched,
    .ident = "EXCALIBUR GXXX",
    .matches = {
        DMI_MATCH(DMI_SYS_VENDOR, "EXCALIBUR BILGISAYAR SISTEMLERI"),
        DMI_MATCH(DMI_PRODUCT_NAME, "EXCALIBUR GXXX")
    },
    .driver_data = (void *)false,
},
```

2. Recompile and test.
3. Submit a pull request.

---

## Distribution

```bash
# Create package
tar -czf excalibur-wmi.tar.gz excalibur.c Makefile install.sh README.md LICENSE.txt

# Install from package
tar -xzf excalibur-wmi.tar.gz
cd excalibur-wmi
sudo ./install.sh install
```

---

## License

MIT License — see [LICENSE.txt](LICENSE) for details.

Copyright (c) 2025 Kayra Sarı
