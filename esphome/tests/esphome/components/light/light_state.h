#pragma once
// Minimal stand-in for ESPHome's LightState: enough for rgb_gcode::frame().
namespace esphome { namespace light {
struct LightColorValues {
  float state = 1, brightness = 1, r = 1, g = 1, b = 1;
  float get_brightness() const { return brightness; }
  void as_rgb(float *red, float *green, float *blue) const { *red = state * brightness * r; *green = state * brightness * g; *blue = state * brightness * b; }
};
struct LightState;
struct LightCall {
  LightState *s; float r = -1, g = -1, b = -1, br = -1; bool pub = true, save = true; unsigned trans = 999;
  LightCall &set_rgb(float a, float c, float d) { r = a; g = c; b = d; return *this; }
  LightCall &set_brightness(float x) { br = x; return *this; }
  LightCall &set_transition_length(unsigned t) { trans = t; return *this; }
  LightCall &set_publish(bool p) { pub = p; return *this; }
  LightCall &set_save(bool v) { save = v; return *this; }
  void perform();
};
struct LightState {
  LightColorValues remote_values, current_values; int calls = 0, bad_calls = 0;
  LightCall turn_on() { return LightCall{this}; }
};
inline void LightCall::perform() {
  s->calls++;
  if (pub || save || trans != 0) s->bad_calls++;
  if (r >= 0) { s->current_values.r = r; s->current_values.g = g; s->current_values.b = b; }
  if (br >= 0) s->current_values.brightness = br;
}
}}
