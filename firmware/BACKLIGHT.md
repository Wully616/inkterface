# LCD-5B backlight power

The LCD-5B firmware provides a binary backlight control through the CH422G
expander's `EXIO2` output. Inkterface exposes this as an **On/Off** switch.
The backlight defaults to on after the first frame is presented.

There is no PWM or brightness control in the firmware. Leave the board's
`PWM` pad unconnected; do not connect it to `485_A` or another signal.
