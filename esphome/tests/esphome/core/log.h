#pragma once
#include <cstdio>
#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
