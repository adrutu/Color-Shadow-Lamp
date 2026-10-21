// Host tests for the lamp's RGB G-code player (../rgb_gcode.h), with stub ESPHome headers
// in ./esphome. Run on any machine with g++, from this folder:
//   g++ -std=gnu++20 -Wall -Wextra -g -fsanitize=address,undefined -I. -I.. test.cpp -o t && ./t
#include "rgb_gcode.h"
#include <cassert>
#include <cstdio>
namespace esphome { uint32_t fake_now = 0; uint32_t rng_state = 12345; }
using namespace rgb_gcode;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)
static bool near(float a, float b, float tol = 0.01f) { return fabsf(a - b) <= tol; }

struct Run {  // drive a Player directly
  Program p; Player pl; float out[3]; int scene = -1;
  explicit Run(const char *src, float r = 0, float g = 0, float b = 0) {
    std::string e = p.load(src); CHECK(e.empty(), "load: %s", e.c_str());
    float st[3] = {r, g, b}; scene = pl.tick(p, 0, true, st, out);
  }
  void at(uint32_t t) { float st[3] = {0, 0, 0}; scene = pl.tick(p, t, false, st, out); }
  bool is(float r, float g, float b, float tol = 0.01f) { return near(out[0], r, tol) && near(out[1], g, tol) && near(out[2], b, tol); }
};

