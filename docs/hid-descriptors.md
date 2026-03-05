# HID Descriptor Reference

## Keyboard Descriptor (Report ID 0x01)

### Input Report Layout (8 bytes)

| Byte | Bits  | Field       | Description |
|------|-------|-------------|-------------|
| 0    | 0-7   | Modifiers   | LCtrl(0), LShift(1), LAlt(2), LGUI(3), RCtrl(4), RShift(5), RAlt(6), RGUI(7) |
| 1    | 0-7   | Reserved    | Always 0x00 |
| 2    | 0-7   | Key[0]      | HID keycode (0x00=none) |
| 3    | 0-7   | Key[1]      | HID keycode |
| 4    | 0-7   | Key[2]      | HID keycode |
| 5    | 0-7   | Key[3]      | HID keycode |
| 6    | 0-7   | Key[4]      | HID keycode |
| 7    | 0-7   | Key[5]      | HID keycode |

### Output Report Layout (1 byte)

| Byte | Bits  | Field       | Description |
|------|-------|-------------|-------------|
| 0    | 0     | Num Lock    | LED state |
| 0    | 1     | Caps Lock   | LED state |
| 0    | 2     | Scroll Lock | LED state |
| 0    | 3     | Compose     | LED state |
| 0    | 4     | Kana        | LED state |
| 0    | 5-7   | Padding     | Unused |

## Mouse Descriptor (Report ID 0x02)

### Input Report Layout (7 bytes)

| Byte  | Bits  | Field   | Description |
|-------|-------|---------|-------------|
| 0     | 0     | Left    | Button state |
| 0     | 1     | Right   | Button state |
| 0     | 2     | Middle  | Button state |
| 0     | 3     | X1      | Button state |
| 0     | 4     | X2      | Button state |
| 0     | 5-7   | Padding | Unused |
| 1-2   | 0-15  | X       | 16-bit signed relative |
| 3-4   | 0-15  | Y       | 16-bit signed relative |
| 5     | 0-7   | Wheel   | 8-bit signed scroll |
| 6     | 0-7   | AC Pan  | 8-bit signed h-scroll |

## Common HID Keycodes

| Key | HID Code | Key | HID Code |
|-----|----------|-----|----------|
| A   | 0x04     | 1   | 0x1E     |
| B   | 0x05     | 2   | 0x1F     |
| Z   | 0x1D     | 0   | 0x27     |
| Enter | 0x28   | Esc | 0x29     |
| Space | 0x2C   | Tab | 0x2B     |
| F1  | 0x3A     | F12 | 0x45     |
