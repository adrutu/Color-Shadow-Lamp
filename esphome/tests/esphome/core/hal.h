#pragma once
#include <cstdint>
namespace esphome { extern uint32_t fake_now; inline uint32_t millis() { return fake_now; } }
