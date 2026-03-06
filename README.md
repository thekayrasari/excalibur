# Excalibur WMI Linux Driver

## Overview

This project provides a Linux kernel module for Excalibur laptops, enabling control and monitoring of various hardware features through Windows Management Instrumentation (WMI). It exposes functionalities such as keyboard backlight control, corner LED management, CPU/GPU fan speed monitoring, and power plan selection.

## Features

*   **Keyboard Backlight Control**: Adjust brightness and control different keyboard LED zones.
*   **Corner LED Control**: Manage the behavior of corner LEDs.
*   **Fan Speed Monitoring**: Read CPU and GPU fan speeds via `hwmon` interface.
*   **Power Plan Management**: Select different power plans for optimized performance or power saving through `hwmon`.

## Supported Models

The driver is designed for Excalibur laptops and has been specifically tested and identified for the following models:

*   EXCALIBUR G650
*   EXCALIBUR G750
*   EXCALIBUR G670
*   EXCALIBUR G900 (with BIOS version CP131)

## Prerequisites

To build and install this module, you need the following:

*   Linux kernel headers for your running kernel (`uname -r`)
*   Build tools: `make`, `gcc`

**Installation of Prerequisites (Examples):**

*   **Ubuntu/Debian:**
    ```bash
    sudo apt update
    sudo apt install build-essential linux-headers-$(uname -r)
    ```
*   **Arch Linux:**
    ```bash
    sudo pacman -S base-devel linux-headers
    ```
*   **Fedora:**
    ```bash
    sudo dnf install make gcc kernel-devel
    ```

## Installation

To install the driver, use the provided `install.sh` script. This script will build the module, install it, configure it for auto-loading on boot, and update your initramfs.

```bash
sudo ./install.sh install
```

## Uninstallation

To uninstall the driver, use the `install.sh` script with the `uninstall` argument. This will unload the module, remove its files, and revert auto-loading configurations.

```bash
sudo ./install.sh uninstall
```

## Usage

After successful installation, the driver will expose various controls and sensors through the Linux `sysfs` interface. 

### LED Control

Keyboard and corner LEDs can be controlled via `sysfs` entries, typically found under `/sys/class/leds/`.

*   **Keyboard Backlight**: The keyboard backlight is registered as a standard LED class device, usually named `excalibur::kbd_backlight`. You can control its brightness:
    ```bash
    echo 1 > /sys/class/leds/excalibur::kbd_backlight/brightness
    # Brightness values typically range from 0 (off) to 2 (max)
    ```
*   **Advanced LED Control (Zones, Colors):** For more advanced control over keyboard LED zones and specific colors, a `led_control` attribute is provided. This attribute expects a 64-bit hexadecimal value where the higher 32 bits represent the LED zone and the lower 32 bits represent the LED data (color/mode). 
    *   **LED Zones:**
        *   `0x03`: Keyboard LED Zone 1
        *   `0x04`: Keyboard LED Zone 2
        *   `0x05`: Keyboard LED Zone 3
        *   `0x06`: All Keyboard LEDs
        *   `0x07`: Corner LEDs
    
    *   **Example: Set all keyboard LEDs to a specific color (e.g., bright red):**
        ```bash
        echo "00000006FF000000" | sudo tee /sys/devices/platform/excalibur-wmi/led_control
        # Format: [ZONE_ID][COLOR_DATA]
        # ZONE_ID: 00000006 for all keyboard LEDs
        # COLOR_DATA: FF000000 for bright red (ARGB format, A=alpha/brightness)
        ```
        *Note: The exact interpretation of `COLOR_DATA` (e.g., ARGB, RGB, specific modes) might require further experimentation or documentation from Excalibur.* 

### Fan Speed Monitoring

CPU and GPU fan speeds are exposed via the `hwmon` interface, typically found under `/sys/class/hwmon/hwmonX/` (where `X` is a number).

*   **Identify your hwmon device:**
    ```bash
    ls /sys/class/hwmon/hwmon*/name
    # Look for a file containing "excalibur_wmi"
    ```
*   Once identified (e.g., `hwmon0`):
    ```bash
    cat /sys/class/hwmon/hwmon0/fan1_input  # CPU Fan Speed (RPM)
    cat /sys/class/hwmon/hwmon0/fan1_label # "cpu_fan_speed"
    cat /sys/class/hwmon/hwmon0/fan2_input  # GPU Fan Speed (RPM)
    cat /sys/class/hwmon/hwmon0/fan2_label # "gpu_fan_speed"
    ```

### Power Plan Control

Power plans can be set via the `hwmon` interface, typically found under `/sys/class/hwmon/hwmonX/`.

*   The power plan control is exposed as `pwm1`.
    ```bash
    # Read current power plan
    cat /sys/class/hwmon/hwmon0/pwm1
    
    # Set power plan
    # Values:
    # 1: HIGH_POWER
    # 2: GAMING
    # 3: TEXT_MODE
    # 4: LOW_POWER
    echo 2 | sudo tee /sys/class/hwmon/hwmon0/pwm1 # Set to GAMING power plan
    ```

## Building Manually

If you prefer to build the module manually without using `install.sh`:

```bash
make
```
This will produce `excalibur.ko` in the current directory.

## License

This project is licensed under the MIT License - see the `LICENSE` file for details.

Copyright (c) 2025 Kayra Sarı
