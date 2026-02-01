<div align="center">

# ⚔️ Excalibur WMI Kernel Module

### Linux Driver for Excalibur Gaming Laptops

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE.txt)
[![Platform](https://img.shields.io/badge/Platform-Linux-orange.svg)]()
[![Kernel](https://img.shields.io/badge/Kernel-5.0+-green.svg)]()

*Control your Excalibur laptop's hardware features directly from Linux*

[Features](#-features) • [Installation](#-installation) • [Usage](#-usage) • [Troubleshooting](#-troubleshooting)

---

</div>

## 📋 Overview

The **Excalibur WMI Kernel Module** is a Linux driver that provides comprehensive hardware control for Excalibur gaming laptops through the WMI (Windows Management Instrumentation) interface. Take full control of your laptop's RGB lighting, fan speeds, and power profiles directly from your Linux system.

<br>

## ✨ Features

<table>
<tr>
<td width="50%">

### 💡 **RGB Keyboard Control**
- Multi-zone RGB backlight configuration
- Per-zone color customization
- 3-level brightness control
- Corner LED support

</td>
<td width="50%">

### 🌀 **Fan Management**
- Real-time CPU fan speed monitoring
- Real-time GPU fan speed monitoring
- Hardware sensor integration
- Live system monitoring via hwmon

</td>
</tr>
<tr>
<td width="50%">

### ⚡ **Power Profiles**
- **High Power** - Maximum performance
- **Gaming** - Balanced gaming mode
- **Text Mode** - Quiet productivity
- **Low Power** - Battery saving

</td>
<td width="50%">

### 🔧 **Hardware Info**
- BIOS version query
- Hardware information retrieval
- DMI system identification
- ACPI/WMI integration

</td>
</tr>
</table>

<br>

## 🖥️ Supported Models

| Model | Status | Notes |
|-------|--------|-------|
| 🎮 Excalibur G650 | ✅ Fully Supported | Tested & Verified |
| 🎮 Excalibur G750 | ✅ Fully Supported | Tested & Verified |
| 🎮 Excalibur G670 | ✅ Fully Supported | Tested & Verified |
| 🎮 Excalibur G900 | ✅ Fully Supported | BIOS CP131 |
| 🎮 Excalibur G870 | ⚠️ Compatible | Add to DMI table |
| 🎮 Excalibur G770 | ⚠️ Compatible | Add to DMI table |

> 💡 **Tip:** Don't see your model? You can add it to the DMI table in `excalibur.c` and recompile!

<br>

## 📦 Prerequisites

### Build Requirements

Before installation, ensure you have the necessary build tools and kernel headers:

<details>
<summary><b>🐧 Ubuntu / Debian / Linux Mint</b></summary>

```bash
sudo apt install build-essential linux-headers-$(uname -r) dkms
```
</details>

<details>
<summary><b>🔵 Arch Linux / Manjaro</b></summary>

```bash
sudo pacman -S base-devel linux-headers
# For DKMS (optional)
yay -S dkms
```
</details>

<details>
<summary><b>🎩 Fedora / RHEL / CentOS</b></summary>

```bash
sudo dnf install make gcc kernel-devel dkms
```
</details>

<details>
<summary><b>🦎 openSUSE</b></summary>

```bash
sudo zypper install make gcc kernel-devel dkms
```
</details>

### System Requirements

- ✅ Linux kernel with ACPI/WMI support (5.0+)
- ✅ GCC compiler
- ✅ Make build system
- ✅ Kernel headers matching your running kernel
- ⚙️ DKMS (optional, for automatic rebuilds)

<br>

## 🚀 Installation

### Method 1: Standard Installation

```bash
# 1. Clone the repository
git clone https://github.com/thekayrasari/excalibur
cd excalibur

# 2. Run the installation script
sudo ./install.sh install

# 3. Verify the installation
lsmod | grep excalibur
dmesg | grep excalibur
```

**What this does:**
- 🔨 Builds the kernel module
- 📁 Installs to `/lib/modules/$(uname -r)/extra/`
- 🔄 Configures auto-loading on boot
- 💾 Updates initramfs (distribution-specific)

### Method 2: DKMS Installation (Recommended)

DKMS automatically rebuilds the module when you update your kernel.

```bash
sudo ./install.sh dkms
```

**Benefits:**
- ✅ Automatic rebuilds on kernel updates
- ✅ Simplified version management
- ✅ Better upgrade path

<br>

## 🎮 Usage

### 🌈 RGB Keyboard Control

#### Set Zone Colors

```bash
# Zone 3 - Full RGB control (format: ZZRRGGBB in hex)
echo "301000000" | sudo tee /sys/class/leds/excalibur::kbd_backlight/led_control

# Examples:
echo "30FF0000" | sudo tee /sys/class/leds/excalibur::kbd_backlight/led_control  # Red
echo "300FF000" | sudo tee /sys/class/leds/excalibur::kbd_backlight/led_control  # Green
echo "3000FF00" | sudo tee /sys/class/leds/excalibur::kbd_backlight/led_control  # Blue
```

#### Adjust Brightness

```bash
# Level 0 - Off
echo 0 | sudo tee /sys/class/leds/excalibur::kbd_backlight/brightness

# Level 1 - Low
echo 1 | sudo tee /sys/class/leds/excalibur::kbd_backlight/brightness

# Level 2 - High
echo 2 | sudo tee /sys/class/leds/excalibur::kbd_backlight/brightness
```

### 🌀 Fan Speed Monitoring

```bash
# Check CPU fan speed (RPM)
cat /sys/class/hwmon/hwmon*/fan1_input

# Check GPU fan speed (RPM)
cat /sys/class/hwmon/hwmon*/fan2_input

# Monitor both fans in real-time
watch -n 1 'cat /sys/class/hwmon/hwmon*/fan*_input'
```

### ⚡ Power Plan Management

```bash
# View current power plan
cat /sys/class/hwmon/hwmon*/pwm1

# Set power plans:
echo 1 | sudo tee /sys/class/hwmon/hwmon*/pwm1  # High Power
echo 2 | sudo tee /sys/class/hwmon/hwmon*/pwm1  # Gaming
echo 3 | sudo tee /sys/class/hwmon/hwmon*/pwm1  # Text Mode
echo 4 | sudo tee /sys/class/hwmon/hwmon*/pwm1  # Low Power
```

| Mode | Value | Use Case |
|------|-------|----------|
| ⚡ High Power | 1 | Maximum performance, high power consumption |
| 🎮 Gaming | 2 | Balanced performance and cooling |
| 📝 Text Mode | 3 | Quiet operation, productivity tasks |
| 🔋 Low Power | 4 | Battery saving, minimal fan noise |

<br>

## 🔍 Debugging

### Enable Debug Mode

```bash
# Load module with debug output
sudo modprobe excalibur debug=1

# View debug messages
dmesg | grep excalibur
```

### Common Debug Commands

```bash
# Check if module is loaded
lsmod | grep excalibur

# View detailed kernel messages
dmesg | tail -50

# Check WMI GUID availability
ls -la /sys/bus/wmi/devices/644C5791-B7B0-4123-A90B-E93876E0DAAD/
```

<br>

## 🗑️ Uninstallation

### Standard Uninstall

```bash
sudo ./install.sh uninstall
```

### DKMS Uninstall

```bash
# Remove DKMS module
sudo dkms remove -m excalibur -v 1.0

# Clean up source files
sudo rm -rf /usr/src/excalibur-1.0
```

<br>

## 📦 Distribution

### Create Distribution Package

```bash
tar -czf excalibur-wmi.tar.gz excalibur.c Makefile install.sh README.md LICENSE.txt
```

### Installation from Package

```bash
# Extract the archive
tar -xzf excalibur-wmi.tar.gz
cd excalibur-wmi

# Install
sudo ./install.sh install
```

<br>

## 🛠️ Troubleshooting

### ❌ Module Not Loading

**Problem:** Module fails to load after installation

```bash
# Check kernel messages for errors
dmesg | grep excalibur

# Common issues:
# - Missing WMI GUID
# - Incompatible kernel version
# - Missing dependencies
```

**Solution:**
- Verify WMI GUID exists: `sudo cat /sys/bus/wmi/devices/644C5791-B7B0-4123-A90B-E93876E0DAAD/guid`
- Check kernel version: `uname -r` (requires 5.0+)
- Reinstall kernel headers

### 💡 Backlight Not Working

**Problem:** Keyboard backlight controls have no effect

```bash
# Test WMI device availability
sudo cat /sys/bus/wmi/devices/644C5791-B7B0-4123-A90B-E93876E0DAAD/guid
```

**Solution:**
- Verify your laptop model is supported
- Check if BIOS has WMI disabled
- Try loading with debug mode

### 📝 Script Syntax Errors

**Problem:** Installation script fails with syntax errors

```bash
# Convert line endings to Unix format
dos2unix install.sh

# Or use sed
sed -i 's/\r$//' install.sh

# Make executable
chmod +x install.sh
```

### 🔧 Build Failures

**Problem:** Compilation errors during build

**Common causes:**
- Missing kernel headers
- Incompatible kernel version
- Missing build tools

**Solution:**
```bash
# Reinstall build dependencies
sudo apt install build-essential linux-headers-$(uname -r)  # Ubuntu/Debian
sudo pacman -S base-devel linux-headers                      # Arch
sudo dnf install make gcc kernel-devel                       # Fedora

# Clean and rebuild
make clean
make
```

<br>

## 🤝 Contributing

Contributions are welcome! Here's how you can help:

- 🐛 Report bugs and issues
- ✨ Suggest new features
- 📝 Improve documentation
- 🔧 Submit pull requests
- 🎮 Test on different laptop models

### Adding New Models

To add support for your Excalibur model:

1. Edit `excalibur.c`
2. Add your model to the DMI table:
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
3. Recompile and test
4. Submit a pull request!

<br>

## 📄 License

This project is licensed under the **MIT License** - see the [LICENSE.txt](LICENSE.txt) file for details.

```
Copyright (c) 2025 Kayra Sarı
```

<br>

## 👤 Author

**Kayra Sarı**
- 📧 Email: thekayrasari@gmail.com
- 🐙 GitHub: [@thekayrasari](https://github.com/thekayrasari)

<br>

## 🙏 Acknowledgments

- Linux kernel WMI subsystem developers
- Excalibur laptop hardware documentation
- Open source community contributors

<br>

---

<div align="center">

### ⭐ Star this repository if you found it helpful!

**Made with ❤️ for the Linux community**

[Report Bug](https://github.com/thekayrasari/excalibur/issues) • [Request Feature](https://github.com/thekayrasari/excalibur/issues) • [Documentation](https://github.com/thekayrasari/excalibur/wiki)

</div>
