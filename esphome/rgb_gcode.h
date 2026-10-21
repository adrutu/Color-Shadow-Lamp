#pragma once
// RGB G-code - a small G-code-style show player for the rctestflight Color Shadow Lamp
// (3 separate R/G/B LEDs). https://github.com/adrutu/Color-Shadow-Lamp/tree/main/esphome
//
// Built-in shows are lamp effects ("Colour Show"). HA can play a new one without a reflash:
//   action: esphome.color_shadow_lamp_play   data: {program: "<RGB G-code>"}   -> effect "Custom"
//
// One command per line, ';' starts a comment, channels are 0-255, times in ms:
//   G0 R255 G80 B0          jump to a colour; channels you leave out keep their value (U = G too)
//   G1 R0 G0 B255 T2000 E1  fade there over 2000 ms. Easing: E0 linear (default), E1 smooth,
//                           E2 fast-then-slow, E3 slow-then-fast
//   G1 R? G?40-255 B?       ? = a new random value each time the line runs, ?a-b = within a range
//   G3 A360 T12000          hue arc forwards (red > yellow > green > ...) keeping saturation and
//                           brightness; G2 goes backwards. A = degrees (default 360)
//   G4 T1500                hold. Any time can be T<ms> or S<seconds>; default 1000 ms
//   M808 L4 ... M808        repeat the lines in between 4 times (L0 = forever); loops nest
//   M220 S150               play everything after this at 150 % speed
//   M117 Sunset             scene name, shown in HA as the lamp's "Scene" sensor
// The light's normal brightness scales the whole show. A program that ends holds its last colour.

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "esphome/components/light/light_state.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace rgb_gcode {

enum Code : uint8_t { JUMP, FADE, ARC, HOLD, LOOP, END, SPEED, SCENE };

struct Chan {
  bool set = false;
  bool rnd = false;
  float v = 0, lo = 0, hi = 1;  // 0..1
};

struct Op {
  Code code = HOLD;
  Chan ch[3];
  float deg = 360;  // ARC, signed: + forwards (G3), - backwards (G2)
  uint32_t ms = 1000;
  uint8_t ease = 0;
  int32_t n = 0;     // LOOP: passes (0 = forever) | SPEED: percent | SCENE: text index
  int32_t pair = -1;  // LOOP <-> END
};

static const size_t MAX_SRC = 16384, MAX_OPS = 4000;

struct Program {
  std::vector<Op> ops;
  std::vector<std::string> texts;
  uint32_t gen = 0;  // bumped on every successful load, so a playing show restarts

  // "" when OK, else "line N: why". A rejected program leaves the old one in place.
  std::string load(const std::string &src);
};

inline std::string err_at(int line, const std::string &why) { return "line " + std::to_string(line) + ": " + why; }

inline bool read_num(const std::string &s, size_t &i, float &out) {
  size_t j = i;
  bool digits = false;
  while (j < s.size() && (isdigit((unsigned char) s[j]) || s[j] == '.')) {
    digits = digits || s[j] != '.';
    j++;
  }
  if (!digits)
    return false;
  out = strtof(s.substr(i, j - i).c_str(), nullptr);
  i = j;
  return true;
}