int main() {
  // --- parser errors, each with a line number and a plain reason
  struct { const char *src; const char *want; } bad[] = {
    {"G1 R255\nG9 R1", "line 2: unknown command G9"},
    {"G1 A90", "line 1: G1 doesn't take A"},
    {"G1 R300", "line 1: R needs a value 0-255"},
    {"G1 R?100-300", "line 1: random range must be within 0-255"},
    {"G1 R?10", "line 1: random range should look like R?40-255"},
    {"M808 L2\nG1 R1", "line 1: M808 L loop is never closed"},
    {"G1 R1\nM808", "line 2: M808 end without a matching M808 L<count>"},
    {"G1 E5", "line 1: E must be 0-3"},
    {"M220", "line 1: M220 needs S1-S1000 (percent)"},
    {"; only a comment\n\n", "program is empty"},
    {"hello", "line 1: expected a command like G1 or M808"},
  };
  for (auto &c : bad) { Program p; std::string e = p.load(c.src); CHECK(e == c.want, "'%s' -> '%s' (want '%s')", c.src, e.c_str(), c.want); CHECK(p.gen == 0, "gen bumped on error"); }
  { Program p; p.load("G1 R10"); uint32_t g = p.gen; p.load("G9"); CHECK(p.gen == g && p.ops.size() == 1, "rejected load must keep old program"); }

  // --- the README example parses
  { Program p; std::string e = p.load("M117 Sea glass\nM808 L0\n  G1 R20 G200 B180 T4000 E1\n  G3 A90 T6000 E1\n  G2 A90 T6000 E1\nM808\n");
    CHECK(e.empty() && p.ops.size() == 6, "example: %s", e.c_str()); }
  // --- built-in show parses
  { Program &cs = colour_show(); CHECK(!cs.ops.empty() && cs.gen == 1, "built-in failed"); printf("Colour Show: %zu steps, %zu scenes\n", cs.ops.size(), cs.texts.size()); }

  // --- fades, holds, channel keep, G-letter green, U alias, comments, spacing-free words
  { Run r("G0 R0 G0 B0 ; start black\nG1R255T1000\nG4 T500\nG1 U255 B255 T1000 E0");
    CHECK(r.is(0, 0, 0), "t0 %f %f %f", r.out[0], r.out[1], r.out[2]);
    r.at(500);  CHECK(r.is(0.5, 0, 0), "t500 %f", r.out[0]);
    r.at(1000); CHECK(r.is(1, 0, 0), "t1000");
    r.at(1400); CHECK(r.is(1, 0, 0), "hold");
    r.at(2000); CHECK(r.is(1, 0.5, 0.5), "t2000 %f %f %f", r.out[0], r.out[1], r.out[2]);
    r.at(2500); CHECK(r.is(1, 1, 1), "end");
    r.at(99999); CHECK(r.is(1, 1, 1), "holds last colour after end"); }
  // --- easing midpoints
  { Run r("G1 R255 T1000 E1"); r.at(500); CHECK(near(r.out[0], 0.5), "E1 mid %f", r.out[0]); r.at(250); CHECK(near(r.out[0], 0.1464f), "E1 q %f", r.out[0]); }
  { Run r("G1 R255 T1000 E2"); r.at(500); CHECK(near(r.out[0], 0.75), "E2 mid %f", r.out[0]); }
  { Run r("G1 R255 T1000 E3"); r.at(500); CHECK(near(r.out[0], 0.25), "E3 mid %f", r.out[0]); }
  // --- seconds
  { Run r("G1 R255 S2"); r.at(1000); CHECK(near(r.out[0], 0.5), "S2 %f", r.out[0]); }
  // --- loops: 3 passes then continue; nested; forever
  { Run r("M808 L3\nG1 R255 T100\nG1 R0 T100\nM808\nG1 G255 T100");
    r.at(150); CHECK(r.is(0.5, 0, 0), "pass1 down %f", r.out[0]);
    r.at(450); CHECK(r.is(0.5, 0, 0), "pass3 up %f", r.out[0]);
    r.at(600); CHECK(r.is(0, 0, 0), "loop done");
    r.at(650); CHECK(r.is(0, 0.5, 0), "after loop %f", r.out[1]); }
  { Run r("M808 L2\nM808 L2\nG1 R255 T10\nG1 R0 T10\nM808\nG1 B255 T10\nG1 B0 T10\nM808\nG1 G255 T10");
    // inner 2x(20) + outer tail 20 = 60 per outer pass, x2 = 120, then G
    r.at(125); CHECK(r.is(0, 0.5, 0), "nested %f %f %f", r.out[0], r.out[1], r.out[2]); }
  { Run r("M808 L0\nG0 R255\nM808"); r.at(5000); CHECK(r.is(1, 0, 0), "timeless forever loop must not hang"); }
  { Run r("M808 L0\nG1 R255 T10\nG1 R0 T10\nM808");
    for (uint32_t t = 0; t < 600000; t += 7 + (t % 13)) r.at(t);  // 10 min of jittery ticks
    r.at(600005); CHECK(near(r.out[0], 0.5), "no drift after 10 min: %f", r.out[0]); }
  // --- hue arcs keep saturation/brightness
  { Run r("G3 A120 T1000", 1, 0, 0); r.at(500); CHECK(r.is(1, 1, 0), "G3 half %f %f %f", r.out[0], r.out[1], r.out[2]); r.at(1000); CHECK(r.is(0, 1, 0), "G3 end"); }
  { Run r("G2 A120 T1000", 1, 0, 0); r.at(1000); CHECK(r.is(0, 0, 1), "G2 end %f %f %f", r.out[0], r.out[1], r.out[2]); }
  { Run r("G3 T1000", 1, 0.5f, 0.2f); r.at(1000); CHECK(r.is(1, 0.5, 0.2), "full turn back home"); r.at(500);
    float mn = fminf(r.out[0], fminf(r.out[1], r.out[2])); CHECK(near(mn, 0.2) && near(fmaxf(r.out[0], fmaxf(r.out[1], r.out[2])), 1), "pastel keeps all LEDs on"); }
  // --- speed
  { Run r("M220 S200\nG1 R255 T1000"); r.at(500); CHECK(r.is(1, 0, 0), "200%% speed %f", r.out[0]); }
  // --- random stays in range, differs per pass
  { Run r("M808 L0\nG1 R?100-200 G? T10\nM808"); float lo = 9, hi = -9; int distinct = 0; float prev = -1;
    for (uint32_t t = 10; t < 2000; t += 10) { r.at(t); lo = fminf(lo, r.out[0]); hi = fmaxf(hi, r.out[0]); if (!near(r.out[0], prev, 0.0001f)) distinct++; prev = r.out[0]; }
    CHECK(lo >= 100 / 255.f - 1e-4 && hi <= 200 / 255.f + 1e-4, "range %f..%f", lo, hi); CHECK(distinct > 50, "randoms repeat"); }
  // --- scenes
  { Run r("M117 One\nG4 T100\nM117  Two words \nG4 T100"); CHECK(r.scene == 0 && r.p.texts[0] == "One", "scene 1");
    r.at(50); CHECK(r.scene == -1, "no repeat"); r.at(120); CHECK(r.scene == 1 && r.p.texts[1] == "Two words", "scene 2 '%s'", r.p.texts[1].c_str()); }
  // --- frame(): master brightness, dark phase, call flags, reload restarts from current colour
  { esphome::light::LightState ls; ls.remote_values.brightness = 0.5f; ls.current_values = ls.remote_values;
    Program p; p.load("G0 R255 G0 B0\nG4 T100\nG0 R0\nG4 T100\nG1 R255 G255 T100");
    esphome::fake_now = 0; const std::string *sc = frame(ls, true, p);
    CHECK(sc && sc->empty(), "start clears scene");
    CHECK(near(ls.current_values.brightness, 0.5) && near(ls.current_values.r, 1) && near(ls.current_values.g, 0), "red at master 50%%");
    esphome::fake_now = 150; frame(ls, false, p); CHECK(near(ls.current_values.brightness, 0), "dark phase = brightness 0");
    esphome::fake_now = 250; frame(ls, false, p); CHECK(near(ls.current_values.brightness, 0.25) && near(ls.current_values.r, 1) && near(ls.current_values.g, 1), "fade mid %f", ls.current_values.brightness);
    int calls = ls.calls; frame(ls, false, p); CHECK(ls.calls == calls, "no call when nothing changed");
    CHECK(ls.bad_calls == 0, "every call must be publish=false save=false transition=0");
    p.load("G4 T1000"); esphome::fake_now = 300; frame(ls, false, p); CHECK(near(ls.current_values.brightness, 0.25), "reload starts from the colour showing now %f", ls.current_values.brightness); }
  // --- the whole Colour Show: 2 loops at 16 ms ticks, all outputs valid, print the scene timeline
  { Run r(COLOUR_SHOW, 0.9f, 1, 0.96f); bool ok = true; int shown = 0;
    printf("  %6.1fs  %s\n", 0.0, r.p.texts[r.scene].c_str());
    for (uint32_t t = 16; t < 2 * 200000; t += 16) {
      r.at(t);
      for (float v : r.out) if (!(v >= -1e-4f && v <= 1.0001f)) ok = false;
      if (r.scene >= 0 && shown++ < 12) printf("  %6.1fs  %s\n", t / 1000.0, r.p.texts[r.scene].c_str());
    }
    CHECK(ok, "output out of 0..1"); }
  printf(fails ? "%d FAILED\n" : "ALL PASSED\n", fails);
  return fails != 0;
}
