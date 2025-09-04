# Excalibur - Casper Gaming Laptop Linux Driver
Excalibur is a technical Linux kernel module and TUI application providing advanced hardware control for Casper Excalibur laptops. It enables deep system integration, bridging ACPI/WMI firmware features to Linux.

---

## Key Features

- **RGB Keyboard:**  
  8 lighting modes, 9-zone control, custom colors, and preset effects.

- **Hardware Monitoring:**  
  Real-time fan speeds, temperature tracking, and sensor framework integration.

- **Power Profile Management:**  
  Seamless with `power-profiles-daemon`, switch between balanced, performance, and power-saver.

- **Gaming-First Design:**  
  Minimal overhead kernel module, direct WMI, system integration for LED control.

---

## Quick Start

### Prerequisites
Install required packages for your distro:
```bash
# Ubuntu/Debian
sudo apt update && sudo apt install build-essential gcc linux-headers-$(uname -r) zstd python3

# Fedora/RHEL
sudo dnf install kernel-devel gcc make zstd python3

# Arch Linux
sudo pacman -S linux-headers gcc make zstd python3
```

### Installation
```bash
git clone https://github.com/sarikayra/excalibur.git
cd excalibur
sudo ./install.sh
sudo reboot
```

### Launch Control Panel
```bash
excalibur
```

---

## Technical Architecture

- **Kernel Module:**  
  WMI interface, LED device, hwmon integration, ACPI power profiles.
- **TUI Application:**  
  Curses interface, real-time system feedback, cross-distro compatibility.
- **WMI Communication:**  
  GUID: `644C5791-B7B0-4123-A90B-E93876E0DAAD`, LED control, hardware info, power plans.

---

## Development

```bash
make clean && make           # Build kernel module
sudo insmod casper-wmi.ko    # Test module
sudo rmmod casper-wmi        # Remove module
```

---

## License

MIT License. See [LICENSE](LICENSE).
