#!/bin/bash
# ============================================================================
#                     WMI Driver + Control Panel Installer
#                       github.com/thekayrasari/excalibur
# ============================================================================
# Usage:
#   sudo ./install.sh                  — interactive wizard
#   sudo ./install.sh install          — non-interactive manual install
#   sudo ./install.sh uninstall        — non-interactive manual uninstall
#   sudo ./install.sh dkms-install     — register + build via DKMS
#   sudo ./install.sh dkms-uninstall   — remove DKMS registration
# ============================================================================
set -euo pipefail

# ── Colours ──────────────────────────────────────────────────────────────────
R='\033[0;31m'
G='\033[0;32m'
Y='\033[0;33m'
B='\033[0;34m'
C='\033[0;36m'
M='\033[0;35m'
W='\033[1;37m'
D='\033[2m'
NC='\033[0m'

# ── Constants ─────────────────────────────────────────────────────────────────
MODULE_NAME="excalibur"
DKMS_NAME="excalibur-wmi"
DKMS_VERSION="1.0.0"
KO_FILE="${MODULE_NAME}.ko"
LIB_MODULES="/lib/modules/$(uname -r)"
INSTALL_DIR="${LIB_MODULES}/extra"
MODULES_LOAD_DIR="/etc/modules-load.d"
DKMS_SRC_DIR="/usr/src/${DKMS_NAME}-${DKMS_VERSION}"
CONTROL_PANEL_SRC="control-panel.py"
CONTROL_PANEL_DEST="/usr/local/lib/excalibur-control-panel.py"
CONTROL_PANEL_BIN="/usr/local/bin/excalibur-panel"
UDEV_RULES_FILE="/etc/udev/rules.d/99-excalibur.rules"
DESKTOP_FILE="/usr/share/applications/excalibur-panel.desktop"
INITRAMFS_CMD=""
PYTHON_BIN="python3"
PKG_INSTALL=""
HEADERS_PKG=""
COMPILER=""       # "gcc" or "clang" — detected at runtime from /proc/version
MAKE_FLAGS=""     # extra flags forwarded to every make invocation

# ── Helpers ───────────────────────────────────────────────────────────────────
print_banner() {
    echo -e "  ${W}WMI Driver + Control Panel Installer${NC}   ${D}github.com/thekayrasari/excalibur${NC}"
    echo -e "  ${D}────────────────────────────────────────────────────────────────────${NC}"
    echo ""
}

step()    { echo -e "\n${B}[${W}*${B}]${NC} ${W}${1}${NC}"; }
ok()      { echo -e "  ${G}✔${NC}  ${1}"; }
warn()    { echo -e "  ${Y}⚠${NC}  ${Y}${1}${NC}"; }
err()     { echo -e "  ${R}✘${NC}  ${R}${1}${NC}"; }
info()    { echo -e "  ${D}${1}${NC}"; }
divider() { echo -e "  ${D}────────────────────────────────────────${NC}"; }

ask() {
    local question="$1"
    local default="${2:-y}"
    local prompt
    if [[ "$default" == "y" ]]; then
        prompt="${W}[${G}Y${W}/n]${NC}"
    else
        prompt="${W}[${R}y${W}/N]${NC}"
    fi
    echo -ne "\n  ${C}?${NC}  ${question} ${prompt} "
    read -r answer
    answer="${answer:-$default}"
    [[ "$answer" =~ ^[Yy]$ ]]
}

require_root() {
    if [[ "$(id -u)" -ne 0 ]]; then
        err "This script must be run as root."
        echo -e "  ${D}Run: ${W}sudo ./install.sh${NC}"
        exit 1
    fi
}

