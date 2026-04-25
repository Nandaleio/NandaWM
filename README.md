# NandaWM

A minimal Wayland compositor written in C, built on [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) 0.18.

NandaWM follows a maximizing window manager philosophy: every application window fills the entire screen. Windows are stacked and cycled through with a keyboard shortcut, keeping the interface distraction-free and keyboard-driven.

## Features

- **Full-screen layout** — every window is automatically maximized to fill the output
- **Window cycling** — cycle through open windows with `Super+Tab`
- **Layer shell support** — panels, docks, wallpapers, and overlays via `wlr-layer-shell-unstable-v1`
- **Popup rendering** — context menus and dropdowns for both XDG and layer shell surfaces
- **Multi-monitor support** — outputs are managed with automatic layout
- **Clipboard** — selection forwarding via `wlr_data_device`
- **XCursor theming** — standard cursor theme support
- **App launcher** — launches [fuzzel](https://codeberg.org/dnkl/fuzzel) on demand
- **Auto-start** — spawns [Alacritty](https://github.com/alacritty/alacritty) on compositor start

## Keybindings

| Shortcut | Action |
|---|---|
| `Super + Tab` | Cycle through open windows |
| `Alt + F4` | Close the focused window |
| `Super + F` or `Super + Enter` | Open the fuzzel app launcher |
| `Ctrl + Alt + Q` | Quit the compositor |

## Tech Stack

| Component | Technology |
|---|---|
| Language | C11 |
| Compositor toolkit | [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) 0.18 |
| Display protocol | [Wayland](https://wayland.freedesktop.org/) (`wayland-server`) |
| Keyboard handling | [xkbcommon](https://xkbcommon.org/) |
| Build system | [Meson](https://mesonbuild.com/) (also supports Make) |
| Protocol generation | `wayland-scanner` |

**Wayland protocols implemented:**
- `xdg-shell` (stable) — standard window protocol used by modern Wayland apps
- `wlr-layer-shell-unstable-v1` — protocol for panels, docks, and overlays

## Architecture

```
src/
├── nanda.h      # Core structs and function declarations
├── main.c       # Server init, event loop, scene graph setup
├── output.c     # Monitor/display management
├── input.c      # Keyboard and pointer event handling
├── view.c       # XDG toplevel window lifecycle and focus
└── layer.c      # Layer shell surface management
```

The scene graph (`wlr_scene`) handles compositing with a fixed z-order:

```
BACKGROUND → BOTTOM → shell windows → TOP → OVERLAY
```

## Dependencies

- `wlroots-0.18`
- `wayland-server`
- `xkbcommon`
- `wayland-scanner` (build-time)

## Build

### With Meson

```sh
meson setup build
ninja -C build
```

### With Make

```sh
make
```

The Make build expects:
- Wayland protocols at `/usr/share/wayland-protocols`
- wlroots protocols at `~/Workspace/wlroots/protocol`

Adjust the paths in `Makefile` if needed.

## Running

```sh
./nandawm
```

The compositor sets `WAYLAND_DISPLAY` automatically and logs the socket name on startup. Run it from a TTY or nest it inside an existing Wayland session for testing.

## Keyboard Layout

The keyboard is configured for a French layout (`fr`, `latin9` variant, `pc105` model). Edit `create_keyboard()` in `src/input.c` to change the layout.
