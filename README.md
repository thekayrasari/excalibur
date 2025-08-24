# 🗡️ Casper Excalibur WMI Driver

**🎮 Complete WMI driver and control panel for Casper Excalibur gaming laptops**

*Control RGB keyboard backlight, monitor system temperatures, and manage power profiles*

## ✨ Features

- **🌈 RGB Keyboard Backlight Control** - Full control over 8 different lighting modes with custom colors
- **📊 Hardware Monitoring** - Real-time CPU and GPU fan speed monitoring  
- **⚡ Power Profile Management** - Integration with power-profiles-daemon
- **🌍 Multi-Region Support** - Control up to 9 keyboard lighting regions
- **🎨 Preset Effects** - Pre-configured lighting effects for gaming and productivity
- **🔧 System Integration** - Desktop entry and passwordless LED control
- **🚀 Performance Optimized** - Lightweight kernel module with minimal overhead
- **📱 TUI Control Panel** - Beautiful terminal-based interface

## 🚀 Quick Installation

### Prerequisites
Make sure you have the required dependencies installed:

```bash
# 🐧 Ubuntu/Debian
sudo apt update && sudo apt install build-essential gcc linux-headers-$(uname -r) zstd python3

# 🎩 Fedora/RHEL  
sudo dnf install kernel-devel gcc make zstd python3

# 🏹 Arch Linux
sudo pacman -S linux-headers gcc make zstd python3
```

### ⚡ One-Click Setup

1. **Clone the repository**
   ```bash
   git clone https://github.com/yourusername/casper-excalibur-wmi
   cd casper-excalibur-wmi
   ```

2. **Run the universal installer**
   ```bash
   sudo ./install.sh
   ```

3. **Reboot and enjoy!**
   ```bash
   sudo reboot
   ```

### 🎛️ Installation Options

```bash
sudo ./install.sh                # 📦 Install both driver and control panel
sudo ./install.sh --driver-only  # 🔧 Install only the WMI driver
sudo ./install.sh --panel-only   # 🖥️  Install only the control panel
sudo ./install.sh --uninstall    # 🗑️  Remove all components
```

## 🎯 Usage

### 🖥️ Control Panel

Launch the beautiful TUI control panel:
```bash
excalibur
```

The control panel provides access to:

- **⚡ Power Profile**: Switch between power-saver, balanced, and performance modes
- **💡 Keyboard Backlight**: Basic brightness and mode control
- **🎨 RGB Color Control**: Choose from preset colors or custom RGB values
- **✨ Preset Effects**: Apply pre-configured lighting effects
- **📊 System Info**: View hardware information and driver status

### 🎨 Direct LED Control

For power users, control the keyboard backlight directly:

```bash
# Control string format: [regions][mode][brightness][color]
# Example: 3 regions, static mode, max brightness, white color
echo "312ffffff" | sudo tee /sys/class/leds/casper::kbd_backlight/led_control
```

#### 🔧 Control String Reference

| Component | Range | Description |
|-----------|-------|-------------|
| 🌍 Regions | 1-9 | Number of keyboard regions |
| 🎭 Mode | 0-7 | Lighting mode (see modes table) |
| ☀️ Brightness | 0-2 | Brightness level |
| 🎨 Color | 000000-ffffff | RGB color in hex |

#### ✨ Available Lighting Modes

| Mode | Name | Description |
|------|------|-------------|
| 0 | 🔴 Off | Keyboard backlight disabled |
| 1 | 🟢 Static | Solid color lighting |
| 2 | 🔵 Blinking | Blinking effect |
| 3 | 🟡 Breathing | Smooth fade in/out |
| 4 | 🟠 Pulsing | Quick pulse effect |
| 5 | 🌈 Rainbow Pulsing | Rainbow color pulsing |
| 6 | 🎪 Rainbow Pulsing Alt | Alternative rainbow pulse |
| 7 | 🌊 Rainbow Wave | Moving rainbow wave |

### 📊 Hardware Monitoring

The driver exposes fan speeds through the standard hwmon interface:

```bash
# View fan speeds
sensors casper_wmi-*

# Example output:
# casper_wmi-wmi-0
# Adapter: WMI adapter
# cpu_fan_speed:   2800 RPM  🌀
# gpu_fan_speed:   2400 RPM  🎮
```

## 🏗️ Architecture

### 🔧 WMI Driver (`casper-wmi.c`)

The kernel module provides:

- **🔌 WMI Interface**: Communicates with laptop ACPI/WMI firmware
- **💡 LED Class Device**: Standard Linux LED interface for keyboard backlight
- **📊 Hardware Monitoring**: hwmon interface for fan speed monitoring
- **⚡ Power Management**: Integration with ACPI power profiles