# ── Distro detection ──────────────────────────────────────────────────────────
detect_distro() {
    DISTRO_ID=""
    DISTRO_NAME=""
    if [[ -f /etc/os-release ]]; then
        . /etc/os-release
        DISTRO_ID="${ID:-unknown}"
        DISTRO_NAME="${PRETTY_NAME:-unknown}"
    fi
    case "$DISTRO_ID" in
        arch|manjaro|endeavouros|cachyos)
            INITRAMFS_CMD="mkinitcpio -P"
            PKG_INSTALL="pacman -S --noconfirm"
            HEADERS_PKG="linux-headers"
            ;;
        ubuntu|debian|linuxmint|pop)
            INITRAMFS_CMD="update-initramfs -u"
            PKG_INSTALL="apt-get install -y"
            HEADERS_PKG="linux-headers-$(uname -r)"
            ;;
        fedora|centos|rhel|rocky|almalinux)
            INITRAMFS_CMD="dracut --force"
            PKG_INSTALL="dnf install -y"
            HEADERS_PKG="kernel-devel"
            ;;
        opensuse*|suse)
            INITRAMFS_CMD="mkinitrd"
            PKG_INSTALL="zypper install -y"
            HEADERS_PKG="kernel-devel"
            ;;
        *)
            warn "Unrecognised distro '${DISTRO_ID}'. initramfs update will be skipped."
            INITRAMFS_CMD=""
            ;;
    esac
}

# ── Compiler detection ────────────────────────────────────────────────────────
# Read /proc/version to match the compiler that built the running kernel.
# Building with a different compiler than the host kernel risks ABI issues;
# this is the root cause of install failures on CachyOS (clang-built by default).
detect_compiler() {
    if grep -q "clang" /proc/version 2>/dev/null; then
        COMPILER="clang"
        MAKE_FLAGS="CC=clang LLVM=1 LLVM_IAS=1"
        ok "Kernel was built with clang — using clang for module build"
    else
        COMPILER="gcc"
        MAKE_FLAGS=""
        ok "Kernel was built with gcc — using gcc for module build"
    fi
}