inline std::string Program::load(const std::string &src) {
  if (src.size() > MAX_SRC)
    return "program is longer than 16 KB";
  std::vector<Op> ops_;
  std::vector<std::string> texts_;
  std::vector<std::pair<int32_t, int>> open;  // (op index, line) of unclosed M808 L
  int line_no = 0;
  size_t pos = 0;
  while (pos < src.size()) {
    size_t nl = src.find('\n', pos);
    if (nl == std::string::npos)
      nl = src.size();
    std::string s = src.substr(pos, nl - pos);
    pos = nl + 1;
    line_no++;
    size_t semi = s.find(';');
    if (semi != std::string::npos)
      s.resize(semi);
    size_t i = 0;
    while (i < s.size() && isspace((unsigned char) s[i]))
      i++;
    if (i == s.size())
      continue;

    char letter = toupper((unsigned char) s[i++]);
    float num;
    if ((letter != 'G' && letter != 'M') || !read_num(s, i, num))
      return err_at(line_no, "expected a command like G1 or M808");
    int cmd = (int) num;
    Op op;
    std::string allowed;
    if (letter == 'G' && (cmd == 0 || cmd == 1)) {
      op.code = cmd == 0 ? JUMP : FADE;
      allowed = cmd == 0 ? "RGUB" : "RGUBTSE";
    } else if (letter == 'G' && (cmd == 2 || cmd == 3)) {
      op.code = ARC;
      allowed = "ATSE";
    } else if (letter == 'G' && cmd == 4) {
      op.code = HOLD;
      allowed = "TSP";
    } else if (letter == 'M' && cmd == 808) {
      op.code = END;  // becomes LOOP if it has an L
      allowed = "L";
    } else if (letter == 'M' && cmd == 220) {
      op.code = SPEED;
      op.n = -1;
      allowed = "S";
    } else if (letter == 'M' && cmd == 117) {
      std::string t = s.substr(i);
      size_t a = t.find_first_not_of(" \t\r"), b = t.find_last_not_of(" \t\r");
      op.code = SCENE;
      op.n = texts_.size();
      texts_.push_back(a == std::string::npos ? "" : t.substr(a, b - a + 1));
      ops_.push_back(op);
      continue;
    } else {
      return err_at(line_no, std::string("unknown command ") + letter + std::to_string(cmd));
    }

    bool has_l = false;
    while (i < s.size()) {
      if (isspace((unsigned char) s[i])) {
        i++;
        continue;
      }
      char p = toupper((unsigned char) s[i++]);
      if (allowed.find(p) == std::string::npos)
        return err_at(line_no, std::string(1, letter) + std::to_string(cmd) + " doesn't take " + p);
      if (p == 'R' || p == 'G' || p == 'U' || p == 'B') {
        Chan &c = op.ch[p == 'R' ? 0 : p == 'B' ? 2 : 1];
        c.set = true;
        if (i < s.size() && s[i] == '?') {
          i++;
          c.rnd = true;
          float lo = 0, hi = 255;
          if (read_num(s, i, lo)) {
            if (i >= s.size() || s[i] != '-' || !read_num(s, ++i, hi))
              return err_at(line_no, "random range should look like R?40-255");
          }
          if (lo > hi || hi > 255)
            return err_at(line_no, "random range must be within 0-255");
          c.lo = lo / 255.0f;
          c.hi = hi / 255.0f;
          continue;
        }
        if (!read_num(s, i, num) || num > 255)
          return err_at(line_no, std::string(1, p) + " needs a value 0-255");
        c.v = num / 255.0f;
        continue;
      }
      if (!read_num(s, i, num))
        return err_at(line_no, std::string(1, p) + " needs a number");
      switch (p) {
        case 'T':
        case 'P':
          op.ms = (uint32_t) num;
          break;
        case 'S':
          if (op.code == SPEED)
            op.n = (int32_t) num;
          else
            op.ms = (uint32_t) (num * 1000.0f);
          break;
        case 'E':
          if (num > 3)
            return err_at(line_no, "E must be 0-3");
          op.ease = (uint8_t) num;
          break;
        case 'A':
          op.deg = num;
          break;
        case 'L':
          op.n = (int32_t) num;
          has_l = true;
          break;
      }
    }
    if (op.ms < 1 || op.ms > 3600000)
      return err_at(line_no, "time must be 1 ms to 1 hour");
    if (op.code == ARC && cmd == 2)
      op.deg = -op.deg;
    if (op.code == SPEED && (op.n < 1 || op.n > 1000))
      return err_at(line_no, "M220 needs S1-S1000 (percent)");
    if (op.code == END && has_l) {
      op.code = LOOP;
      open.emplace_back(ops_.size(), line_no);
    } else if (op.code == END) {
      if (open.empty())
        return err_at(line_no, "M808 end without a matching M808 L<count>");
      op.pair = open.back().first;
      ops_[op.pair].pair = ops_.size();
      open.pop_back();
    }
    ops_.push_back(op);
    if (ops_.size() > MAX_OPS)
      return "program has more than 4000 steps";
  }
  if (!open.empty())
    return err_at(open.back().second, "M808 L loop is never closed");
  if (ops_.empty())
    return "program is empty";
  this->ops.swap(ops_);
  this->texts.swap(texts_);
  this->gen++;
  return "";
}

inline float ease(uint8_t e, float x) {
  switch (e) {
    case 1:
      return 0.5f - 0.5f * cosf(x * 3.14159265f);
    case 2:
      return 1.0f - (1.0f - x) * (1.0f - x);
    case 3:
      return x * x;
    default:
      return x;
  }
}

inline void rgb2hsv(const float c[3], float &h, float &s, float &v) {
  float mx = fmaxf(c[0], fmaxf(c[1], c[2])), mn = fminf(c[0], fminf(c[1], c[2])), d = mx - mn;
  v = mx;
  s = mx > 0 ? d / mx : 0;
  if (d <= 0) {
    h = 0;
  } else if (mx == c[0]) {
    h = 60.0f * fmodf((c[1] - c[2]) / d, 6.0f);
  } else if (mx == c[1]) {
    h = 60.0f * ((c[2] - c[0]) / d + 2.0f);
  } else {
    h = 60.0f * ((c[0] - c[1]) / d + 4.0f);
  }
  if (h < 0)
    h += 360.0f;
}

