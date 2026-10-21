# How this got here

A write-up of the whole build: where it started, each step and why, where it is now, and what's
still open. I did this with Claude (Anthropic's AI coding assistant) over a few sessions in
September 2026; Claude wrote most of the code, I tested it on my lamp.

## Where it started

The lamp ships with rctestflight's own firmware. It's good on its own: the dials set the colour
(an RGB mode, and a brightness / colour temperature / tint mode), and a Wi-Fi mode makes the lamp
its own `Color_Shadow` hotspot with a colour-picker page. It caps total LED power at 30 % (60 % once
unlocked). What it doesn't do is join your home Wi-Fi, so there's no Home Assistant.

People were asking for exactly that
([rctestflight#3](https://github.com/rctestflight/Color-Shadow-Lamp/issues/3)), and two answers
already existed: [8bitben's fork](https://github.com/8bitben/Color-Shadow-Lamp) (stock firmware plus
MQTT) and [n2kbg's ESPHome config](https://github.com/n2kbg/ESPHome-ColorShadowRGB). I went with
n2kbg's, because ESPHome talks to Home Assistant natively (no MQTT broker) and updates over Wi-Fi.

## 1. First flash

n2kbg's config was right about the hardware: ESP32-C3, red/green/blue on GPIO7/6/5 as LEDC PWM
at 1220 Hz, dials on GPIO4/3/0, button on GPIO9. The button doubles as the C3's boot pin, which is
why the first flash needs the dance: hold the button, apply 12 V, plug in USB, upload. After that
every update went over Wi-Fi.

## 2. The dials fight Home Assistant

The first real problem. Setting the lamp to pure red from Home Assistant (`[255, 0, 0]`) read back
as `[255, 0, 249]` a second or two later. The blue dial was sitting near the top, and the C3's ADC
is noisy enough that a dial nobody touches still produces new readings. n2kbg's config passes every
reading to the light, so the dials kept taking the colour back.

Turning the dials down doesn't fix it either. The config maps each dial's bottom end to 12-15 %
(`calibrate_linear: 0.0 -> 0.12`, because the LEDs don't visibly light below that), so a dial at
zero still holds its channel at 12-15 % and keeps overwriting it.

I don't use the dials, so the fix was to delete the whole `sensor:` block. Dial position is now
irrelevant. Doing them properly would need the firmware to ignore a dial until it really moves,
and to set absolute values (n2kbg's README lists that one as a known bug too). That's still open.

## 3. Tidy-ups

- **Doubled names.** With the light's `name:` the same as the device's `friendly_name`, Home
  Assistant made `light.color_shadow_lamp_color_shadow_lamp`. `name: None` on the light gives
  `light.color_shadow_lamp`. (If you already have the doubled id, rename it in HA.)
- **No web server.** ESPHome's `web_server` was on with no password: anyone on the network could
  drive the lamp. Home Assistant doesn't need it, so it's gone.
- **Secrets out of the YAML.** The API key and the OTA and fallback-hotspot passwords moved to
  `!secret`, with new values. Two things I learned doing that:
  - To change the OTA password, compile with the new password, then upload that build using a copy
    of the old YAML (`esphome upload old.yaml --file firmware.ota.bin`), because the upload has to
    log in with the password the lamp still has.
  - After a new API key, Home Assistant doesn't ask for it by itself. Use **Reconfigure** on the
    ESPHome entry and it picks the new key up.

## 4. Disco

The first show: an on-lamp ESPHome `lambda` effect, so the beat stays tight and Home Assistant sees
one state change rather than one per flash. 125 BPM, a 4-bar phrase of 7.7 seconds:

- bars 1-2: one LED per beat, hit then fade, so the coloured shadows jump from LED to LED
- bar 3: two-LED colours (yellow, cyan, magenta) on the eighth notes
- bar 4: a random colour every sixteenth, then two beats of strobe

Every other phrase the chase runs backwards and the strobe uses single LEDs instead of white.

ESPHome details that mattered, and that the show player below reuses:

- A dark moment is brightness 0 with the light still **on**. Turning it off stops the effect.
- ESPHome normalises RGB (the brightest channel becomes 1.0), so dimming has to go through
  brightness, not smaller RGB numbers.
- Every frame's light call sets `transition_length(0)`, `publish(false)` and `save(false)`:
  no fades fighting the effect, no flood of state updates to Home Assistant, no flash wear.

## 5. A show language: RGB G-code

Writing every show in C++ and reflashing doesn't scale, so the next step was a tiny language that
Home Assistant can send at runtime. It borrows the shape of 3D-printer G-code: one command per
line, short letters for parameters, easy to type into an action. The full reference is in the
[README](../README.md#rgb-g-code) and at the top of [rgb_gcode.h](../rgb_gcode.h).

Choices made on the way:

- **A bad program never breaks the lamp.** It's parsed in full before anything plays; a mistake
  is refused with the line number and a plain reason (`line 2: unknown command G9`), and the old
  show keeps going.
- **No drift.** Each step starts exactly when the last one should have ended, not when the code
  happened to notice. Tested over ten simulated minutes of irregular ticks.
- **The light's brightness is the master.** Dim the lamp and the show dims with it.
- **A (re)started show begins from the colour on the lamp right now**, so the first fade is smooth.
- **Safety limits**: 16 KB, 4000 steps, 1 ms to 1 hour per step, and a loop with nothing timed in
  it can't hang the lamp.
- **Scene names** (`M117`) show up as a sensor, so a dashboard can say what's playing.

The built-in **Colour Show** (about three minutes, then it loops) is written in the same language:
white light, then each LED steps out in turn, a shadow dance, a rainbow, opposites, a sunset, an
aurora and a candy section. It keeps all three LEDs partly on as much as it can, because then every
shadow has its own colour.

It was checked two ways: host tests with stub ESPHome headers (in [tests/](../tests), run under
AddressSanitizer and UBSan), and on the lamp itself with a camera pointed at the wall, where the
wall's colour followed the program to the second.

## 6. Home Assistant switches

Two template switches, **Disco Mode** and **Colour Show**
([package](../home-assistant/color_shadow_lamp_shows.yaml)), for dashboards and voice. The useful
part is turning them off: the lamp goes back to how it was (off if it was off, otherwise its
previous colour, which the lamp keeps while a show runs). Going straight from one show to the
other keeps that memory.

## Where it is now

- Running on my lamp since September 2026, on ESPHome 2026.9.1.
- Effects: None, Pulse, Random, Flicker, Colour Show, Custom, Disco.
- Shows can be sent from Home Assistant at any time; the Scene and Show program sensors report back.
- Updates over Wi-Fi, encrypted API, no open web page, secrets out of the config.
- This folder is that config with my own names swapped for generic ones. It compiles and the tests
  pass.

## What's still open

Known gaps and ideas, not promises:

- **Power cap.** The stock firmware's 30 %/60 % total-power limit isn't here (see the README's
  heat note). A cap on the sum of the three channels, like stock, would be the proper fix.
- **Colour balance.** Stock trims red to 95 % and blue to 40 %, so its white looks different.
  ESPHome's `max_power:` on each output could do the same. Untested.
- **Dials.** Back in, but only reacting to real movement (see step 2).
- **Back on after an update.** The lamp comes back off after an OTA reboot. A `restore_mode` on the
  light should fix that; not tried yet.
- **Button.** It cycles the four original effects only; Disco and Colour Show aren't on it. And a
  double-click also moves the effect on by one (inherited from n2kbg's config).
- **Upstream.** Parts of this could go back to n2kbg's repo as pull requests, if wanted.
