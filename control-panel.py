#!/usr/bin/env python3
"""
excalibur_tui.py — Textual TUI control center for the excalibur-wmi kernel driver.

Requires:
    pip install textual

Run:
    sudo python3 excalibur_tui.py
    (sudo required for sysfs writes; reads work without it)
"""

from __future__ import annotations

import asyncio
import glob
import os
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import ClassVar

from textual import on, work
from textual.app import App, ComposeResult
from textual.message import Message
from textual.binding import Binding
from textual.color import Color
from textual.containers import Container, Horizontal, ScrollableContainer, Vertical
from textual.css.query import NoMatches
from textual.reactive import reactive
from textual.timer import Timer
from textual.widget import Widget
from textual.widgets import (
    Button,
    Footer,
    Header,
    Label,
    ListItem,
    ListView,
    OptionList,
    RadioButton,
    RadioSet,
    Select,
    Static,
    Switch,
    TabbedContent,
    TabPane,
)
from textual.widgets.option_list import Option

# ─────────────────────────────────────────────────────────────────────────────
# Sysfs helpers
# ─────────────────────────────────────────────────────────────────────────────

LED_BASE = "/sys/class/leds"
HWMON_BASE = "/sys/class/hwmon"
ZONE_NAMES = ("left", "middle", "right", "corners")

POWER_PLANS = {
    1: ("High Power", "🔥"),
    2: ("Gaming",     "🎮"),
    3: ("Text Mode",  "📝"),
    4: ("Low Power",  "🌿"),
}

COLOR_PRESETS: list[tuple[str, str]] = [
    ("White",   "FFFFFF"),
    ("Red",     "FF0000"),
    ("Orange",  "FF8000"),
    ("Yellow",  "FFFF00"),
    ("Green",   "00FF00"),
    ("Cyan",    "00FFFF"),
    ("Blue",    "0000FF"),
    ("Magenta", "FF00FF"),
    ("Purple",  "800080"),
    ("Pink",    "FF69B4"),
    ("Off",     "000000"),
]


def _read(path: str) -> str | None:
    try:
        return Path(path).read_text().strip()
    except (OSError, PermissionError):
        return None


def _write(path: str, value: str) -> tuple[bool, str]:
    """Returns (success, error_message)."""
    try:
        Path(path).write_text(value)
        return True, ""
    except PermissionError:
        return False, f"Permission denied: {path}\nTry running with sudo."
    except OSError as exc:
        return False, str(exc)


def find_hwmon_path() -> str | None:
    """Find the hwmon directory for excalibur_wmi by reading name files."""
    for name_path in glob.glob(f"{HWMON_BASE}/hwmon*/name"):
        val = _read(name_path)
        if val == "excalibur_wmi":
            return str(Path(name_path).parent)
    return None


def led_path(zone: str, attr: str) -> str:
    return f"{LED_BASE}/excalibur::kbd_backlight-{zone}/{attr}"


def get_available_modes(zone: str = "left") -> list[str]:
    raw = _read(led_path(zone, "available_modes"))
    if raw:
        return raw.split()
    # Fallback to driver-known modes
    return ["off", "static", "blink", "fade", "heartbeat", "wave", "random", "rainbow"]


# ─────────────────────────────────────────────────────────────────────────────
# Internal state
# ─────────────────────────────────────────────────────────────────────────────

@dataclass
class ZoneState:
    color: str = "FFFFFF"
    mode: str = "static"
    brightness: int = 2


@dataclass
class AppState:
    zones: dict[str, ZoneState] = field(
        default_factory=lambda: {z: ZoneState() for z in ZONE_NAMES}
    )
    power_plan: int = 2
    cpu_rpm: int = 0
    gpu_rpm: int = 0
    hwmon_path: str | None = None
    has_write_perm: bool = True


# ─────────────────────────────────────────────────────────────────────────────
# Custom widgets
# ─────────────────────────────────────────────────────────────────────────────

