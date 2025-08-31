#!/bin/bash
#
# Casper Excalibur Universal Installer
# Installs both the WMI kernel module and the control panel
#
# Usage: ./install.sh [options]
# Options:
#   --uninstall    Remove both components
#   --driver-only  Install only the WMI driver
#   --panel-only   Install only the control panel
#   --help         Show this help
#

set -e # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
NC='\033[0m' # No Color

# Configuration
DRIVER_NAME="casper-wmi"
PANEL_NAME="excalibur"
PANEL_SCRIPT="excalibur.py"
KERNEL_VERSION=$(uname -r)
MODULE_DIR="/lib/modules/${KERNEL_VERSION}/kernel/drivers/platform/x86"
MODULES_LOAD_DIR="/etc/modules-load.d"
BIN_DIR="/usr/local/bin"

# Function to print colored output
print_status() {
  echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
  echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
  echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
  echo -e "${RED}[ERROR]${NC} $1"
}

print_header() {
  echo -e "${BOLD}${BLUE}"
  echo "╔══════════════════════════════════════════════════════════════╗"
  echo "║              Casper Excalibur Universal Installer           ║"
  echo "║                 WMI Driver + Control Panel                  ║"
  echo "╚══════════════════════════════════════════════════════════════╝"
  echo -e "${NC}"
}

# Check if running as root
check_root() {
  if [[ $EUID -ne 0 ]]; then
    print_error "This script must be run as root (use sudo)"
    exit 1
  fi
}