### 🖥️ Control Panel (`excalibur.py`)

The TUI application offers:

- **🖼️ Curses-based Interface**: Beautiful terminal-based user interface
- **📊 Real-time Monitoring**: Live system information updates
- **⚡ Power Profile Integration**: Works with power-profiles-daemon
- **✨ Preset Management**: Pre-configured lighting effects
- **🎨 Color Picker**: Interactive RGB color selection

### 📦 Universal Installer (`install.sh`)

The installer script handles:

- **✅ Dependency Checking**: Validates required packages
- **🔨 Module Compilation**: Builds and installs kernel module
- **🔧 System Integration**: Creates desktop entries and sudoers rules
- **🚀 Auto-loading**: Configures module to load at boot

## 🔬 Technical Details

### 🔌 WMI Communication

The driver uses ACPI WMI GUID `644C5791-B7B0-4123-A90B-E93876E0DAAD` to communicate with the laptop firmware. Key operations include:

- `CASPER_SET_LED (0x0100)`: 💡 Control keyboard lighting
- `CASPER_GET_HARDWAREINFO (0x0200)`: 📊 Read hardware sensors
- `CASPER_POWERPLAN (0x0300)`: ⚡ Manage power profiles

### 💡 LED Control Protocol

The LED control uses a 9-character hexadecimal string:
- 1 digit: 🌍 Number of regions (1-9)
- 1 digit: 🎭 Mode (0-7)  
- 1 digit: ☀️ Brightness (0-2)
- 6 digits: 🎨 RGB color (RRGGBB)

### 🌀 Fan Speed Handling

Different laptop models require different fan speed calculation methods:
- **Newer models**: Raw RPM values
- **Older models**: Byte-swapped values requiring correction

## 🛠️ Development

### 🏗️ Building from Source

```bash
# Build kernel module
make clean
make

# Test without installing
sudo insmod casper-wmi.ko

# Remove module
sudo rmmod casper-wmi
```

### 🐛 Debugging

Enable debug output:
```bash
# Load module with debug info
sudo modprobe casper-wmi
dmesg | grep casper-wmi
```

Check LED interface:
```bash
# Verify LED control file exists
ls -la /sys/class/leds/casper::kbd_backlight/
```

## 🆘 Troubleshooting

### 🚨 Common Issues

**❌ Module fails to load:**
```bash
# Check if WMI GUID is available
ls /sys/bus/wmi/devices/ | grep 644C5791-B7B0-4123-A90B-E93876E0DAAD

# Check kernel logs
dmesg | grep -i casper
```

**💡 LED control not working:**
```bash
# Verify driver is loaded
lsmod | grep casper_wmi

# Check permissions
ls -la /sys/class/leds/casper::kbd_backlight/led_control
```

**💥 Control panel crashes:**
```bash
# Run with error output
python3 excalibur.py

# Check dependencies
powerprofilesctl --help
```

### 🆘 Getting Help

If you encounter issues:

1. ✅ Check the troubleshooting section above
2. 🔍 Verify your laptop model is supported
3. 📋 Review kernel logs with `dmesg`
4. 🐛 Open an issue with system information:
   - 💻 Laptop model (`sudo dmidecode -s system-product-name`)
   - 🐧 Kernel version (`uname -r`)
   - 📦 Distribution (`lsb_release -a`)

## 🤝 Contributing

Contributions are welcome! Areas where help is needed:

- **🎮 Device Support**: Adding support for new Casper Excalibur models
- **✨ Feature Enhancement**: Additional lighting effects and controls
- **🧪 Testing**: Validation on different hardware configurations
- **📚 Documentation**: Improving guides and troubleshooting

### 🛠️ Development Setup

```bash
git clone https://github.com/yourusername/casper-excalibur-wmi
cd casper-excalibur-wmi

# Create development branch
git checkout -b feature/your-feature

# Test your changes
sudo ./install.sh --uninstall  # Remove existing installation
sudo ./install.sh              # Install your changes
```

## 📄 License

This project is open source and available under the [MIT License](LICENSE).

## 🙏 Acknowledgments

Thanks to the amazing open-source community and the developers of:

- 🐧 Linux kernel WMI subsystem developers
- 🎮 Casper community for hardware information and testing
- 👥 Contributors who helped identify supported models
- 🌟 Everyone who made this project possible

---

<div align="center">

**🎮 Made with ❤️ for the Linux gaming community**

⭐ If you found this useful, consider giving it a star!

[🐛 Report Bug](https://github.com/yourusername/casper-excalibur-wmi/issues) • [✨ Request Feature](https://github.com/yourusername/casper-excalibur-wmi/issues) • [🤝 Contribute](https://github.com/yourusername/casper-excalibur-wmi/pulls)

</div>