ASCII_LOGO = r"""
  ███████╗██╗  ██╗ ██████╗ █████╗ ██╗     ██╗██████╗ ██╗   ██╗██████╗
  ██╔════╝╚██╗██╔╝██╔════╝██╔══██╗██║     ██║██╔══██╗██║   ██║██╔══██╗
  █████╗   ╚███╔╝ ██║     ███████║██║     ██║██████╔╝██║   ██║██████╔╝
  ██╔══╝   ██╔██╗ ██║     ██╔══██║██║     ██║██╔══██╗██║   ██║██╔══██╗
  ███████╗██╔╝ ██╗╚██████╗██║  ██║███████╗██║██████╔╝╚██████╔╝██║  ██║
  ╚══════╝╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝╚══════╝╚═╝╚═════╝  ╚═════╝ ╚═╝  ╚═╝
                          WMI Control Center
"""


class LogoWidget(Static):
    """Renders the ASCII art logo."""

    DEFAULT_CSS = """
    LogoWidget {
        color: $accent;
        text-style: bold;
        padding: 0 1;
        height: auto;
    }
    """

    def render(self) -> str:
        return ASCII_LOGO


class FanGauge(Static):
    """A single fan speed display with color-coded RPM indicator."""

    DEFAULT_CSS = """
    FanGauge {
        border: round $panel;
        padding: 0 2;
        height: 7;
        width: 1fr;
    }
    FanGauge .fan-label { text-style: bold; color: $text-muted; }
    FanGauge .fan-rpm   { text-style: bold; text-align: center; }
    FanGauge .fan-bar   { color: $success; }
    """

    rpm: reactive[int] = reactive(0)

    def __init__(self, label: str, **kwargs):
        super().__init__(**kwargs)
        self._label = label

    def compose(self) -> ComposeResult:
        yield Label(self._label, classes="fan-label")
        yield Label("0 RPM", id="rpm-value", classes="fan-rpm")
        yield Label("", id="fan-bar", classes="fan-bar")

    def watch_rpm(self, value: int) -> None:
        try:
            rpm_label = self.query_one("#rpm-value", Label)
            bar_label = self.query_one("#fan-bar", Label)
        except NoMatches:
            return

        # Color thresholds
        if value == 0:
            color = "dim"
            bar_text = "  ○ STOPPED"
        elif value < 2000:
            color = "green"
            bar_text = self._make_bar(value, 6000, "█")
        elif value < 4000:
            color = "yellow"
            bar_text = self._make_bar(value, 6000, "█")
        else:
            color = "red"
            bar_text = self._make_bar(value, 6000, "█")

        rpm_label.update(f"[{color} bold]{value:,} RPM[/{color} bold]")
        bar_label.update(f"[{color}]{bar_text}[/{color}]")

    @staticmethod
    def _make_bar(value: int, max_val: int, char: str, width: int = 20) -> str:
        filled = int((min(value, max_val) / max_val) * width)
        return f"  [{char * filled}{'░' * (width - filled)}]"


class PowerPlanPanel(Static):
    """Displays and controls the active power plan."""

    DEFAULT_CSS = """
    PowerPlanPanel {
        border: round $panel;
        padding: 1 2;
        height: auto;
    }
    PowerPlanPanel Label { margin-bottom: 1; }
    PowerPlanPanel #plan-buttons { height: auto; }
    PowerPlanPanel Button { width: 1fr; margin-right: 1; }
    PowerPlanPanel Button.-active-plan {
        background: $accent;
        color: $background;
        text-style: bold;
    }
    """

    active_plan: reactive[int] = reactive(2)

    def compose(self) -> ComposeResult:
        yield Label("⚡ Power Plan", id="plan-title")
        with Horizontal(id="plan-buttons"):
            for num, (name, icon) in POWER_PLANS.items():
                yield Button(
                    f"{icon} {name}",
                    id=f"plan-{num}",
                    variant="default",
                )

    def watch_active_plan(self, plan: int) -> None:
        for num in POWER_PLANS:
            try:
                btn = self.query_one(f"#plan-{num}", Button)
                if num == plan:
                    btn.add_class("-active-plan")
                else:
                    btn.remove_class("-active-plan")
            except NoMatches:
                pass