inline void hsv2rgb(float h, float s, float v, float c[3]) {
  h = fmodf(h, 360.0f);
  if (h < 0)
    h += 360.0f;
  float k = v * s, x = k * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f)), m = v - k;
  float r = 0, g = 0, b = 0;
  if (h < 60) {
    r = k, g = x;
  } else if (h < 120) {
    r = x, g = k;
  } else if (h < 180) {
    g = k, b = x;
  } else if (h < 240) {
    g = x, b = k;
  } else if (h < 300) {
    r = x, b = k;
  } else {
    r = k, b = x;
  }
  c[0] = r + m, c[1] = g + m, c[2] = b + m;
}

inline float pick(const Chan &c, float keep) {
  if (!c.set)
    return keep;
  return c.rnd ? c.lo + (c.hi - c.lo) * esphome::random_float() : c.v;
}

class Player {
 public:
  // Colour for `now` (0..1 per channel) into out. Returns a SCENE text index when one starts, else -1.
  int tick(const Program &p, uint32_t now, bool restart, const float start[3], float out[3]) {
    this->scene_ = -1;
    if (restart || this->prog_ != &p || this->gen_ != p.gen) {
      this->prog_ = &p;
      this->gen_ = p.gen;
      for (int i = 0; i < 3; i++)
        this->cur_[i] = start[i];
      this->pc_ = 0;
      this->speed_ = 1.0f;
      this->in_seg_ = false;
      this->left_.assign(p.ops.size(), -1);
      this->run_(p, now);
    }
    int guard = 0;
    while (this->in_seg_ && now - this->seg_t0_ >= this->seg_ms_) {
      this->colour_at_(1.0f, this->cur_);
      uint32_t t = this->seg_t0_ + this->seg_ms_;  // next step starts where this one ended: no drift
      this->in_seg_ = false;
      this->run_(p, t);
      if (++guard > 200) {  // far behind (or very short steps): pick up from now
        this->seg_t0_ = now;
        break;
      }
    }
    if (this->in_seg_) {
      this->colour_at_(ease(this->ease_, (float) (now - this->seg_t0_) / (float) this->seg_ms_), out);
    } else {
      for (int i = 0; i < 3; i++)
        out[i] = this->cur_[i];
    }
    return this->scene_;
  }

 protected:
  // Run zero-time steps until a timed step starts at time t (or the program ends).
  void run_(const Program &p, uint32_t t) {
    for (int steps = 0; this->pc_ < p.ops.size(); steps++) {
      if (steps > 1000) {  // a loop with nothing timed in it would spin forever
        this->pc_ = p.ops.size();
        return;
      }
      const Op &op = p.ops[this->pc_];
      switch (op.code) {
        case JUMP:
          for (int i = 0; i < 3; i++)
            this->cur_[i] = pick(op.ch[i], this->cur_[i]);
          this->pc_++;
          break;
        case SPEED:
          this->speed_ = op.n / 100.0f;
          this->pc_++;
          break;
        case SCENE:
          this->scene_ = op.n;
          this->pc_++;
          break;
        case LOOP:
          if (this->left_[this->pc_] < 0)
            this->left_[this->pc_] = op.n == 0 ? INT32_MAX : op.n;
          this->pc_++;
          break;
        case END: {
          int32_t &left = this->left_[op.pair];
          if (left != INT32_MAX)
            left--;
          if (left > 0) {
            this->pc_ = op.pair + 1;
          } else {
            left = -1;  // so an outer loop can run this one again from the top
            this->pc_++;
          }
          break;
        }
        default:  // FADE, ARC, HOLD
          this->code_ = op.code;
          this->ease_ = op.ease;
          this->seg_t0_ = t;
          float ms = op.ms / this->speed_;
          this->seg_ms_ = ms < 1.0f ? 1 : (uint32_t) ms;
          for (int i = 0; i < 3; i++) {
            this->from_[i] = this->cur_[i];
            this->to_[i] = op.code == FADE ? pick(op.ch[i], this->cur_[i]) : this->cur_[i];
          }
          if (op.code == ARC) {
            rgb2hsv(this->from_, this->h_, this->s_, this->v_);
            this->deg_ = op.deg;
          }
          this->in_seg_ = true;
          this->pc_++;
          return;
      }
    }
  }

  void colour_at_(float x, float out[3]) const {
    if (this->code_ == ARC) {
      hsv2rgb(this->h_ + this->deg_ * x, this->s_, this->v_, out);
      return;
    }
    for (int i = 0; i < 3; i++)
      out[i] = this->from_[i] + (this->to_[i] - this->from_[i]) * x;
  }

