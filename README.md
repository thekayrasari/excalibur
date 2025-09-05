# Excalibur WMI Kernel Module

This is a Linux kernel module for controlling hardware features on Excalibur laptops (e.g., keyboard backlight, fan speeds, power plans) via WMI.

## Features
- Keyboard backlight control
- Fan speed monitoring
- Power plan adjustment
- Hardware info querying

## Prerequisites
- Linux kernel with ACPI/WMI support (most modern kernels)
- Build tools: `make`, `gcc`
- Kernel headers for your running kernel (`uname -r`)
- (Optional) DKMS for auto-rebuild on kernel updates

Install prerequisites:
- **Ubuntu/Debian**: `sudo apt install build-essential linux-headers-$(uname -r) dkms`
- **Arch Linux**: `sudo pacman -S base-devel linux-headers` (DKMS via AUR: `yay -S dkms`)
- **Fedora**: `sudo dnf install make gcc kernel-devel dkms`

## Installation
1. Extract the archive:
   ```bash
   tar -xzf excalibur-wmi.tar.gz
   cd excalibur-wmi
   ```

2. Install the module:
   ```bash
   sudo ./install.sh install
   ```
   This will:
   - Build the module
   - Install to `/lib/modules/$(uname -r)/extra/`
   - Configure auto-loading on boot
   - Update initramfs (if applicable)

3. (Optional) Install with DKMS for auto-rebuild on kernel updates:
   ```bash
   sudo ./install.sh dkms
   ```

4. Verify installation:
   ```bash
   lsmod | grep excalibur
   dmesg | grep excalibur
   ```

## Uninstallation
```bash
sudo ./install.sh uninstall
```
For DKMS:
```bash
sudo dkms remove -m excalibur -v 1.0
sudo rm -rf /usr/src/excalibur-1.0
```

## Usage
- **Keyboard Backlight**:
  ```bash
  echo 1 | sudo tee /sys/class/leds/excalibur::kbd_backlight/brightness  # Level 1
  echo 2 | sudo tee /sys/class/leds/excalibur::kbd_backlight/brightness  # Level 2
  ```

- **LED Control** (advanced):
  ```bash
  echo "301000000" | sudo tee /sys/class/leds/excalibur::kbd_backlight/led_control  # Zone 3
  ```

- **Fan Speeds**:
  ```bash
  cat /sys/class/hwmon/hwmon*/fan1_input  # CPU fan
  cat /sys/class/hwmon/hwmon*/fan2_input  # GPU fan
  ```

- **Power Plan**:
  ```bash
  cat /sys/class/hwmon/hwmon*/pwm1  # Read current plan
  echo 2 | sudo tee /sys/class/hwmon/hwmon*/pwm1  # Set to Gaming mode
  ```

## Debugging
- Enable debug mode:
  ```bash
  sudo modprobe excalibur debug=1
  ```
- Check logs:
  ```bash
  dmesg | grep excalibur
  ```

## Redistribution
1. Package the module:
   ```bash
   tar -czf excalibur-wmi.tar.gz excalibur.c Makefile install.sh README.md
   ```

2. Share the tarball. Users can extract and run `sudo ./install.sh [install|dkms]`.

## Supported Models
- Excalibur G650, G750, G670, G900 (BIOS CP131)
- Add new models to `excalibur.c` (DMI table) and recompile.

## Troubleshooting
- **Module not loading**: Check `dmesg | grep excalibur` for errors (e.g., missing WMI GUID).
- **Backlight not working**: Verify hardware compatibility with `sudo cat /sys/bus/wmi/devices/644C5791-B7B0-4123-A90B-E93876E0DAAD/guid`.
- **Syntax errors in script**: Ensure Unix line endings (`dos2unix install.sh` or edit in `vim`/`nano`).

## Maintainer
Kayra Sari <sarikayra@proton.me>
