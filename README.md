# ACERCTRL
Control RGB and various hardware functions of newer Acer laptops on Linux

## Supported Features
- RGB Control
- Keyboard Backlight Timeout
- Battery charging limit
- Changing power profile via mode button

## How to Build/Install
On Ubuntu/PopOS you'll need these first: `sudo apt install -y cmake ninja-build clang clang-tools-18 libstdc++-14-dev qt6-base-dev` (needs Clang, not GCC — the build uses C++23 modules).\
Run `make` to build, then run `sudo make install` to install it. It will install to `/usr/local/bin` and `/usr/local/lib` by default.\
*Note: the following instructions are for systemd-based systems. If you are not using systemd, figure it out. I believe in you.*\
Run `systemctl enable --now acerctrld.service` to start and enable the service at boot.\
If you have a laptop that doesn't turn RGB off during suspend, copy `acer-sleep.sh` to `/usr/lib/systemd/system-sleep/`

## How to Uninstall
Stop the `acerctrld` daemon however it is done on your system. On systemd-based distros run `systemctl disable --now acerctrld.service`\
Run `sudo make uninstall`\
This should remove both the executable files and residual config.

## How to Use
You can use `acerctrl-systray` for a system tray widget which provides a GUI.\
`acerctrl-cli` can be used as follows:
- To set RGB: `acerctrl-cli set-rgb --device <device_name> --effect <effect> --brightness <brightness> --rgb <red> <green> <blue>`
- To set the keyboard timeout: `acerctrl-cli set-timeout <timeout>`
- To set the battery charging limit: `acerctrl-cli set-battery-limit --lower-limit <lower_limit> --upper-limit <upper_limit>`
- For more information, run `acerctrl-cli --help`

## Components
- libacerhidrgb: C++ module for controlling RGB
- libacerhidhw: C++ module for controlling hardware features
- acerctrld: daemon for controlling RGB and/or hardware features
- acerctrl-cli: CLI interface for acerctrld
- acerctrl-systray: System tray GUI for interfacing with acerctrld

## Supported Devices
| Device Name | RGB | Hardware Features |
|-------------|-----| ----------------- |
| PHN16S-71   | Yes | Yes               |
If your device works, let me know so I can add it!

*Created by ZoeBattleSand with help from the great people [here at linuwu-sense](https://github.com/0x7375646F/Linuwu-Sense/pull/65)*\
*This project is not affiliated with Acer in any way!*
