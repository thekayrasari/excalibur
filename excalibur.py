#!/usr/bin/env python3
"""
Power & Backlight Control Panel for Casper Excalibur
A TUI application to control power profiles and advanced keyboard backlight
Compatible with casper-wmi driver
"""

import curses
import subprocess
import sys
import os
import time
from typing import List, Optional, Tuple

class CasperBacklightTUI:
    def __init__(self):
        self.current_selection = 0
        self.menu_items = [
            "Power Profile",
            "Keyboard Backlight",
            "RGB Color Control",
            "Preset Effects",
            "System Info",
            "Exit"
        ]
        
        # Power profile settings
        self.power_profiles = []
        self.current_power_profile = ""
        
        # Casper backlight settings
        self.regions = 3  # Number of regions (first digit)
        self.mode = 1     # Mode (0-7, second digit)
        self.brightness = 2  # Brightness (0-2, third digit)
        self.color = "ffffff"  # RGB color (6 hex digits)
        
        # LED modes for Casper
        self.led_modes = {
            0: "Off",
            1: "Static",
            2: "Blinking", 
            3: "Breathing",
            4: "Pulsing",
            5: "Rainbow Pulsing",
            6: "Rainbow Pulsing Alt",
            7: "Rainbow Wave"
        }
        
        # Predefined colors
        self.preset_colors = {
            "White": "ffffff",
            "Red": "ff0000",
            "Green": "00ff00", 
            "Blue": "0000ff",
            "Purple": "ff00ff",
            "Cyan": "00ffff",
            "Yellow": "ffff00",
            "Orange": "ff8000"
        }
        
        self.status_message = ""
        self.casper_led_path = "/sys/class/leds/casper::kbd_backlight/led_control"
        
    def check_dependencies(self):
        """Check if required tools are available"""
        missing = []
        
        # Check for powerprofilesctl
        try:
            subprocess.run(['powerprofilesctl'], capture_output=True, check=True)
        except (subprocess.CalledProcessError, FileNotFoundError):
            missing.append("power-profiles-daemon (powerprofilesctl)")
        
        # Check for casper-wmi driver
        if not os.path.exists(self.casper_led_path):
            missing.append("casper-wmi driver (/sys/class/leds/casper::kbd_backlight/led_control)")
            
        return missing

    def get_power_profiles(self):
        """Get available power profiles"""
        try:
            result = subprocess.run(['powerprofilesctl', 'list'], 
                                  capture_output=True, text=True, check=True)
            profiles = []
            current = ""
            
            for line in result.stdout.split('\n'):
                line = line.strip()
                if line.startswith('*'):
                    # Current active profile
                    profile = line.split()[1].rstrip(':')
                    profiles.append(profile)
                    current = profile
                elif line and ':' in line and not line.startswith(' '):
                    # Other available profiles
                    profile = line.split(':')[0].strip()
                    if profile not in profiles:
                        profiles.append(profile)
            
            self.power_profiles = profiles
            self.current_power_profile = current
            return True
        except (subprocess.CalledProcessError, FileNotFoundError):
            self.power_profiles = ["balanced", "power-saver", "performance"]  # fallback
            self.current_power_profile = "balanced"
            return False

    def set_power_profile(self, profile: str):
        """Set power profile"""
        try:
            subprocess.run(['powerprofilesctl', 'set', profile], check=True)
            self.current_power_profile = profile
            self.status_message = f"Power profile set to: {profile}"
            return True
        except subprocess.CalledProcessError:
            self.status_message = f"Failed to set power profile: {profile}"
            return False

    def set_casper_backlight(self, regions: int = None, mode: int = None, 
                           brightness: int = None, color: str = None):
        """Set Casper keyboard backlight using the LED control interface"""
        if regions is not None:
            self.regions = max(1, min(9, regions))
        if mode is not None:
            self.mode = max(0, min(7, mode))
        if brightness is not None:
            self.brightness = max(0, min(2, brightness))
        if color is not None:
            # Ensure color is 6 hex digits
            color = color.replace('#', '').lower()
            if len(color) == 6 and all(c in '0123456789abcdef' for c in color):
                self.color = color
        
        # Build control string: regions + mode + brightness + color
        control_string = f"{self.regions}{self.mode}{self.brightness}{self.color}"
        
        try:
            subprocess.run(['sudo', 'tee', self.casper_led_path], 
                         input=control_string, text=True, 
                         capture_output=True, check=True)
            self.status_message = f"LED set: Mode={self.led_modes[self.mode]}, Brightness={self.brightness}, Color=#{self.color}"
            return True
        except subprocess.CalledProcessError as e:
            self.status_message = f"Failed to set LED control: {e}"
            return False

    def apply_preset_effect(self, preset_name: str):
        """Apply a preset effect"""
        presets = {
            "off": (3, 0, 0, "000000"),
            "default_static": (3, 1, 2, "ffffff"),
            "default_blinking": (3, 2, 2, "ffffff"),
            "default_breathing": (3, 3, 2, "ffffff"),
            "default_pulsing": (3, 4, 2, "ffffff"),
            "rainbow_pulsing": (3, 5, 2, "ffffff"),
            "rainbow_pulsing_alt": (3, 6, 2, "ffffff"),
            "rainbow_wave": (3, 7, 2, "ffffff"),
            "gaming_red": (3, 1, 2, "ff0000"),
            "gaming_blue": (3, 1, 2, "0000ff"),
            "cyber_purple": (3, 3, 2, "8000ff"),
            "hacker_green": (3, 2, 1, "00ff00")
        }
        
        if preset_name in presets:
            regions, mode, brightness, color = presets[preset_name]
            self.set_casper_backlight(regions, mode, brightness, color)

    def draw_header(self, stdscr):
        """Draw the header"""
        height, width = stdscr.getmaxyx()
        title = "Casper Excalibur Control Panel"
        stdscr.addstr(0, (width - len(title)) // 2, title, curses.A_BOLD)
        stdscr.addstr(1, 0, "=" * width)

    def draw_status(self, stdscr):
        """Draw status line"""
        height, width = stdscr.getmaxyx()
        if self.status_message:
            stdscr.addstr(height - 2, 0, self.status_message[:width-1])

    def draw_main_menu(self, stdscr):
        """Draw main menu"""
        height, width = stdscr.getmaxyx()
        start_y = 4
        
        for i, item in enumerate(self.menu_items):
            y = start_y + i * 2
            if y >= height - 4:
                break
                
            if i == self.current_selection:
                stdscr.addstr(y, 2, f"> {item}", curses.A_REVERSE)
            else:
                stdscr.addstr(y, 4, item)

    def draw_power_profile_menu(self, stdscr):
        """Draw power profile selection menu"""
        height, width = stdscr.getmaxyx()
        
        stdscr.addstr(3, 2, "Power Profiles:", curses.A_BOLD)
        stdscr.addstr(4, 2, f"Current: {self.current_power_profile}")
        
        start_y = 6
        for i, profile in enumerate(self.power_profiles):
            y = start_y + i
            if y >= height - 4:
                break
                
            marker = "* " if profile == self.current_power_profile else "  "
            if i == self.current_selection:
                stdscr.addstr(y, 2, f"{marker}{profile}", curses.A_REVERSE)
            else:
                stdscr.addstr(y, 2, f"{marker}{profile}")
        
        stdscr.addstr(height - 4, 2, "Press ENTER to select, ESC to go back")

    def draw_backlight_menu(self, stdscr):
        """Draw backlight control menu"""
        height, width = stdscr.getmaxyx()
        
        stdscr.addstr(3, 2, "Keyboard Backlight Control:", curses.A_BOLD)
        
        # Current settings
        stdscr.addstr(5, 2, f"Current Settings:")
        stdscr.addstr(6, 4, f"Regions: {self.regions}")
        stdscr.addstr(7, 4, f"Mode: {self.mode} ({self.led_modes.get(self.mode, 'Unknown')})")
        stdscr.addstr(8, 4, f"Brightness: {self.brightness}/2")
        stdscr.addstr(9, 4, f"Color: #{self.color.upper()}")
        stdscr.addstr(10, 4, f"Control String: {self.regions}{self.mode}{self.brightness}{self.color}")
        
        # Controls
        stdscr.addstr(12, 2, "Controls:")
        stdscr.addstr(13, 4, "m/M: Change mode (0-7)")
        stdscr.addstr(14, 4, "b/B: Change brightness (0-2)")
        stdscr.addstr(15, 4, "r/R: Change regions (1-9)")
        stdscr.addstr(16, 4, "c: Color selection")
        stdscr.addstr(17, 4, "0: Turn off")
        stdscr.addstr(18, 4, "ESC: Go back")

    def draw_color_menu(self, stdscr):
        """Draw RGB color control menu"""
        height, width = stdscr.getmaxyx()
        
        stdscr.addstr(3, 2, "RGB Color Control:", curses.A_BOLD)
        stdscr.addstr(4, 2, f"Current Color: #{self.color.upper()}")
        
        # Parse current RGB values
        try:
            r = int(self.color[0:2], 16)
            g = int(self.color[2:4], 16) 
            b = int(self.color[4:6], 16)
        except ValueError:
            r = g = b = 255
            
        stdscr.addstr(6, 2, f"RGB Values:")
        stdscr.addstr(7, 4, f"Red:   {r:3d} (0x{r:02X})")
        stdscr.addstr(8, 4, f"Green: {g:3d} (0x{g:02X})")
        stdscr.addstr(9, 4, f"Blue:  {b:3d} (0x{b:02X})")
        
        # Preset colors
        stdscr.addstr(11, 2, "Preset Colors:")
        y = 12
        colors_per_row = 4
        col = 0
        for i, (name, hex_val) in enumerate(self.preset_colors.items()):
            x = 4 + (col * 12)
            if i == self.current_selection:
                stdscr.addstr(y, x, f"{name}", curses.A_REVERSE)
            else:
                stdscr.addstr(y, x, f"{name}")
            
            col += 1
            if col >= colors_per_row:
                col = 0
                y += 1
        
        stdscr.addstr(height - 6, 2, "Controls:")
        stdscr.addstr(height - 5, 4, "Arrow keys: Navigate presets")
        stdscr.addstr(height - 4, 4, "ENTER: Apply selected color")
        stdscr.addstr(height - 3, 4, "ESC: Go back")

    def draw_presets_menu(self, stdscr):
        """Draw preset effects menu"""
        height, width = stdscr.getmaxyx()
        
        stdscr.addstr(3, 2, "Preset Effects:", curses.A_BOLD)
        
        presets = [
            ("Off", "off"),
            ("Default Static", "default_static"),
            ("Default Blinking", "default_blinking"), 
            ("Default Breathing", "default_breathing"),
            ("Default Pulsing", "default_pulsing"),
            ("Rainbow Pulsing", "rainbow_pulsing"),
            ("Rainbow Pulsing Alt", "rainbow_pulsing_alt"),
            ("Rainbow Wave", "rainbow_wave"),
            ("Gaming Red", "gaming_red"),
            ("Gaming Blue", "gaming_blue"),
            ("Cyber Purple", "cyber_purple"),
            ("Hacker Green", "hacker_green")
        ]
        
        start_y = 5
        for i, (name, _) in enumerate(presets):
            y = start_y + i
            if y >= height - 4:
                break
                
            if i == self.current_selection:
                stdscr.addstr(y, 4, f"> {name}", curses.A_REVERSE)
            else:
                stdscr.addstr(y, 6, name)
        
        stdscr.addstr(height - 4, 2, "Press ENTER to apply preset, ESC to go back")
        
        return presets

    def draw_system_info(self, stdscr):
        """Draw system information"""
        height, width = stdscr.getmaxyx()
        
        stdscr.addstr(3, 2, "System Information:", curses.A_BOLD)
        
        y = 5
        try:
            # CPU info
            with open('/proc/cpuinfo', 'r') as f:
                for line in f:
                    if line.startswith('model name'):
                        cpu = line.split(':')[1].strip()
                        stdscr.addstr(y, 2, f"CPU: {cpu[:width-10]}")
                        y += 1
                        break
            
            # Memory info
            with open('/proc/meminfo', 'r') as f:
                mem_total = mem_available = 0
                for line in f:
                    if line.startswith('MemTotal:'):
                        mem_total = int(line.split()[1]) // 1024  # Convert to MB
                    elif line.startswith('MemAvailable:'):
                        mem_available = int(line.split()[1]) // 1024
                        break
                
                stdscr.addstr(y, 2, f"Memory: {mem_available}MB / {mem_total}MB available")
                y += 1
            
            # Power profile
            stdscr.addstr(y, 2, f"Power Profile: {self.current_power_profile}")
            y += 1
            
            # Casper backlight info
            stdscr.addstr(y, 2, f"Casper LED Control: {self.regions}{self.mode}{self.brightness}{self.color}")
            y += 1
            stdscr.addstr(y, 2, f"LED Mode: {self.led_modes.get(self.mode, 'Unknown')}")
            y += 1
            
            # Driver status
            if os.path.exists(self.casper_led_path):
                stdscr.addstr(y, 2, "Casper WMI Driver: ✓ Loaded")
            else:
                stdscr.addstr(y, 2, "Casper WMI Driver: ✗ Not found")
            y += 1
            
        except Exception as e:
            stdscr.addstr(y, 2, f"Error reading system info: {str(e)}")
        
        stdscr.addstr(height - 4, 2, "Press ESC to go back")

    def handle_main_menu_input(self, key):
        """Handle input in main menu"""
        if key == curses.KEY_UP and self.current_selection > 0:
            self.current_selection -= 1
        elif key == curses.KEY_DOWN and self.current_selection < len(self.menu_items) - 1:
            self.current_selection += 1
        elif key == ord('\n') or key == ord(' '):
            return self.menu_items[self.current_selection].lower().replace(' ', '_')
        elif key == ord('q') or key == 27:  # ESC
            return 'exit'
        return None

    def handle_power_profile_input(self, key):
        """Handle input in power profile menu"""
        if key == curses.KEY_UP and self.current_selection > 0:
            self.current_selection -= 1
        elif key == curses.KEY_DOWN and self.current_selection < len(self.power_profiles) - 1:
            self.current_selection += 1
        elif key == ord('\n') or key == ord(' '):
            self.set_power_profile(self.power_profiles[self.current_selection])
            return 'main'
        elif key == 27:  # ESC
            return 'main'
        return None

    def handle_backlight_input(self, key):
        """Handle input in backlight menu"""
        if key == ord('m'):
            self.mode = (self.mode + 1) % 8
            self.set_casper_backlight()
        elif key == ord('M'):
            self.mode = (self.mode - 1) % 8
            self.set_casper_backlight()
        elif key == ord('b'):
            self.brightness = (self.brightness + 1) % 3
            self.set_casper_backlight()
        elif key == ord('B'):
            self.brightness = (self.brightness - 1) % 3
            self.set_casper_backlight()
        elif key == ord('r'):
            self.regions = min(9, self.regions + 1)
            self.set_casper_backlight()
        elif key == ord('R'):
            self.regions = max(1, self.regions - 1)
            self.set_casper_backlight()
        elif key == ord('c'):
            return 'rgb_color_control'
        elif key == ord('0'):
            self.set_casper_backlight(mode=0)  # Turn off
        elif key == 27:  # ESC
            return 'main'
        return None

    def handle_color_input(self, key):
        """Handle input in color menu"""
        color_names = list(self.preset_colors.keys())
        
        if key == curses.KEY_UP:
            self.current_selection = max(0, self.current_selection - 4)
        elif key == curses.KEY_DOWN:
            self.current_selection = min(len(color_names) - 1, self.current_selection + 4)
        elif key == curses.KEY_LEFT and self.current_selection > 0:
            self.current_selection -= 1
        elif key == curses.KEY_RIGHT and self.current_selection < len(color_names) - 1:
            self.current_selection += 1
        elif key == ord('\n') or key == ord(' '):
            color_name = color_names[self.current_selection]
            self.set_casper_backlight(color=self.preset_colors[color_name])
            return 'keyboard_backlight'
        elif key == 27:  # ESC
            return 'keyboard_backlight'
        return None

    def handle_presets_input(self, key, presets):
        """Handle input in presets menu"""
        if key == curses.KEY_UP and self.current_selection > 0:
            self.current_selection -= 1
        elif key == curses.KEY_DOWN and self.current_selection < len(presets) - 1:
            self.current_selection += 1
        elif key == ord('\n') or key == ord(' '):
            _, preset_key = presets[self.current_selection]
            self.apply_preset_effect(preset_key)
            return 'main'
        elif key == 27:  # ESC
            return 'main'
        return None

    def run(self, stdscr):
        """Main application loop"""
        curses.curs_set(0)  # Hide cursor
        stdscr.nodelay(1)   # Non-blocking input
        stdscr.timeout(1000)  # 1 second timeout
        
        # Initialize
        self.get_power_profiles()
        
        current_mode = 'main'
        last_update = 0
        
        while True:
            # Update data periodically
            current_time = time.time()
            if current_time - last_update > 2:  # Update every 2 seconds
                self.get_power_profiles()
                last_update = current_time
            
            stdscr.clear()
            
            # Draw UI based on current mode
            self.draw_header(stdscr)
            
            if current_mode == 'main':
                self.current_selection = min(self.current_selection, len(self.menu_items) - 1)
                self.draw_main_menu(stdscr)
            elif current_mode == 'power_profile':
                self.current_selection = min(self.current_selection, len(self.power_profiles) - 1)
                self.draw_power_profile_menu(stdscr)
            elif current_mode == 'keyboard_backlight':
                self.draw_backlight_menu(stdscr)
            elif current_mode == 'rgb_color_control':
                self.current_selection = min(self.current_selection, len(self.preset_colors) - 1)
                self.draw_color_menu(stdscr)
            elif current_mode == 'preset_effects':
                presets = self.draw_presets_menu(stdscr)
                self.current_selection = min(self.current_selection, len(presets) - 1)
            elif current_mode == 'system_info':
                self.draw_system_info(stdscr)
            
            self.draw_status(stdscr)
            stdscr.refresh()
            
            # Handle input
            key = stdscr.getch()
            if key == -1:  # No input
                continue
                
            # Clear status message on input
            if self.status_message:
                self.status_message = ""
            
            if current_mode == 'main':
                result = self.handle_main_menu_input(key)
                if result == 'exit':
                    break
                elif result in ['power_profile', 'keyboard_backlight', 'rgb_color_control', 
                              'preset_effects', 'system_info']:
                    current_mode = result
                    self.current_selection = 0
            elif current_mode == 'power_profile':
                result = self.handle_power_profile_input(key)
                if result == 'main':
                    current_mode = 'main'
                    self.current_selection = 0
            elif current_mode == 'keyboard_backlight':
                result = self.handle_backlight_input(key)
                if result == 'main':
                    current_mode = 'main'
                    self.current_selection = 1
                elif result == 'rgb_color_control':
                    current_mode = 'rgb_color_control'
                    self.current_selection = 0
            elif current_mode == 'rgb_color_control':
                result = self.handle_color_input(key)
                if result == 'keyboard_backlight':
                    current_mode = 'keyboard_backlight'
                    self.current_selection = 0
            elif current_mode == 'preset_effects':
                presets = [
                    ("Off", "off"), ("Default Static", "default_static"),
                    ("Default Blinking", "default_blinking"), ("Default Breathing", "default_breathing"),
                    ("Default Pulsing", "default_pulsing"), ("Rainbow Pulsing", "rainbow_pulsing"),
                    ("Rainbow Pulsing Alt", "rainbow_pulsing_alt"), ("Rainbow Wave", "rainbow_wave"),
                    ("Gaming Red", "gaming_red"), ("Gaming Blue", "gaming_blue"),
                    ("Cyber Purple", "cyber_purple"), ("Hacker Green", "hacker_green")
                ]
                result = self.handle_presets_input(key, presets)
                if result == 'main':
                    current_mode = 'main'
                    self.current_selection = 3
            elif current_mode == 'system_info':
                if key == 27:  # ESC
                    current_mode = 'main'
                    self.current_selection = 4

def main():
    app = CasperBacklightTUI()
    
    # Check dependencies
    missing = app.check_dependencies()
    if missing:
        print("Warning: Missing dependencies:")
        for dep in missing:
            print(f"  - {dep}")
        print("\nSome features may not work properly.")
        print("Make sure casper-wmi module is loaded: sudo modprobe casper-wmi")
        input("Press Enter to continue anyway...")
    
    try:
        curses.wrapper(app.run)
    except KeyboardInterrupt:
        print("\nExiting...")
    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