  const Program *prog_ = nullptr;
  uint32_t gen_ = 0;
  size_t pc_ = 0;
  bool in_seg_ = false;
  Code code_ = HOLD;
  uint8_t ease_ = 0;
  uint32_t seg_t0_ = 0, seg_ms_ = 1;
  float cur_[3] = {0, 0, 0}, from_[3] = {0, 0, 0}, to_[3] = {0, 0, 0};
  float h_ = 0, s_ = 0, v_ = 0, deg_ = 0, speed_ = 1.0f;
  std::vector<int32_t> left_;  // per LOOP step: passes left, -1 = not running
  int scene_ = -1;
};

// The built-in tour. Keeps all three LEDs partly on as much as possible, so every shadow
// has its own colour: where one LED is blocked you see the mix of the other two.
static const char *const COLOUR_SHOW = R"GCODE(
M808 L0
  M117 White light, colour shadows
  G1 R255 G255 B255 T3000 E1
  G4 T6000

  M117 Blue steps out
  G1 B0 T4000 E1
  G4 T2500
  G1 B255 T3000 E1
  M117 Green steps out
  G1 G0 T4000 E1
  G4 T2500
  G1 G255 T3000 E1
  M117 Red steps out
  G1 R0 T4000 E1
  G4 T2500
  G1 R255 T3000 E1
  G4 T1500

  M117 Shadow dance
  M808 L4
    G1 R50 G255 B255 T1400 E1
    G1 R255 G50 B255 T1400 E1
    G1 R255 G255 B50 T1400 E1
  M808

  M117 Rainbow shadows
  G1 R255 G120 B40 T3000 E1
  G3 A360 T24000

  M117 Opposites
  M808 L3
    G1 R255 G130 B20 T2600 E1
    G4 T1400
    G1 R20 G130 B255 T2600 E1
    G4 T1400
  M808

  M117 Sunset
  G1 R255 G170 B90 T3000 E1
  G1 R255 G110 B30 T4000 E1
  G1 R255 G55 B45 T4000 E1
  G1 R215 G30 B95 T4000 E1
  G1 R140 G20 B170 T4000 E1
  G1 R45 G20 B160 T4000 E1
  G4 T2500

  M117 Aurora
  M808 L8
    G1 R?0-70 G?90-255 B?100-255 T3200 E1
  M808

  M117 Candy
  M808 L8
    G1 R?120-255 G?60-230 B?90-255 T2400 E1
  M808
M808
)GCODE";

inline Player &player() {
  static Player p;
  return p;
}

inline Program &custom() {
  static Program p;
  return p;
}

inline Program &colour_show() {
  static Program p;
  if (p.gen == 0) {
    std::string err = p.load(COLOUR_SHOW);
    if (!err.empty())
      ESP_LOGE("rgb_gcode", "built-in Colour Show rejected: %s", err.c_str());
  }
  return p;
}

// One effect frame: play `prog` on the light. Returns a scene name to publish (the empty
// string when a show starts), else nullptr.
inline const std::string *frame(esphome::light::LightState &it, bool initial_run, const Program &prog) {
  static const std::string NO_SCENE;
  static float last[3];
  float master = it.remote_values.get_brightness();
  // A (re)started show begins from whatever the lamp shows now, so its first fade is smooth.
  float start[3];
  it.current_values.as_rgb(&start[0], &start[1], &start[2]);
  for (int i = 0; i < 3; i++) {
    start[i] = master > 0.01f ? fminf(1.0f, start[i] / master) : start[i];
    if (initial_run)
      last[i] = -1;
  }
  float out[3];
  int scene = player().tick(prog, esphome::millis(), initial_run, start, out);
  if (fabsf(out[0] - last[0]) > 0.001f || fabsf(out[1] - last[1]) > 0.001f || fabsf(out[2] - last[2]) > 0.001f) {
    for (int i = 0; i < 3; i++)
      last[i] = out[i];
    float mx = fmaxf(out[0], fmaxf(out[1], out[2]));
    auto call = it.turn_on();
    if (mx > 0.0005f)
      call.set_rgb(out[0] / mx, out[1] / mx, out[2] / mx);
    call.set_brightness(mx * master);  // 0 = dark with the state kept on, so the effect keeps running
    call.set_transition_length(0);
    call.set_publish(false);
    call.set_save(false);
    call.perform();
  }
  if (scene >= 0 && (size_t) scene < prog.texts.size())
    return &prog.texts[scene];
  return initial_run ? &NO_SCENE : nullptr;
}

}  // namespace rgb_gcode
