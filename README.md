# madaWM

A minimal tiling window manager for X11 with 2 workspaces, allowing only terminal and browser windows.
This WM is designed to be lightweight with minimal dependencies and keybindings.

---

## Features

- 2 workspaces
- Only allows terminal (`kitty`, `xterm`, `urxvt`) and browser (`firefox`)
- Simple tiling: all windows on a workspace are evenly distributed
- Focus cycling with `Super + h/l`
- Workspace switching with `Super + 1/2`
- Spawn terminal: `Super + Enter`
- Spawn browser: `Super + b`
- Quit WM: `Super + c`

---

## Build

Make sure you have the X11 development libraries installed:

**On Arch/Manjaro:**
```bash
sudo pacman -S xorg-server xorg-x11-server-utils libx11 libx11-dev

## On The directory of the project run the following commands:
gcc -o madaWM madaWM.c -lX11

## then put this line in ~/.xinitrc file
exec /path/to/madaWM