# ── Pre-flight checks ─────────────────────────────────────────────────────────
check_build_tools() {
    local missing=()

    if [[ "$COMPILER" == "clang" ]]; then
        command -v clang   &>/dev/null || missing+=("clang")
        command -v make    &>/dev/null || missing+=("make")
        command -v llvm-ar &>/dev/null || missing+=("llvm")
        if [[ ${#missing[@]} -gt 0 ]]; then
            err "Missing build tools for clang build: ${missing[*]}"
            [[ -n "$PKG_INSTALL" ]] && info "Install with: ${PKG_INSTALL} clang llvm make"
            return 1
        fi
        ok "Build tools present (clang, llvm, make)"
    else
        command -v make &>/dev/null || missing+=("make")
        command -v gcc  &>/dev/null || missing+=("gcc")
        if [[ ${#missing[@]} -gt 0 ]]; then
            err "Missing build tools: ${missing[*]}"
            [[ -n "$PKG_INSTALL" ]] && info "Install with: ${PKG_INSTALL} ${missing[*]}"
            return 1
        fi
        ok "Build tools present (make, gcc)"
    fi
}

check_kernel_headers() {
    if [[ -d "${LIB_MODULES}/build" ]]; then
        ok "Kernel headers found"
        return 0
    fi
    err "Kernel headers not found at ${LIB_MODULES}/build"
    if [[ "$DISTRO_ID" =~ ^(arch|manjaro|endeavouros|cachyos)$ ]]; then
        local kernel_pkg
        kernel_pkg=$(pacman -Qo "${LIB_MODULES}" 2>/dev/null | awk '{print $NF}' || true)
        if [[ -n "$kernel_pkg" ]]; then
            info "Install with: pacman -S ${kernel_pkg}-headers"
        else
            info "Install the headers package for your kernel variant, e.g.:"
            info "  pacman -S linux-cachyos-headers"
            info "  pacman -S linux-cachyos-lts-headers"
            info "  pacman -S linux-cachyos-bore-headers"
            info "Run 'uname -r' and match the suffix to find yours."
        fi
    elif [[ -n "$HEADERS_PKG" && -n "$PKG_INSTALL" ]]; then
        info "Install with: ${PKG_INSTALL} ${HEADERS_PKG}"
    fi
    return 1
}

check_wmi_guid() {
    local guid="644C5791-B7B0-4123-A90B-E93876E0DAAD"
    if ls /sys/bus/wmi/devices/ 2>/dev/null | grep -qi "${guid}"; then
        ok "WMI GUID found in firmware"
    else
        warn "WMI GUID not found — driver may not bind on this machine"
    fi
}

check_python() {
    local py
    py=$(command -v python3 2>/dev/null || command -v python 2>/dev/null || true)
    if [[ -z "$py" ]]; then
        err "python3 not found"
        return 1
    fi
    PYTHON_BIN="$py"
    local ver
    ver=$("$PYTHON_BIN" --version 2>&1)
    ok "Python found: ${ver}"
}

check_textual() {
    if "$PYTHON_BIN" -c "import textual" 2>/dev/null; then
        local ver
        ver=$("$PYTHON_BIN" -c "import textual; print(textual.__version__)" 2>/dev/null)
        ok "Textual ${ver} already installed"
        return 0
    fi
    return 1
}

check_dkms() {
    if command -v dkms &>/dev/null; then
        ok "DKMS found: $(dkms --version 2>/dev/null | head -1)"
        return 0
    fi
    warn "dkms not found"
    if [[ -n "$PKG_INSTALL" ]]; then
        info "Install with: ${PKG_INSTALL} dkms"
    fi
    return 1
}

install_textual() {
    step "Installing Textual Python library"
    if "$PYTHON_BIN" -m pip install textual 2>/dev/null; then
        ok "Textual installed"
        return 0
    fi
    if "$PYTHON_BIN" -m pip install textual --break-system-packages 2>/dev/null; then
        ok "Textual installed (--break-system-packages)"
        return 0
    fi
    err "Could not install Textual automatically."
    info "Try: sudo pip install textual --break-system-packages"
    return 1
}

# ── Driver — manual build/install/uninstall ───────────────────────────────────
build_driver() {
    step "Building kernel module (compiler: ${COMPILER})"
    if [[ ! -f "excalibur.c" || ! -f "Makefile" ]]; then
        err "excalibur.c or Makefile not found in $(pwd)"
        info "Run this script from the excalibur source directory."
        exit 1
    fi
    # shellcheck disable=SC2086
    make clean $MAKE_FLAGS 2>/dev/null || true
    # shellcheck disable=SC2086
    if make $MAKE_FLAGS; then
        ok "Module built: ${KO_FILE}"
    else
        err "Build failed"
        exit 1
    fi
}

install_driver() {
    step "Installing kernel module"
    mkdir -p "${INSTALL_DIR}"
    cp "${KO_FILE}" "${INSTALL_DIR}/"
    depmod -a
    ok "Module installed: ${INSTALL_DIR}/${KO_FILE}"

    step "Configuring auto-load at boot"
    mkdir -p "${MODULES_LOAD_DIR}"
    echo "${MODULE_NAME}" > "${MODULES_LOAD_DIR}/${MODULE_NAME}.conf"
    ok "Auto-load config: ${MODULES_LOAD_DIR}/${MODULE_NAME}.conf"

    if [[ -n "${INITRAMFS_CMD}" ]]; then
        step "Updating initramfs"
        ${INITRAMFS_CMD} && ok "initramfs updated" || warn "initramfs update failed (non-fatal)"
    fi

    step "Loading module"
    if modprobe "${MODULE_NAME}"; then
        ok "Module loaded"
    else
        warn "modprobe failed — check: sudo dmesg | grep excalibur"
    fi
}

uninstall_driver() {
    step "Unloading kernel module"
    rmmod "${MODULE_NAME}" 2>/dev/null && ok "Module unloaded" || warn "Module was not loaded"

    step "Removing files"
    rm -f "${MODULES_LOAD_DIR}/${MODULE_NAME}.conf"
    rm -f "${INSTALL_DIR}/${KO_FILE}"
    depmod -a
    ok "Driver files removed"

    if [[ -n "${INITRAMFS_CMD}" ]]; then
        step "Updating initramfs"
        ${INITRAMFS_CMD} && ok "Done" || warn "Failed (non-fatal)"
    fi
}

# ── Driver — DKMS build/install/uninstall ────────────────────────────────────
#
# DKMS keeps a copy of the sources in /usr/src/<name>-<version>/ and rebuilds
# the module automatically whenever a new kernel is installed.  This is the
# recommended persistent installation method for out-of-tree modules.

install_dkms_driver() {
    step "Registering ${DKMS_NAME}/${DKMS_VERSION} with DKMS"

    if dkms status "${DKMS_NAME}/${DKMS_VERSION}" 2>/dev/null | grep -q "installed"; then
        warn "${DKMS_NAME}/${DKMS_VERSION} is already installed in DKMS."
        info "To upgrade: sudo ./install.sh dkms-uninstall && sudo ./install.sh dkms-install"
        return 0
    fi

    # Copy sources into DKMS tree.
    mkdir -p "${DKMS_SRC_DIR}"
    for f in excalibur.c Makefile Kconfig dkms.conf; do
        if [[ -f "$f" ]]; then
            cp "$f" "${DKMS_SRC_DIR}/"
        else
            err "Required file '$f' not found in $(pwd)"
            exit 1
        fi
    done
    ok "Sources copied to ${DKMS_SRC_DIR}"

    # Propagate clang flags into dkms.conf if the running kernel uses clang.
    if [[ "$COMPILER" == "clang" && -n "$MAKE_FLAGS" ]]; then
        # Append CC/LLVM flags to the MAKE line in the installed dkms.conf.
        sed -i "s|^MAKE\[0\]=\"make|MAKE[0]=\"make ${MAKE_FLAGS}|" \
            "${DKMS_SRC_DIR}/dkms.conf"
        ok "Clang build flags injected into dkms.conf"
    fi

    dkms add -m "${DKMS_NAME}" -v "${DKMS_VERSION}"
    ok "DKMS source registered"

    step "Building module with DKMS (kernel $(uname -r))"
    dkms build -m "${DKMS_NAME}" -v "${DKMS_VERSION}"
    ok "Build complete"

    step "Installing module with DKMS"
    dkms install -m "${DKMS_NAME}" -v "${DKMS_VERSION}"
    ok "Module installed via DKMS"

    step "Configuring auto-load at boot"
    mkdir -p "${MODULES_LOAD_DIR}"
    echo "${MODULE_NAME}" > "${MODULES_LOAD_DIR}/${MODULE_NAME}.conf"
    ok "Auto-load config: ${MODULES_LOAD_DIR}/${MODULE_NAME}.conf"

    step "Loading module"
    if modprobe "${MODULE_NAME}"; then
        ok "Module loaded"
    else
        warn "modprobe failed — check: sudo dmesg | grep excalibur"
    fi
}

uninstall_dkms_driver() {
    step "Removing DKMS registration for ${DKMS_NAME}/${DKMS_VERSION}"

    rmmod "${MODULE_NAME}" 2>/dev/null && ok "Module unloaded" || warn "Module was not loaded"

    if dkms status "${DKMS_NAME}" 2>/dev/null | grep -q "${DKMS_VERSION}"; then
        dkms remove "${DKMS_NAME}/${DKMS_VERSION}" --all
        ok "DKMS registration removed"
    else
        warn "No DKMS entry found for ${DKMS_NAME}/${DKMS_VERSION}"
    fi

    rm -rf "${DKMS_SRC_DIR}"
    ok "Source tree ${DKMS_SRC_DIR} removed"

    rm -f "${MODULES_LOAD_DIR}/${MODULE_NAME}.conf"
    depmod -a
    ok "Module auto-load config removed"
}

# ── udev rules ────────────────────────────────────────────────────────────────
install_udev_rules() {
    step "Installing udev rules"
    cat > "${UDEV_RULES_FILE}" <<'UDEV'
# excalibur-wmi udev rules
# Grants write access to LED zones and power plan for wheel/sudo group members,
# allowing the control panel to run without sudo.

# Keyboard LED zones
SUBSYSTEM=="leds", KERNEL=="excalibur*", \
    RUN+="/bin/sh -c 'chown root:wheel /sys%p/brightness /sys%p/color /sys%p/mode /sys%p/raw 2>/dev/null; chmod g+w /sys%p/brightness /sys%p/color /sys%p/mode /sys%p/raw 2>/dev/null'", \
    RUN+="/bin/sh -c 'chown root:sudo  /sys%p/brightness /sys%p/color /sys%p/mode /sys%p/raw 2>/dev/null; chmod g+w /sys%p/brightness /sys%p/color /sys%p/mode /sys%p/raw 2>/dev/null'"

# hwmon (fan speeds + power plan)
SUBSYSTEM=="hwmon", ATTR{name}=="excalibur_wmi", \
    RUN+="/bin/sh -c 'chown root:wheel /sys%p/pwm1 /sys%p/fan1_input /sys%p/fan2_input 2>/dev/null; chmod g+rw /sys%p/pwm1 2>/dev/null'", \
    RUN+="/bin/sh -c 'chown root:sudo  /sys%p/pwm1 /sys%p/fan1_input /sys%p/fan2_input 2>/dev/null; chmod g+rw /sys%p/pwm1 2>/dev/null'"
UDEV
    udevadm control --reload-rules
    udevadm trigger
    ok "udev rules installed: ${UDEV_RULES_FILE}"
    info "Re-login (or reboot) for group permissions to take effect."
    info "Ensure your user is in the 'wheel' group (Fedora/Arch) or 'sudo' group (Debian/Ubuntu):"
    info "  sudo usermod -aG wheel \$SUDO_USER   (Fedora/Arch)"
    info "  sudo usermod -aG sudo  \$SUDO_USER   (Debian/Ubuntu)"
}

uninstall_udev_rules() {
    step "Removing udev rules"
    rm -f "${UDEV_RULES_FILE}"
    udevadm control --reload-rules
    ok "Removed ${UDEV_RULES_FILE}"
}

# ── Control panel ─────────────────────────────────────────────────────────────
install_control_panel() {
    step "Installing control panel"
    if [[ ! -f "${CONTROL_PANEL_SRC}" ]]; then
        err "${CONTROL_PANEL_SRC} not found in $(pwd)"
        exit 1
    fi

    mkdir -p "$(dirname "${CONTROL_PANEL_DEST}")"
    cp "${CONTROL_PANEL_SRC}" "${CONTROL_PANEL_DEST}"
    chmod 644 "${CONTROL_PANEL_DEST}"
    ok "Control panel source: ${CONTROL_PANEL_DEST}"

    cat > "${CONTROL_PANEL_BIN}" <<LAUNCHER
#!/bin/bash
# Excalibur Control Panel launcher — auto-generated by installer
exec "${PYTHON_BIN}" "${CONTROL_PANEL_DEST}" "\$@"
LAUNCHER
    chmod 755 "${CONTROL_PANEL_BIN}"
    ok "Launcher: ${CONTROL_PANEL_BIN}"
}

uninstall_control_panel() {
    step "Removing control panel"
    rm -f "${CONTROL_PANEL_DEST}" "${CONTROL_PANEL_BIN}" "${DESKTOP_FILE}"
    ok "Control panel removed"
}

install_desktop_entry() {
    step "Installing desktop entry"
    mkdir -p "$(dirname "${DESKTOP_FILE}")"
    cat > "${DESKTOP_FILE}" <<DESKTOP
[Desktop Entry]
Version=1.0
Type=Application
Name=Excalibur Control Panel
GenericName=Laptop Control Panel
Comment=RGB lighting, fan monitoring and power plan control for Excalibur laptops
Exec=bash -c 'exec ${PYTHON_BIN} ${CONTROL_PANEL_DEST}'
Icon=input-keyboard
Terminal=true
Categories=System;HardwareSettings;
Keywords=excalibur;rgb;keyboard;fan;laptop;
StartupNotify=false
DESKTOP
    ok "Desktop entry: ${DESKTOP_FILE}"
}

# ── Verify ────────────────────────────────────────────────────────────────────
verify_install() {
    step "Verifying installation"
    divider

    lsmod | grep -q "^${MODULE_NAME}" \
        && ok "Kernel module loaded" \
        || warn "Module NOT loaded — try: sudo modprobe excalibur"

    local led_count
    led_count=$(ls /sys/class/leds/ 2>/dev/null | grep -c "excalibur" || true)
    [[ "$led_count" -gt 0 ]] \
        && ok "LED sysfs nodes found (${led_count} zones)" \
        || warn "No LED sysfs nodes found yet"

    local hwmon_found=false
    for f in /sys/class/hwmon/hwmon*/name; do
        [[ "$(cat "$f" 2>/dev/null)" == "excalibur_wmi" ]] && hwmon_found=true && break
    done
    $hwmon_found && ok "hwmon device found" || warn "hwmon device not found yet"

    if command -v dkms &>/dev/null; then
        dkms status "${DKMS_NAME}" 2>/dev/null | grep -q "${DKMS_VERSION}" \
            && ok "DKMS registration active (auto-rebuild on kernel upgrades)" \
            || info "DKMS not active — manual install only"
    fi

    [[ -x "${CONTROL_PANEL_BIN}" ]] \
        && ok "Control panel launcher ready: excalibur-panel" \
        || warn "Control panel launcher not found"

    "$PYTHON_BIN" -c "import textual" 2>/dev/null \
        && ok "Textual library available" \
        || warn "Textual not importable — run: sudo pip install textual --break-system-packages"

    divider
}

# ── Interactive wizard ────────────────────────────────────────────────────────
interactive_install() {
    print_banner

    echo -e "  ${W}Welcome to the Excalibur WMI installer.${NC}"
    echo -e "  ${D}This wizard installs the kernel driver and TUI control panel.${NC}"
    echo ""
    echo -e "  ${D}System : ${W}${DISTRO_NAME:-Unknown}${NC}  |  Kernel: ${W}$(uname -r)${NC}  |  Compiler: ${W}${COMPILER}${NC}"
    echo ""

    step "Pre-flight checks"
    divider
    check_build_tools    || { err "Cannot continue without build tools."; exit 1; }
    check_kernel_headers || { err "Cannot continue without kernel headers."; exit 1; }
    check_wmi_guid
    check_python         || { err "Cannot continue without Python 3."; exit 1; }
    divider

    # Driver install method
    echo ""
    echo -e "  ${M}── Kernel Driver ─────────────────────────────────────────────${NC}"

    INSTALL_DRIVER=false
    USE_DKMS=false

    if lsmod 2>/dev/null | grep -q "^${MODULE_NAME}"; then
        warn "excalibur module is already loaded."
        ask "Reinstall / upgrade the kernel driver?" && INSTALL_DRIVER=true || INSTALL_DRIVER=false
    else
        ask "Install the excalibur-wmi kernel driver?" && INSTALL_DRIVER=true || INSTALL_DRIVER=false
        [[ "$INSTALL_DRIVER" == false ]] && warn "Skipping driver — hardware controls will not work."
    fi

    if [[ "$INSTALL_DRIVER" == true ]]; then
        echo ""
        echo -e "  ${D}DKMS automatically rebuilds the module for every new kernel.${NC}"
        if check_dkms 2>/dev/null; then
            ask "Use DKMS for persistent install (recommended)?" \
                && USE_DKMS=true || USE_DKMS=false
        else
            warn "DKMS not available — will use manual install."
            USE_DKMS=false
        fi
    fi

    # Control panel
    echo ""
    echo -e "  ${M}── Control Panel ─────────────────────────────────────────────${NC}"
    INSTALL_PANEL=false
    INSTALL_TEXTUAL=false
    if ask "Install the Excalibur TUI control panel?"; then
        INSTALL_PANEL=true
        if check_textual; then
            INSTALL_TEXTUAL=false
        else
            warn "Textual is not installed."
            ask "Install Textual automatically?" && INSTALL_TEXTUAL=true || INSTALL_TEXTUAL=false
        fi
    fi

    # udev
    echo ""
    echo -e "  ${M}── Permissions ───────────────────────────────────────────────${NC}"
    echo -e "  ${D}udev rules let you run the control panel without sudo.${NC}"
    INSTALL_UDEV=false
    ask "Install udev rules (recommended)?" && INSTALL_UDEV=true || INSTALL_UDEV=false
    [[ "$INSTALL_UDEV" == false ]] && warn "You will need sudo to run the control panel."

    # Desktop
    echo ""
    echo -e "  ${M}── Desktop Integration ───────────────────────────────────────${NC}"
    INSTALL_DESKTOP=false
    ask "Install a desktop entry (adds the app to your launcher)?" \
        && INSTALL_DESKTOP=true || INSTALL_DESKTOP=false

    # Summary
    echo ""
    echo -e "  ${M}── Summary ────────────────────────────────────────────────────${NC}"
    divider
    info "  Compiler: ${COMPILER}  (${MAKE_FLAGS:-no extra flags})"
    if [[ "$INSTALL_DRIVER" == true ]]; then
        [[ "$USE_DKMS" == true ]] \
            && info "  ✦ Kernel driver  →  DKMS (auto-rebuild)" \
            || info "  ✦ Kernel driver  →  manual install"
    else
        info "  ○ Kernel driver (skip)"
    fi
    [[ "$INSTALL_TEXTUAL"  == true ]] && info "  ✦ Textual library"           || info "  ○ Textual (skip)"
    [[ "$INSTALL_PANEL"    == true ]] && info "  ✦ Control panel  →  ${CONTROL_PANEL_BIN}" || info "  ○ Control panel (skip)"
    [[ "$INSTALL_UDEV"     == true ]] && info "  ✦ udev rules (no-sudo access)" || info "  ○ udev rules (skip)"
    [[ "$INSTALL_DESKTOP"  == true ]] && info "  ✦ Desktop entry"              || info "  ○ Desktop entry (skip)"
    divider

    if ! ask "Proceed with installation?"; then
        echo -e "\n  ${Y}Cancelled.${NC}\n"
        exit 0
    fi

    echo ""
    if [[ "$INSTALL_DRIVER" == true ]]; then
        if [[ "$USE_DKMS" == true ]]; then
            install_dkms_driver
        else
            build_driver
            install_driver
        fi
    fi
    [[ "$INSTALL_TEXTUAL" == true ]] && install_textual
    [[ "$INSTALL_PANEL"   == true ]] && install_control_panel
    [[ "$INSTALL_UDEV"    == true ]] && install_udev_rules
    [[ "$INSTALL_DESKTOP" == true ]] && install_desktop_entry

    verify_install

    echo ""
    echo -e "  ${G}╔══════════════════════════════════════════════════════════╗${NC}"
    echo -e "  ${G}║${NC}  ${W}✦  Excalibur installation complete!  ✦${NC}               ${G}║${NC}"
    echo -e "  ${G}╚══════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "  ${W}Launch the control panel:${NC}"
    if [[ "$INSTALL_UDEV" == true ]]; then
        echo -e "    ${C}excalibur-panel${NC}     ${D}(no sudo needed after re-login)${NC}"
    else
        echo -e "    ${C}sudo excalibur-panel${NC}"
    fi
    echo ""
    echo -e "  ${D}Reload driver if needed:  sudo modprobe excalibur${NC}"
    echo -e "  ${D}Check driver status:      sudo dmesg | grep excalibur${NC}"
    echo ""
}

interactive_uninstall() {
    print_banner
    echo -e "  ${R}Uninstall mode${NC}\n"

    # Detect if DKMS was used and offer to remove that registration too.
    if command -v dkms &>/dev/null && \
       dkms status "${DKMS_NAME}" 2>/dev/null | grep -q "${DKMS_VERSION}"; then
        ask "Remove DKMS registration for ${DKMS_NAME}?" \
            && uninstall_dkms_driver || true
    else
        ask "Remove the kernel driver (manual install)?" \
            && uninstall_driver || true
    fi

    ask "Remove the control panel?"    && uninstall_control_panel || true
    ask "Remove udev rules?"           && uninstall_udev_rules    || true
    echo -e "\n  ${G}✔${NC}  Uninstall complete.\n"
}

# ── Entry point ───────────────────────────────────────────────────────────────
require_root
detect_distro
detect_compiler

case "${1:-}" in
    install)
        print_banner
        step "Non-interactive manual install"
        check_build_tools    || exit 1
        check_kernel_headers || exit 1
        check_python         || exit 1
        check_textual        || install_textual
        build_driver
        install_driver
        install_control_panel
        install_udev_rules
        install_desktop_entry
        verify_install
        ok "All done. Run: excalibur-panel"
        ;;
    uninstall)
        print_banner
        uninstall_driver
        uninstall_control_panel
        uninstall_udev_rules
        ok "Uninstall complete."
        ;;
    dkms-install)
        print_banner
        step "Non-interactive DKMS install"
        check_dkms           || exit 1
        check_python         || exit 1
        check_textual        || install_textual
        install_dkms_driver
        install_control_panel
        install_udev_rules
        install_desktop_entry
        verify_install
        ok "All done. Module will auto-rebuild on kernel upgrades."
        ok "Run: excalibur-panel"
        ;;
    dkms-uninstall)
        print_banner
        uninstall_dkms_driver
        uninstall_control_panel
        uninstall_udev_rules
        ok "DKMS uninstall complete."
        ;;
    "")
        interactive_install
        ;;
    *)
        echo -e "Usage: sudo $0 [install|uninstall|dkms-install|dkms-uninstall]"
        exit 1
        ;;
esac
