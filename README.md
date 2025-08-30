# ⚔️ Excalibur - Casper Gaming Laptop Linux Driver

**🌈 Complete Linux WMI driver and control panel for Casper Excalibur gaming laptops**

*Transform your Casper gaming experience on Linux with full RGB control, hardware monitoring, and power management*

</div>

---

## ✨ What is Excalibur?

Excalibur is a **comprehensive Linux driver solution** for Casper Excalibur gaming laptops that unlocks the full potential of your hardware on Linux systems. This project bridges the gap between Casper's Windows-focused features and the Linux gaming community, providing native kernel-level support for advanced laptop controls.

## 🎯 Key Features

### 🌈 **RGB Keyboard Mastery**
- **8 Dynamic Lighting Modes**: From static colors to mesmerizing rainbow waves
- **9-Region Control**: Fine-grained control over different keyboard zones
- **Custom Color Support**: Full RGB spectrum with hex color codes
- **Preset Effects**: Gaming and productivity lighting profiles

### 📊 **Hardware Monitoring**
- **Real-time Fan Speeds**: Monitor CPU and GPU fan RPM through standard `hwmon` interface
- **Temperature Awareness**: Keep track of system thermal performance
- **Hardware Sensor Integration**: Native Linux sensor framework support

### ⚡ **Power Profile Management**
- **Seamless Integration**: Works with `power-profiles-daemon`
- **Performance Modes**: Power-saver, balanced, and performance profiles
- **Battery Optimization**: Intelligent power management for mobile gaming

### 🎮 **Gaming-First Design**
- **Minimal Overhead**: Lightweight kernel module optimized for gaming performance
- **Zero Latency**: Direct WMI communication with laptop firmware
- **System Integration**: Desktop entries and passwordless LED control

---

## 🚀 Quick Start

### 📋 Prerequisites

Ensure you have the required dependencies for your distribution:

```bash
# 🐧 Ubuntu/Debian
sudo apt update && sudo apt install build-essential gcc linux-headers-$(uname -r) zstd python3

# 🎩 Fedora/RHEL
sudo dnf install kernel-devel gcc make zstd python3

# 🏹 Arch Linux
sudo pacman -S linux-headers gcc make zstd python3
```

### ⚡ Installation

```bash
# Clone the repository
git clone https://github.com/sarikayra/excalibur.git
cd excalibur

# Run the universal installer
sudo ./install.sh

# Reboot and enjoy!
sudo reboot
```

### 🎛️ Launch Control Panel

```bash
excalibur
```

---

## 🎨 RGB Control Guide

### 🖥️ **TUI Control Panel**
The beautiful terminal-based interface provides:
- ⚡ **Power Profile Switching**: Performance modes at your fingertips
- 💡 **Backlight Control**: Brightness and mode adjustments
- 🎨 **RGB Color Picker**: Interactive color selection
- ✨ **Preset Effects**: One-click lighting configurations
- 📊 **System Information**: Real-time hardware status

### 🔧 **Manual Control**

For power users who want direct control:

```bash
# Control string format: [regions][mode][brightness][color]
# Example: 3 regions, rainbow wave, max brightness, purple theme
echo "372ff00ff" | sudo tee /sys/class/leds/casper::kbd_backlight/led_control
```

### 🎭 **Lighting Modes**

| Mode | Effect | Perfect For |
|------|--------|-------------|
| 🔴 **0** - Off | No lighting | Battery saving |
| 🟢 **1** - Static | Solid color | Professional work |
| 🔵 **2** - Blinking | Attention-grabbing blink | Notifications |
| 🟡 **3** - Breathing | Smooth fade in/out | Relaxed gaming |
| 🟠 **4** - Pulsing | Quick pulse effect | High-intensity gaming |
| 🌈 **5** - Rainbow Pulse | Dynamic rainbow | Streaming/content creation |
| 🎪 **6** - Rainbow Alt | Alternative rainbow | Customization variety |
| 🌊 **7** - Rainbow Wave | Moving wave effect | Immersive gaming |

---

## 📊 Hardware Monitoring

View your laptop's thermal performance with standard Linux tools:

