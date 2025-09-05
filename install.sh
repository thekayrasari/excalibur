#!/bin/bash

# Installer script for Excalibur WMI kernel module
# This script builds, installs, and configures the module for auto-loading.
# Supports major distros: Arch, Ubuntu/Debian, Fedora, etc.
# Prerequisites: Kernel headers, build-essential/make/gcc
# Usage: sudo ./install.sh [install|uninstall]

set -e

MODULE_NAME="excalibur"
MODULE_VERSION="1.0"
SRC_DIR="$(pwd)"
KO_FILE="${MODULE_NAME}.ko"
LIB_MODULES="/lib/modules/$(uname -r)"
INSTALL_DIR="${LIB_MODULES}/extra"
MODULES_LOAD_DIR="/etc/modules-load.d"
INITRAMFS_CMD=""

# Function to detect distro and set initramfs command
detect_distro() {
    if [ -f /etc/os-release ]; then
        . /etc/os-release
        case "$ID" in
            arch|manjaro)
                INITRAMFS_CMD="mkinitcpio -P"
                ;;
            ubuntu|debian|linuxmint)
                INITRAMFS_CMD="update-initramfs -u"
                ;;
            fedora|centos|rhel|rocky|almalinux)
                INITRAMFS_CMD="dracut --force"
                ;;
            opensuse|suse)
                INITRAMFS_CMD="mkinitrd"
                ;;
            *)
                echo "Warning: Unsupported distro '${ID}'. Skipping initramfs update."
                INITRAMFS_CMD=""
                ;;
        esac
    else
        echo "Warning: Could not detect distro. Skipping initramfs update."
        INITRAMFS_CMD=""
    fi
}

# Function to check prerequisites
check_prerequisites() {
    if ! command -v make &> /dev/null || ! command -v gcc &> /dev/null; then
        echo "Error: Missing build tools (make, gcc). Install them:"
        echo "  - Ubuntu/Debian: sudo apt install build-essential"
        echo "  - Arch: sudo pacman -S base-devel"
        echo "  - Fedora: sudo dnf install make gcc"
        exit 1
    fi

    if [ ! -d "/usr/src/linux-headers-$(uname -r)" ] && [ ! -d "${LIB_MODULES}/build" ]; then
        echo "Error: Kernel headers not installed. Install them:"
        echo "  - Ubuntu/Debian: sudo apt install linux-headers-$(uname -r)"
        echo "  - Arch: sudo pacman -S linux-headers"
        echo "  - Fedora: sudo dnf install kernel-devel"
        exit 1
    fi
}

# Function to build the module
build_module() {
    echo "Building the module..."
    make clean
    make
    if [ ! -f "${KO_FILE}" ]; then
        echo "Error: Build failed. ${KO_FILE} not found."
        exit 1
    fi
}

# Function to install the module
install_module() {
    echo "Installing the module to ${INSTALL_DIR}..."
    mkdir -p "${INSTALL_DIR}"
    cp "${KO_FILE}" "${INSTALL_DIR}/"
    depmod -a

    echo "Configuring auto-load..."
    mkdir -p "${MODULES_LOAD_DIR}"
    echo "${MODULE_NAME}" > "${MODULES_LOAD_DIR}/${MODULE_NAME}.conf"

    if [ -n "${INITRAMFS_CMD}" ]; then
        echo "Updating initramfs..."
        ${INITRAMFS_CMD}
    fi

    echo "Loading the module..."
    modprobe "${MODULE_NAME}" || echo "Warning: Failed to load module. Check dmesg for errors."

    echo "Installation complete! Reboot to verify."
}

# Function to uninstall the module
uninstall_module() {
    echo "Unloading the module..."
    rmmod "${MODULE_NAME}" || echo "Warning: Module not loaded."

    echo "Removing auto-load config..."
    rm -f "${MODULES_LOAD_DIR}/${MODULE_NAME}.conf"

    echo "Removing module file..."
    rm -f "${INSTALL_DIR}/${KO_FILE}"
    depmod -a

    if [ -n "${INITRAMFS_CMD}" ]; then
        echo "Updating initramfs..."
        ${INITRAMFS_CMD}
    fi

    echo "Uninstallation complete!"
}

# Main logic
if [ "$(id -u)" -ne 0 ]; then
    echo "Error: Run this script as root (sudo)."
    exit 1
fi

detect_distro
check_prerequisites

case "$1" in
    install)
        build_module
        install_module
        ;;
    uninstall)
        uninstall_module
        ;;
    *)
        echo "Usage: sudo $0 [install|uninstall]"
        exit 1
        ;;
esac

