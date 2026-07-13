#pragma once

#include <cstdint>

// 4-bit 抗锯齿 Sans-Serif 小号字库 (ASCII 32 - 126)
// 每个字符 64 字节。每行 8 像素占用 4 字节。
extern const uint8_t font8x16_aa[95][64];

// 4-bit 抗锯齿 Sans-Serif 大号字库 (ASCII 32 - 126)
// 每个字符 256 字节。每行 16 像素占用 8 字节。
extern const uint8_t font16x32_aa[95][256];

// 4-bit 抗锯齿特大号数字字库 ('0'-'9' 以及 '-', ' ', '.')
// 每个字符 576 字节。每行 24 像素占用 12 字节。
extern const uint8_t font24x48_nums_aa[13][576];

// 获取数字字符在 font24x48_nums_aa 中的索引
int get_font24x48_index(char c);