# Check dependencies
check_dependencies() {
  print_status "Checking dependencies..."

  local missing_deps=()

  # Check for build tools
  if ! command -v make &>/dev/null; then
    missing_deps+=("build-essential")
  fi

  if ! command -v gcc &>/dev/null; then
    missing_deps+=("gcc")
  fi

  # Check for kernel headers
  if [[ ! -d "/lib/modules/${KERNEL_VERSION}/build" ]]; then
    missing_deps+=("linux-headers-${KERNEL_VERSION}")
  fi

  # Check for zstd
  if ! command -v zstd &>/dev/null; then
    missing_deps+=("zstd")
  fi

  # Check for python3
  if ! command -v python3 &>/dev/null; then
    missing_deps+=("python3")
  fi

  if [[ ${#missing_deps[@]} -gt 0 ]]; then
    print_error "Missing dependencies: ${missing_deps[*]}"
    print_status "Please install them with:"
    echo "  apt update && apt install ${missing_deps[*]}"
    echo "  # or for other distros, use your package manager"
    exit 1
  fi

  print_success "All dependencies satisfied"
}

# Check if files exist
check_files() {
  local missing_files=()

  if [[ "$INSTALL_DRIVER" == "true" ]]; then
    if [[ ! -f "Makefile" ]]; then
      missing_files+=("Makefile (for WMI driver)")
    fi
    if [[ ! -f "${DRIVER_NAME}.c" ]]; then
      missing_files+=("${DRIVER_NAME}.c")
    fi
  fi

  if [[ "$INSTALL_PANEL" == "true" ]]; then
    if [[ ! -f "$PANEL_SCRIPT" ]]; then
      missing_files+=("$PANEL_SCRIPT")
    fi
  fi

  if [[ ${#missing_files[@]} -gt 0 ]]; then
    print_error "Missing files: ${missing_files[*]}"
    print_status "Make sure you're running this from the correct directory"
    exit 1
  fi
}

# Install WMI driver
install_driver() {
  print_status "Installing Casper WMI driver..."

  # Build the module
  print_status "Building kernel module..."
  make clean &>/dev/null || true
  if ! make; then
    print_error "Failed to build kernel module"
    exit 1
  fi
  print_success "Kernel module built successfully"

  # Create module directory if it doesn't exist
  mkdir -p "$MODULE_DIR"

  # Compress and install the module
  print_status "Installing kernel module..."
  if zstd "${DRIVER_NAME}.ko" -o "${MODULE_DIR}/${DRIVER_NAME}.ko.zst" --force; then
    print_success "Kernel module installed to ${MODULE_DIR}/"
  else
    print_error "Failed to install kernel module"
    exit 1
  fi

  # Update module dependencies
  print_status "Updating module dependencies..."
  depmod -a
  print_success "Module dependencies updated"

  # Set up automatic loading
  print_status "Configuring automatic module loading..."
  echo "$DRIVER_NAME" >"${MODULES_LOAD_DIR}/${DRIVER_NAME}.conf"
  print_success "Auto-loading configured"

  # Test loading the module
  print_status "Testing module loading..."
  if modprobe "$DRIVER_NAME"; then
    print_success "Module loaded successfully"

    # Check if the LED interface is available
    if [[ -e "/sys/class/leds/casper::kbd_backlight/led_control" ]]; then
      print_success "LED control interface is available"
    else
      print_warning "LED control interface not found (this might be normal for unsupported models)"
    fi
  else
    print_error "Failed to load module"
    print_warning "The module was installed but couldn't be loaded. This might be normal if your laptop model isn't supported."
  fi
}

# Install control panel
install_panel() {
  print_status "Installing Casper Control Panel..."

  # Make executable
  chmod +x "$PANEL_SCRIPT"

  # Copy to bin directory
  if cp "$PANEL_SCRIPT" "${BIN_DIR}/${PANEL_NAME}"; then
    print_success "Control panel installed to ${BIN_DIR}/${PANEL_NAME}"
  else
    print_error "Failed to install control panel"
    exit 1
  fi

  # Create desktop entry
  print_status "Creating desktop entry..."
  local desktop_entry="/usr/share/applications/${PANEL_NAME}.desktop"

  cat >"$desktop_entry" <<EOF
[Desktop Entry]
Name=Casper Excalibur Control
Comment=Control power profiles and keyboard backlight for Casper Excalibur laptops
Exec=gnome-terminal -- ${PANEL_NAME}
Icon=input-keyboard
Terminal=true
Type=Application
Categories=System;Settings;HardwareSettings;
Keywords=casper;keyboard;backlight;power;rgb;excalibur;
StartupNotify=true
EOF

  if [[ -f "$desktop_entry" ]]; then
    print_success "Desktop entry created"
    # Update desktop database
    if command -v update-desktop-database &>/dev/null; then
      update-desktop-database /usr/share/applications/
    fi
  fi

  # Set up sudoers rule for passwordless LED control
  print_status "Setting up passwordless LED control..."
  local sudoers_file="/etc/sudoers.d/${PANEL_NAME}"

  cat >"$sudoers_file" <<'EOF'
# Allow users to control Casper keyboard backlight without password
%sudo ALL=(ALL) NOPASSWD: /usr/bin/tee /sys/class/leds/casper\:\:kbd_backlight/led_control
%wheel ALL=(ALL) NOPASSWD: /usr/bin/tee /sys/class/leds/casper\:\:kbd_backlight/led_control
EOF

  chmod 440 "$sudoers_file"

  # Test the sudoers file syntax
  if visudo -c -f "$sudoers_file" &>/dev/null; then
    print_success "Sudoers rule created (passwordless LED control)"
  else
    print_warning "Sudoers rule has syntax issues, removing it"
    rm -f "$sudoers_file"
    print_status "You'll need to enter password when controlling LEDs"
  fi
}

# Uninstall everything
uninstall() {
  print_status "Uninstalling Casper Excalibur components..."

  # Unload module if loaded
  if lsmod | grep -q "$DRIVER_NAME"; then
    print_status "Unloading kernel module..."
    modprobe -r "$DRIVER_NAME" || true
  fi

  # Remove module file
  if [[ -f "${MODULE_DIR}/${DRIVER_NAME}.ko.zst" ]]; then
    rm -f "${MODULE_DIR}/${DRIVER_NAME}.ko.zst"
    print_success "Kernel module removed"
    depmod -a
  fi

  # Remove auto-loading config
  if [[ -f "${MODULES_LOAD_DIR}/${DRIVER_NAME}.conf" ]]; then
    rm -f "${MODULES_LOAD_DIR}/${DRIVER_NAME}.conf"
    print_success "Auto-loading config removed"
  fi

  # Remove control panel
  if [[ -f "${BIN_DIR}/${PANEL_NAME}" ]]; then
    rm -f "${BIN_DIR}/${PANEL_NAME}"
    print_success "Control panel removed"
  fi

  # Remove desktop entry
  if [[ -f "/usr/share/applications/${PANEL_NAME}.desktop" ]]; then
    rm -f "/usr/share/applications/${PANEL_NAME}.desktop"
    print_success "Desktop entry removed"
    if command -v update-desktop-database &>/dev/null; then
      update-desktop-database /usr/share/applications/
    fi
  fi

  # Remove sudoers rule
  if [[ -f "/etc/sudoers.d/${PANEL_NAME}" ]]; then
    rm -f "/etc/sudoers.d/${PANEL_NAME}"
    print_success "Sudoers rule removed"
  fi

  print_success "Uninstallation complete"
}

# Show usage
show_usage() {
  echo "Casper Excalibur Universal Installer"
  echo ""
  echo "Usage: $0 [options]"
  echo ""
  echo "Options:"
  echo "  --uninstall    Remove both components"
  echo "  --driver-only  Install only the WMI driver"
  echo "  --panel-only   Install only the control panel"
  echo "  --help         Show this help"
  echo ""
  echo "Default: Install both components"
  echo ""
  echo "Examples:"
  echo "  $0                    # Install both driver and control panel"
  echo "  $0 --driver-only      # Install only the WMI driver"
  echo "  $0 --panel-only       # Install only the control panel"
  echo "  $0 --uninstall        # Remove everything"
}

# Test installation
test_installation() {
  print_status "Testing installation..."

  if [[ "$INSTALL_DRIVER" == "true" ]]; then
    if lsmod | grep -q "$DRIVER_NAME"; then
      print_success "✓ WMI driver is loaded"
    else
      print_warning "⚠ WMI driver is not loaded (might be normal)"
    fi
  fi

  if [[ "$INSTALL_PANEL" == "true" ]]; then
    if [[ -x "${BIN_DIR}/${PANEL_NAME}" ]]; then
      print_success "✓ Control panel is installed"
      print_status "You can now run: ${PANEL_NAME}"
    else
      print_error "✗ Control panel installation failed"
    fi
  fi
}

# Show post-install information
show_post_install() {
  echo ""
  echo -e "${BOLD}${GREEN}Installation Complete!${NC}"
  echo ""

  if [[ "$INSTALL_DRIVER" == "true" ]]; then
    echo -e "${BOLD}WMI Driver:${NC}"
    echo "  • Module installed and configured for auto-loading"
    echo "  • LED control interface: /sys/class/leds/casper::kbd_backlight/led_control"
    echo "  • Test command: echo '372ffffff' | sudo tee /sys/class/leds/casper::kbd_backlight/led_control"
    echo ""
  fi

  if [[ "$INSTALL_PANEL" == "true" ]]; then
    echo -e "${BOLD}Control Panel:${NC}"
    echo "  • Command: ${PANEL_NAME}"
    echo "  • Desktop entry created (check your applications menu)"
    echo "  • Passwordless LED control configured"
    echo ""
  fi

  echo -e "${BOLD}Next Steps:${NC}"
  echo "  1. Reboot your system for the driver to auto-load: sudo reboot"
  echo "  2. After reboot, run: ${PANEL_NAME}"
  echo "  3. Enjoy your RGB keyboard control!"
  echo ""

  print_warning "Note: The driver will only work on supported Casper Excalibur models"
}

# Main function
main() {
  # Parse command line arguments
  INSTALL_DRIVER="true"
  INSTALL_PANEL="true"

  case "${1:-}" in
  --uninstall)
    check_root
    print_header
    uninstall
    exit 0
    ;;
  --driver-only)
    INSTALL_PANEL="false"
    ;;
  --panel-only)
    INSTALL_DRIVER="false"
    ;;
  --help | -h)
    show_usage
    exit 0
    ;;
  "")
    # Default: install both
    ;;
  *)
    print_error "Unknown option: $1"
    show_usage
    exit 1
    ;;
  esac

  # Start installation
  print_header
  check_root
  check_dependencies
  check_files

  # Install components
  if [[ "$INSTALL_DRIVER" == "true" ]]; then
    install_driver
  fi

  if [[ "$INSTALL_PANEL" == "true" ]]; then
    install_panel
  fi

  # Test and show results
  test_installation
  show_post_install
}

# Run main function
main "$@"
