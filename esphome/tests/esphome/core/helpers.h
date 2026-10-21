#pragma once
#include <cstdint>
namespace esphome {
extern uint32_t rng_state;
inline float random_float() { rng_state = rng_state * 1664525u + 1013904223u; return (rng_state >> 8) / 16777215.0f; }
}