class ColorSwatch(Static):
    """A clickable color swatch."""

    DEFAULT_CSS = """
    ColorSwatch {
        width: 4;
        height: 2;
        border: tall transparent;
        content-align: center middle;
    }
    ColorSwatch:hover { border: tall $accent; }
    ColorSwatch.-selected { border: tall white; }
    """

    class Selected(Message):
        """Posted when this swatch is clicked."""
        def __init__(self, swatch: "ColorSwatch") -> None:
            super().__init__()
            self.swatch = swatch

    def __init__(self, name: str, hex_color: str, **kwargs):
        super().__init__(name[:2], **kwargs)
        self._color_name = name
        self._hex = hex_color
        # Set background based on hex
        r = int(hex_color[0:2], 16)
        g = int(hex_color[2:4], 16)
        b = int(hex_color[4:6], 16)
        self.styles.background = f"rgb({r},{g},{b})"
        # Choose text color for contrast
        lum = 0.299 * r + 0.587 * g + 0.114 * b
        self.styles.color = "black" if lum > 128 else "white"

    def on_click(self) -> None:
        self.post_message(self.Selected(self))

    @property
    def hex_color(self) -> str:
        return self._hex

    @property
    def color_name(self) -> str:
        return self._color_name


class LightingPanel(Static):
    """Full keyboard lighting control panel."""

    DEFAULT_CSS = """
    LightingPanel {
        border: round $panel;
        padding: 1 2;
        height: auto;
    }
    LightingPanel .section-title {
        text-style: bold;
        color: $accent;
        margin-top: 1;
        margin-bottom: 0;
    }
    LightingPanel #zone-select, LightingPanel #mode-select {
        width: 100%;
        margin-bottom: 1;
    }
    LightingPanel #color-swatches {
        height: 4;
        margin-bottom: 1;
    }
    LightingPanel #brightness-row {
        height: 3;
        margin-bottom: 1;
    }
    LightingPanel Button.bright-btn {
        width: 1fr;
        margin-right: 1;
    }
    LightingPanel Button.bright-btn.-active-bright {
        background: $accent;
        color: $background;
        text-style: bold;
    }
    LightingPanel #apply-btn {
        width: 100%;
        margin-top: 1;
    }
    LightingPanel #selected-color-preview {
        width: 100%;
        height: 3;
        content-align: center middle;
        border: round $panel;
        margin-bottom: 1;
    }
    LightingPanel #status-msg {
        color: $success;
        height: 1;
        margin-top: 1;
    }
    """

    selected_zone: reactive[str] = reactive("left")
    selected_color: reactive[str] = reactive("FFFFFF")
    selected_mode: reactive[str] = reactive("static")
    selected_brightness: reactive[int] = reactive(2)

    def __init__(self, state: AppState, modes: list[str], **kwargs):
        super().__init__(**kwargs)
        self._state = state
        self._modes = modes

    def compose(self) -> ComposeResult:
        yield Label("💡 Keyboard Lighting", classes="section-title")

        yield Label("Zone", classes="section-title")
        zone_options = [
            ("Left",    "left"),
            ("Middle",  "middle"),
            ("Right",   "right"),
            ("Corners", "corners"),
            ("All",     "all"),
        ]
        yield Select(
            [(name, val) for name, val in zone_options],
            value="left",
            id="zone-select",
        )

        yield Label("Mode", classes="section-title")
        yield Select(
            [(m.capitalize(), m) for m in self._modes],
            value="static",
            id="mode-select",
        )

        yield Label("Color Presets", classes="section-title")
        with Horizontal(id="color-swatches"):
            for name, hex_color in COLOR_PRESETS:
                yield ColorSwatch(name, hex_color, id=f"swatch-{hex_color}")

        yield Label("Color Preview", classes="section-title")
        yield Static("  FFFFFF — White  ", id="selected-color-preview")

        yield Label("Brightness", classes="section-title")
        with Horizontal(id="brightness-row"):
            yield Button("○  Off",    id="bright-0", classes="bright-btn")
            yield Button("◑  Medium", id="bright-1", classes="bright-btn")
            yield Button("●  Full",   id="bright-2", classes="bright-btn -active-bright")

        yield Button("✦  Apply Lighting", id="apply-btn", variant="primary")
        yield Label("", id="status-msg")

    def watch_selected_color(self, hex_color: str) -> None:
        try:
            preview = self.query_one("#selected-color-preview", Static)
            r = int(hex_color[0:2], 16)
            g = int(hex_color[2:4], 16)
            b = int(hex_color[4:6], 16)
            # Find color name
            name = next(
                (n for n, h in COLOR_PRESETS if h == hex_color.upper()),
                "Custom"
            )
            preview.update(f"  #{hex_color.upper()} — {name}  ")
            preview.styles.background = f"rgb({r},{g},{b})"
            lum = 0.299 * r + 0.587 * g + 0.114 * b
            preview.styles.color = "black" if lum > 128 else "white"
        except NoMatches:
            pass

    def watch_selected_brightness(self, value: int) -> None:
        for i in range(3):
            try:
                btn = self.query_one(f"#bright-{i}", Button)
                if i == value:
                    btn.add_class("-active-bright")
                else:
                    btn.remove_class("-active-bright")
            except NoMatches:
                pass

    def set_status(self, msg: str, ok: bool = True) -> None:
        try:
            lbl = self.query_one("#status-msg", Label)
            color = "green" if ok else "red"
            lbl.update(f"[{color}]{msg}[/{color}]")
        except NoMatches:
            pass


