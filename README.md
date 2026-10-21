# Color Shadow Lamp: fork with an ESPHome / Home Assistant version

This is a fork of [rctestflight/Color-Shadow-Lamp](https://github.com/rctestflight/Color-Shadow-Lamp).
rctestflight's stock firmware is unchanged; the original README follows below.

**New in this fork: [`esphome/`](esphome/)**, ESPHome firmware that joins your own Wi-Fi, shows up in
Home Assistant as a normal RGB light, and adds light shows that run on the lamp itself: Disco, a
built-in Colour Show, and your own shows sent from Home Assistant in a small G-code-style language.
Built on [n2kbg's ESPHome config](https://github.com/n2kbg/ESPHome-ColorShadowRGB), made with Claude
(Anthropic's AI assistant) and tested on my own lamp.

- How to install and use it: [esphome/README.md](esphome/README.md)
- How it got here, and what's still open: [esphome/docs/story.md](esphome/docs/story.md)

Licensing: the `esphome/` folder is GPL-3.0 (see [esphome/LICENSE](esphome/LICENSE)); the rest is
rctestflight's original code.

---

## Original README

Firmware Update Instructional Video: https://youtu.be/KALXqIxTB14

To upload code to blank ESP32-C3 via USB: 
1. Hold the button down
2. Apply 12V
3. Plug in USB
4. Upload code
5. You can release the button once the code starts uploading 
6. Power cycle