```bash
# Check fan speeds
sensors casper_wmi-*

# Example output:
# casper_wmi-wmi-0
# Adapter: WMI adapter
# cpu_fan_speed: 2800 RPM 🌀
# gpu_fan_speed: 2400 RPM 🎮
```

---

## 🏗️ Technical Architecture

### 🔌 **Kernel Module Components**
- **WMI Interface**: Direct communication with laptop ACPI/WMI firmware
- **LED Class Device**: Standard Linux LED interface integration
- **Hardware Monitor**: Native `hwmon` interface for thermal data
- **Power Management**: ACPI power profile integration

### 🖼️ **TUI Application Features**
- **Curses-based Interface**: Beautiful, responsive terminal UI
- **Real-time Updates**: Live system monitoring and feedback
- **Cross-platform Compatibility**: Works across all major Linux distributions

### 📡 **WMI Communication**
- **GUID**: `644C5791-B7B0-4123-A90B-E93876E0DAAD`
- **LED Control**: `CASPER_SET_LED (0x0100)`
- **Hardware Info**: `CASPER_GET_HARDWAREINFO (0x0200)`
- **Power Plans**: `CASPER_POWERPLAN (0x0300)`

---

## 🛠️ Installation Options

The flexible installer supports various installation scenarios:

```bash
sudo ./install.sh                 # 📦 Complete installation
sudo ./install.sh --driver-only   # 🔧 Kernel module only
sudo ./install.sh --panel-only    # 🖥️ Control panel only
sudo ./install.sh --uninstall     # 🗑️ Clean removal
```

---

## 🧪 Development & Testing

### 🔨 **Building from Source**

```bash
# Build kernel module
make clean && make

# Test without installing
sudo insmod casper-wmi.ko

# Remove test module
sudo rmmod casper-wmi
```

### 🐛 **Debug Mode**

```bash
# Enable debug output
sudo modprobe casper-wmi
dmesg | grep casper-wmi

# Check LED interface
ls -la /sys/class/leds/casper::kbd_backlight/
```

---

## 🆘 Troubleshooting

### ❌ **Module Won't Load**
```bash
# Check WMI GUID availability
ls /sys/bus/wmi/devices/ | grep 644C5791-B7B0-4123-A90B-E93876E0DAAD

# Review kernel logs
dmesg | grep -i casper
```

### 💡 **LED Control Issues**
```bash
# Verify driver status
lsmod | grep casper_wmi

# Check file permissions
ls -la /sys/class/leds/casper::kbd_backlight/led_control
```

### 💥 **Control Panel Crashes**
```bash
# Run with error output
python3 excalibur.py

# Verify power profiles daemon
powerprofilesctl --help
```

---

## 🤝 Contributing

We welcome contributions from the gaming and Linux communities! Here's how you can help:

### 🎯 **Priority Areas**
- **🎮 Device Support**: Adding support for new Casper Excalibur models
- **✨ Feature Enhancement**: Additional lighting effects and advanced controls
- **🧪 Hardware Testing**: Validation on different configurations
- **📚 Documentation**: Improving guides and user experience
- **🌍 Localization**: Multi-language support for global users

### 🔄 **Development Workflow**

```bash
git clone https://github.com/sarikayra/excalibur.git
cd excalibur

# Create feature branch
git checkout -b feature/amazing-new-feature

# Test your changes
sudo ./install.sh --uninstall  # Clean slate
sudo ./install.sh              # Install your changes

# Submit pull request
```

---

## 📜 License & Attribution

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details.

### 🙏 **Acknowledgments**
- 🐧 **Linux Kernel WMI Subsystem** developers for the foundation
- 🎮 **Casper Community** for hardware information and extensive testing
- 👥 **Open Source Contributors** who helped identify supported models
- 🌟 **Linux Gaming Community** for inspiration and feedback

---

## 🎮 About Casper Excalibur

Casper Excalibur laptops are high-performance gaming machines popular in Turkey and emerging markets. These laptops feature:
- **RGB Keyboard Backlighting** with multiple zones
- **Advanced Thermal Management** with dual fan systems
- **Gaming-Optimized Hardware** with discrete graphics
- **Power Profile Controls** for performance tuning

---

<div align="center">

**🎮 Made with ❤️ for the Linux Gaming Community**

*Bringing Windows gaming features to Linux, one driver at a time.*

</div>
