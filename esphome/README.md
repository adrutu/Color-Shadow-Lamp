# Color Shadow Lamp: ESPHome firmware with light shows

ESPHome firmware for the [rctestflight Color Shadow Lamp](https://www.rctestflight.com/store/p/color-shadow-lamp).
The lamp joins your own Wi-Fi, shows up in Home Assistant as a normal RGB light, and gets light
shows that run on the lamp itself:

- **Disco**: a 125 BPM beat that hops between the three LEDs, so the coloured shadows jump around.
- **Colour Show**: a built-in tour of about three minutes that loops forever.
- **Custom**: your own show, sent from Home Assistant in a small G-code-style language. No reflash needed.

It lives in the `esphome/` folder of my fork of
[rctestflight's repo](https://github.com/rctestflight/Color-Shadow-Lamp); the rest of the fork is the
stock firmware, unchanged. It's built on
[n2kbg's ESPHome config](https://github.com/n2kbg/ESPHome-ColorShadowRGB), which did the hard part
(pins, button, first effects). This folder adds the shows, a few fixes and a write-up of how it got
here: [docs/story.md](docs/story.md).

> **Made with AI, tested on a real lamp.** I built this with Claude (Anthropic's AI coding assistant).
> Claude wrote most of the code and these docs; I ran it on my own lamp, tested it and decided what
> went in. It has been running on my lamp since September 2026. Issues and pull requests are welcome.

## What you get

| | |
|---|---|
| Home Assistant light | `light.color_shadow_lamp`: colour, brightness, on/off, effects |
| Effects | None, Pulse, Random, Flicker (from n2kbg), **Colour Show**, **Custom**, **Disco** |
| Play a show from HA | action `esphome.color_shadow_lamp_play` with the show as text, plays as effect "Custom" |
| Sensors | **Scene**: the name of the part of the show that's playing. **Show program** (diagnostic): `Custom playing, 12 steps`, or `Rejected, line 3: G1 doesn't take A` |
| Button | single press: None, Pulse, Random, Flicker in turn. Double-click: on/off |
| Updates | over Wi-Fi (OTA) after the first USB flash |
| Optional HA package | **Disco Mode** and **Colour Show** switches that put the lamp back the way it was when you turn them off ([home-assistant/](home-assistant/color_shadow_lamp_shows.yaml)) |

## Read this before you flash

**Heat and power.** The stock firmware caps total LED power at 30 %, or 60 % once unlocked.
rctestflight [explained why](https://github.com/rctestflight/Color-Shadow-Lamp/issues/2): each LED arm
has a thermal switch that turns that LED off at about 63 °C, so you can't easily damage it, but at
100 % the switch trips much sooner and LEDs start switching off by themselves. **This firmware has no
power cap**, same as n2kbg's. Full white at full brightness is the hot case; a single colour is far
less. If you'll run it bright for long, keep the brightness down, or add `max_power:` to the three
`ledc` outputs (I haven't tried that, so test it yourself).

**The dials do nothing.** The ESP32-C3's ADC is noisy, so a dial that isn't moving still sends
changes, and those overwrote every colour set from Home Assistant within 1-2 seconds. I don't use
the dials, so I took them out. Details, and why turning the dials down doesn't help, are in
[docs/story.md](docs/story.md#2-the-dials-fight-home-assistant). If you want them back, n2kbg's
`sensor:` block is the starting point, but expect the same fight.

**It replaces the stock firmware.** You lose the stock knob modes and the lamp's own
`Color_Shadow` hotspot page. To go back, flash
the stock firmware (the root of this fork, or
[rctestflight's original](https://github.com/rctestflight/Color-Shadow-Lamp)) the same way (USB).

## Install

1. Install ESPHome: the **ESPHome Device Builder** add-on in Home Assistant, or `pip install esphome`.
2. Copy `color-shadow-lamp.yaml` and `rgb_gcode.h` from this folder into your ESPHome config folder,
   side by side.
3. Copy `secrets.example.yaml` to `secrets.yaml` and fill it in (with the add-on, add the keys to
   the `secrets.yaml` you already have). Generate the API key with `openssl rand -base64 32`.
4. First flash over USB. The button is on GPIO9, which is also the C3's boot pin, so
   ([rctestflight's steps](https://github.com/rctestflight/Color-Shadow-Lamp#readme)):
   1. Hold the button down
   2. Apply 12 V
   3. Plug in USB
   4. Start the upload: `esphome run color-shadow-lamp.yaml` and pick the USB port
   5. Let go of the button once the upload starts
   6. Power-cycle the lamp
5. Home Assistant finds it under **Settings > Devices & services**. Add it and paste the API key.
6. From now on, updates go over Wi-Fi: `esphome run color-shadow-lamp.yaml`.

After an update the lamp has come back **off** for me (ESPHome's default for lights), so turn it
back on afterwards. If it can't join your Wi-Fi it starts a `Color Shadow Lamp Fallback` hotspot.

## Using it

Turn on a built-in show:

```yaml
action: light.turn_on
target:
  entity_id: light.color_shadow_lamp
data:
  effect: Colour Show   # or Disco
```

Play your own show:

```yaml
action: esphome.color_shadow_lamp_play
data:
  program: |
    M117 Sea glass
    M808 L0
      G1 R20 G200 B180 T4000 E1
      G3 A90 T6000 E1
      G2 A90 T6000 E1
    M808
```

Sending a new show restarts it. A show with a mistake in it is refused, the lamp carries on with
what it was doing, and the **Show program** sensor says which line is wrong and why. The light's
brightness scales every show, so a dim lamp plays a dim show. To stop a show, pick effect `None`
(or turn the lamp off).

### RGB G-code

One command per line. `;` starts a comment. Channels are 0-255, times are milliseconds.

| Command | What it does |
|---|---|
| `G0 R255 G80 B0` | Jump to a colour. Channels you leave out keep their value. (`U` works for green too.) |
| `G1 R0 G0 B255 T2000 E1` | Fade there over 2000 ms. Easing: `E0` linear (default), `E1` smooth, `E2` fast then slow, `E3` slow then fast |
| `G1 R? G?40-255 B?` | `?` = a new random value each time the line runs. `?a-b` = random within a range |
| `G3 A360 T12000` | Hue arc forwards (red, yellow, green, ...) keeping saturation and brightness. `G2` goes backwards. `A` = degrees, default 360 |
| `G4 T1500` | Hold. Any time can be `T<ms>` or `S<seconds>`. Default 1000 ms |
| `M808 L4` ... `M808` | Repeat the lines in between 4 times (`L0` = forever). Loops can nest |
| `M220 S150` | Play everything after this at 150 % speed |
| `M117 Sunset` | Scene name, shown in HA as the **Scene** sensor |

A show that ends holds its last colour. Limits: 16 KB of text, 4000 steps, 1 ms to 1 hour per step.
Timing doesn't drift: each step starts exactly where the last one ended, so a show loops in time
for hours.

**Tip for this lamp:** the three LEDs sit apart, so each shadow is lit by the other two. Keeping all
three partly on (pastels, `G?40-255`) gives every shadow its own colour; fading one LED out makes
its shadows turn the colour of the other two mixed. The built-in Colour Show in
[rgb_gcode.h](rgb_gcode.h) is a worked example.

## Home Assistant switches (optional)

[home-assistant/color_shadow_lamp_shows.yaml](home-assistant/color_shadow_lamp_shows.yaml) is a
package with two switches, **Disco Mode** and **Colour Show**. Turning one off puts the lamp back
how it was before: off if it was off, otherwise its previous colour. Swapping straight from one show
to the other keeps that memory. Expose them to Assist and they work by voice too ("turn on colour show").
Install notes are at the top of the file.

## Tests

The show player has host tests (no lamp needed): parser errors, fades, easing, loops, hue arcs,
random ranges, scene names, ten minutes of jittery ticks without drift, and the full Colour Show.

```sh
cd esphome/tests
g++ -std=gnu++20 -Wall -Wextra -g -fsanitize=address,undefined -I. -I.. test.cpp -o t && ./t
```

Beyond that, on my lamp: the shows were checked against a camera pointed at the wall (the wall
colour followed the program to the second), the switches and voice commands were tried from Home
Assistant, and it has run day to day since. This public copy only renames things from my setup
(device name, entity ids); it compiles with ESPHome 2026.9.1 and the tests pass.

## Hardware notes

| | |
|---|---|
| Board | ESP32-C3 on rctestflight's own PCB (`esp32-c3-devkitm-1`, ESP-IDF) |
| LEDs | analog RGB, not addressable (so WLED doesn't fit), 12 V through MOSFETs |
| Red / Green / Blue | GPIO7 / GPIO6 / GPIO5, LEDC PWM at 1220 Hz |
| Dials | ADC on GPIO4 / GPIO3 / GPIO0 (unused here) |
| Button | GPIO9, also the boot pin |

## Credits

- [rctestflight](https://github.com/rctestflight/Color-Shadow-Lamp): the lamp, the stock firmware (the rest of this fork) and the flashing steps.
- [n2kbg/ESPHome-ColorShadowRGB](https://github.com/n2kbg/ESPHome-ColorShadowRGB): the ESPHome config this is built on.
- [8bitben/Color-Shadow-Lamp](https://github.com/8bitben/Color-Shadow-Lamp): MQTT firmware for Home Assistant, and the question that got the power limits explained.
- Claude (Anthropic): co-wrote the code and the docs.

## License

Everything in this `esphome/` folder is GPL-3.0, the same as n2kbg's config that it's built on. See
[LICENSE](LICENSE). The rest of the fork is rctestflight's original code, unchanged.
