#pragma once

#include <cstdint>

// 1-bit 清纯无抗锯齿 6x12 像素点阵小字库 (ASCII 32 - 126)
// 每个字符仅占 12 字节。
extern const uint8_t font6x12_raw[95][12];

// 4-bit 抗锯齿 Sans-Serif 中号字库 (ASCII 32 - 126)
// 每个字符 256 字节。
extern const uint8_t font16x32_aa[95][256];

// 4-bit 抗锯齿巨型数字与档位字库 ('0'-'9' 以及 '-', ' ', '.', 'P', 'R', 'N', 'D', 'S')
// 每个字符 1600 字节。每行 40 像素占用 20 字节。
extern const uint8_t font40x80_nums_aa[18][1600];

int get_font40x80_index(char c);
