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
background_color = \#2F343FFF
text_color = \#D3DAE3FF
font = Roboto 12
icon_theme = Papirus-Dark
```

## FAQ

### What does wWP stand for?

wWP stands for wb9688's Wayfire Plugins.
