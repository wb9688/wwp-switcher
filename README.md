# wWP Switcher

wWP Switcher is a classic switcher plugin for Wayfire.

## Screenshot

![Screenshot](https://github.com/user-attachments/assets/493099cd-1939-4730-8ca4-65bbb77d3ec6)

## Installation

```sh
meson setup build # optionally specify prefix with --prefix=/some/path
ninja -C build install
```

## Configuration

Add `wwp-switcher` to `plugins` in the `core` section of `~/.config/wayfire.ini` to enable wWP Switcher.

The default config for wWP Switcher is as follows:

```ini
[wwp-switcher]
activate = <super> KEY_TAB
background_color = \#000000DD
text_color = \#FFFFFFFF
font = DejaVu Sans 12
icon_theme = Papirus-Dark
```

You can put that into your `~/.config/wayfire.ini` and adjust it however you want.

## FAQ

### What does wWP stand for?

wWP stands for wb9688's Wayfire Plugins.

### Why do the icons not display as expected?

First, please check whether you have set `icon_theme` to the correct icon theme, which you have installed.

wWP Switcher currently does not implement the full freedesktop.org Icon Theme Specification yet, so it might also be possible that wWP Switcher does not work well with your icon theme. It is known that with e.g. Adwaita it will show a generic application icon for almost all applications.