# ─────────────────────────────────────────────────────────────────────────────
# Main Application
# ─────────────────────────────────────────────────────────────────────────────

APP_CSS = """
Screen {
    background: #0d0d0d;
}

Header {
    background: #1a1a2e;
    color: $accent;
}

Footer {
    background: #1a1a2e;
}

TabbedContent {
    height: 1fr;
}

TabPane {
    padding: 1 2;
}

/* Dashboard tab */
#dashboard-grid {
    height: auto;
    layout: grid;
    grid-size: 2;
    grid-gutter: 1;
    margin-bottom: 1;
}

#fan-section {
    height: auto;
    layout: grid;
    grid-size: 2;
    grid-gutter: 1;
    margin-bottom: 1;
}

/* Lighting tab */
#lighting-container {
    height: 1fr;
}

/* Power tab */
#power-container {
    height: auto;
    padding: 1;
}

#power-container PowerPlanPanel {
    margin-bottom: 1;
}

/* Notification / warning bar */
#perm-warning {
    background: $warning;
    color: $background;
    text-style: bold;
    padding: 0 2;
    height: 2;
    content-align: center middle;
    display: none;
}
#perm-warning.-visible { display: block; }

/* Info panel */
#info-panel {
    border: round $panel;
    padding: 1 2;
    height: auto;
    margin-bottom: 1;
}
#info-panel Label { margin-bottom: 0; }

/* Misc */
.muted { color: $text-muted; }
.bold  { text-style: bold; }
.danger { color: $error; }
.success { color: $success; }
"""


