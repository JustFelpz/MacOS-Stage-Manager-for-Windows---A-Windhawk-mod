# macOS Stage Manager for Windows — a Windhawk mod

A [Windhawk](https://windhawk.net) mod inspired by Stage Manager on macOS.
The active window sits centered on the screen, and the other windows become
clickable previews in a column on the left.

## Features

- **Stage + column:** the active window is centered; the others are minimized
  and shown as tilted previews (with the app icon) on the left.
- **Click to switch:** clicking a preview puts that window on stage and sends
  the current one to the column.
- **Animations:** Overlay, Genie or Shrink (or none), GPU-accelerated with a
  CPU fallback.
- **Two windows on stage:** Shift+click a preview to add it next to the current
  window. Drag the handle between them to resize both, double-click it to swap
  them, Ctrl+Shift+click one to send it back to the column.
- **Per virtual desktop:** each desktop has its own Stage Manager state.
- **Toggle** with a keyboard shortcut (`Ctrl+Win` by default) or a four-finger
  sideways swipe on a precision touchpad.

Turning it off puts every window back where it was.

## Install

1. Install [Windhawk](https://windhawk.net).
2. In Windhawk, create a new mod (**Create a new mod**) and replace its code
   with the contents of [`stage-manager.wh.cpp`](stage-manager.wh.cpp).
3. Compile and enable the mod.

## Settings

| Setting | Description |
| --- | --- |
| Keyboard shortcut | Default `Ctrl+Win`. Modifiers only (fires on release) or modifiers + key. Empty disables it. |
| Touchpad gesture | Four-finger swipe left/right, or disabled. Set the Windows four-finger gestures to "Nothing" to avoid also switching desktops. |
| Swipe distance | Percentage of the touchpad width needed to trigger the gesture. |
| Preview width / Icon size / Max previews | Look and size of the column. |
| Size of the centered window | Fixed size in inches, keep the window's size, or fill the free area. |
| Switch animation / duration | Overlay, Genie, Shrink or none, and how long it takes. |

## Notes

- Windows minimized before Stage Manager was turned on are left alone.
- Windows of programs running as administrator can only be managed if Windhawk
  is running as administrator too.
- Maximizing a stage window, or making it full screen, turns Stage Manager off.

## License

[MIT](LICENSE)