class ExcaliburApp(App):
    """Excalibur WMI TUI Control Center."""

    CSS = APP_CSS
    TITLE = "Excalibur WMI Control Center"
    SUB_TITLE = "v1.0"

    BINDINGS: ClassVar[list[Binding]] = [
        Binding("q", "quit", "Quit"),
        Binding("1", "tab_dashboard", "Dashboard"),
        Binding("2", "tab_lighting", "Lighting"),
        Binding("3", "tab_power", "Power"),
        Binding("r", "refresh_now", "Refresh"),
    ]

    def __init__(self):
        super().__init__()
        self._state = AppState()
        self._state.hwmon_path = find_hwmon_path()
        self._modes = get_available_modes()
        self._fan_timer: Timer | None = None

    # ── Layout ───────────────────────────────────────────────────────────────

    def compose(self) -> ComposeResult:
        yield Header(show_clock=True)

        yield Static(
            "⚠  Write permission denied — run with sudo for full control",
            id="perm-warning",
        )

        with TabbedContent(id="tabs"):
            with TabPane("📊 Dashboard", id="tab-dashboard"):
                yield self._build_dashboard()

            with TabPane("💡 Lighting", id="tab-lighting"):
                yield self._build_lighting()

            with TabPane("⚡ Power", id="tab-power"):
                yield self._build_power()

            with TabPane("ℹ  About", id="tab-about"):
                yield self._build_about()

        yield Footer()

    def _build_dashboard(self) -> Widget:
        return Vertical(
            LogoWidget(),
            Static("Fan Speeds", classes="bold muted"),
            Horizontal(
                FanGauge("🌀 CPU Fan", id="cpu-gauge"),
                FanGauge("🌀 GPU Fan", id="gpu-gauge"),
                id="fan-section",
            ),
            PowerPlanPanel(id="plan-panel-dash"),
            id="dashboard-container",
        )

    def _build_lighting(self) -> Widget:
        return ScrollableContainer(
            LightingPanel(self._state, self._modes, id="lighting-panel"),
            id="lighting-container",
        )

    def _build_power(self) -> Widget:
        return ScrollableContainer(
            Static("⚡ Power Management", classes="bold"),
            PowerPlanPanel(id="plan-panel-power"),
            Static(
                "\n[dim]Changing the power plan immediately alters the firmware fan curves.\n"
                "High Power and Gaming modes will increase fan noise and performance.\n"
                "Text Mode and Low Power prioritise battery life and quiet operation.[/dim]",
            ),
            id="power-container",
        )

    def _build_about(self) -> Widget:
        hwmon = self._state.hwmon_path or "[red]NOT FOUND[/red]"
        driver_present = "✅ Loaded" if self._state.hwmon_path else "❌ Not detected"
        return Vertical(
            Static(ASCII_LOGO),
            Static(
                f"[bold]excalibur-wmi[/bold] Control Center\n\n"
                f"  Driver status : {driver_present}\n"
                f"  hwmon path    : [cyan]{hwmon}[/cyan]\n"
                f"  LED base      : [cyan]{LED_BASE}/excalibur::kbd_backlight-*[/cyan]\n\n"
                "[dim]Source: github.com/thekayrasari/excalibur\n"
                "License: GPL-2.0-or-later[/dim]"
            ),
            id="about-container",
        )

    # ── Lifecycle ─────────────────────────────────────────────────────────────

    def on_mount(self) -> None:
        self._fan_timer = self.set_interval(1.0, self._tick_fans)
        # Initial read
        self._tick_fans()
        self._load_power_plan()

    # ── Fan polling ───────────────────────────────────────────────────────────

    def _tick_fans(self) -> None:
        hwmon = self._state.hwmon_path
        if not hwmon:
            return

        cpu_raw = _read(f"{hwmon}/fan1_input")
        gpu_raw = _read(f"{hwmon}/fan2_input")

        try:
            self._state.cpu_rpm = int(cpu_raw) if cpu_raw else 0
        except ValueError:
            self._state.cpu_rpm = 0

        try:
            self._state.gpu_rpm = int(gpu_raw) if gpu_raw else 0
        except ValueError:
            self._state.gpu_rpm = 0

        # Update gauge widgets
        try:
            self.query_one("#cpu-gauge", FanGauge).rpm = self._state.cpu_rpm
            self.query_one("#gpu-gauge", FanGauge).rpm = self._state.gpu_rpm
        except NoMatches:
            pass

    def _load_power_plan(self) -> None:
        hwmon = self._state.hwmon_path
        if not hwmon:
            return
        raw = _read(f"{hwmon}/pwm1")
        if raw:
            try:
                plan = int(raw)
                self._state.power_plan = plan
                self._update_plan_ui(plan)
            except ValueError:
                pass

    def _update_plan_ui(self, plan: int) -> None:
        for panel_id in ("#plan-panel-dash", "#plan-panel-power"):
            try:
                self.query_one(panel_id, PowerPlanPanel).active_plan = plan
            except NoMatches:
                pass

    # ── Event handlers ────────────────────────────────────────────────────────

    @on(Button.Pressed)
    def on_button_pressed(self, event: Button.Pressed) -> None:
        btn_id = event.button.id or ""

        # Power plan buttons
        if btn_id.startswith("plan-"):
            try:
                plan_num = int(btn_id.split("-")[1])
                self._set_power_plan(plan_num)
            except (ValueError, IndexError):
                pass
            return

        # Brightness buttons
        if btn_id.startswith("bright-"):
            try:
                level = int(btn_id.split("-")[1])
                try:
                    panel = self.query_one("#lighting-panel", LightingPanel)
                    panel.selected_brightness = level
                except NoMatches:
                    pass
            except (ValueError, IndexError):
                pass
            return

        # Apply lighting
        if btn_id == "apply-btn":
            self._apply_lighting()
            return

    @on(Select.Changed, "#zone-select")
    def on_zone_changed(self, event: Select.Changed) -> None:
        pass  # Zone selection is read at apply time

    @on(Select.Changed, "#mode-select")
    def on_mode_changed(self, event: Select.Changed) -> None:
        if event.value and event.value != Select.BLANK:
            try:
                panel = self.query_one("#lighting-panel", LightingPanel)
                panel.selected_mode = str(event.value)
            except NoMatches:
                pass

    @on(ColorSwatch.Selected)
    def on_swatch_selected(self, event: ColorSwatch.Selected) -> None:
        """Handle color swatch clicks."""
        try:
            panel = self.query_one("#lighting-panel", LightingPanel)
            panel.selected_color = event.swatch.hex_color
            for swatch in self.query(ColorSwatch):
                swatch.remove_class("-selected")
            event.swatch.add_class("-selected")
        except NoMatches:
            pass

    # ── Sysfs write actions ───────────────────────────────────────────────────

    def _set_power_plan(self, plan: int) -> None:
        hwmon = self._state.hwmon_path
        if not hwmon:
            self._show_warning("hwmon device not found. Is the driver loaded?")
            return

        ok, err = _write(f"{hwmon}/pwm1", str(plan))
        if not ok:
            self._show_perm_warning(err)
            return

        self._state.power_plan = plan
        self._update_plan_ui(plan)

    def _apply_lighting(self) -> None:
        try:
            panel = self.query_one("#lighting-panel", LightingPanel)
        except NoMatches:
            return

        zone_val = self.query_one("#zone-select", Select).value
        mode_val = self.query_one("#mode-select", Select).value
        color    = panel.selected_color
        bright   = panel.selected_brightness
        zone     = str(zone_val) if zone_val != Select.BLANK else "left"
        mode     = str(mode_val) if mode_val != Select.BLANK else "static"

        # ── Determine which sysfs zones to write color+mode to ───────────────
        # The "all" pseudo-zone expands to all four physical zones.
        # "left", "middle", "right" and "corners" map 1-to-1.
        zones_to_write = list(ZONE_NAMES) if zone == "all" else [zone]

        errors = []

        # ── Step 1: color + mode — always safe per-zone ───────────────────────
        for z in zones_to_write:
            for attr, value in [("color", color), ("mode", mode)]:
                ok, err = _write(led_path(z, attr), value)
                if not ok:
                    errors.append(err)
                    break
            else:
                self._state.zones[z].color = color
                self._state.zones[z].mode  = mode

        if errors:
            panel.set_status("✗ " + errors[0].split("\n")[0], ok=False)
            self._show_perm_warning(errors[0])
            return

        # ── Step 2: brightness ────────────────────────────────────────────────
        # HARDWARE CONSTRAINT: the driver's brightness_set for keyboard zones
        # always uses ZONE_ALL_KBD and broadcasts the *triggering zone's* full
        # color to all three keyboard zones — there is no per-zone brightness.
        # Writing brightness for a single kbd zone (left/middle/right) would
        # therefore overwrite the other zones' colors with the selected one.
        #
        # Safe rules:
        #   "all"     → write brightness once (all colors already set above)
        #   "corners" → always safe, corners are fully independent
        #   single kbd zone (left/middle/right) → SKIP brightness write
        #
        # The brightness slider still controls what gets applied on "All" or
        # future writes; we just don't fire the broadcast for single-zone ops.

        if zone == "all":
            # Write to left (arbitrary) — firmware propagates to all kbd zones.
            ok, err = _write(led_path("left", "brightness"), str(bright))
            if not ok:
                errors.append(err)
            else:
                for z in ("left", "middle", "right"):
                    self._state.zones[z].brightness = bright

            if not errors:
                ok, err = _write(led_path("corners", "brightness"), str(bright))
                if not ok:
                    errors.append(err)
                else:
                    self._state.zones["corners"].brightness = bright

        elif zone == "corners":
            ok, err = _write(led_path("corners", "brightness"), str(bright))
            if not ok:
                errors.append(err)
            else:
                self._state.zones["corners"].brightness = bright

        # else: single kbd zone — skip brightness write (see comment above)

        if errors:
            panel.set_status("✗ " + errors[0].split("\n")[0], ok=False)
            self._show_perm_warning(errors[0])
        else:
            zone_label = "All Zones" if zone == "all" else zone.capitalize()
            brightness_note = ""
            if zone in ("left", "middle", "right"):
                brightness_note = "  (brightness: use All Zones to change)"
            panel.set_status(
                f"✓ Applied to {zone_label}: {mode} mode, #{color}, "
                f"brightness {bright}{brightness_note}"
            )

    def _show_perm_warning(self, msg: str) -> None:
        try:
            bar = self.query_one("#perm-warning", Static)
            bar.update(f"⚠  {msg.split(chr(10))[0]}")
            bar.add_class("-visible")
        except NoMatches:
            pass

    def _show_warning(self, msg: str) -> None:
        self._show_perm_warning(msg)

    # ── Key actions ───────────────────────────────────────────────────────────

    def action_tab_dashboard(self) -> None:
        try:
            self.query_one(TabbedContent).active = "tab-dashboard"
        except NoMatches:
            pass

    def action_tab_lighting(self) -> None:
        try:
            self.query_one(TabbedContent).active = "tab-lighting"
        except NoMatches:
            pass

    def action_tab_power(self) -> None:
        try:
            self.query_one(TabbedContent).active = "tab-power"
        except NoMatches:
            pass

    def action_refresh_now(self) -> None:
        self._tick_fans()
        self._load_power_plan()


# ─────────────────────────────────────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────────────────────────────────────

def check_driver() -> None:
    """Warn early if the driver is not loaded at all."""
    led_glob = glob.glob(f"{LED_BASE}/excalibur::kbd_backlight-*")
    hwmon = find_hwmon_path()
    if not led_glob and not hwmon:
        print(
            "\033[93m⚠  WARNING: excalibur-wmi driver does not appear to be loaded.\033[0m\n"
            "   LED sysfs nodes and hwmon device were not found.\n"
            "   The TUI will still launch but controls will have no effect.\n"
            "   Load the driver first:\n"
            "     sudo modprobe excalibur\n"
            "   or verify with:\n"
            "     lsmod | grep excalibur\n",
            file=sys.stderr,
        )


if __name__ == "__main__":
    check_driver()
    app = ExcaliburApp()
    app.run()
