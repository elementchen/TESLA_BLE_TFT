#pragma once

#include <cstdint>

// 经典 Linux 8x16 无衬线点阵字库 (ASCII 32 - 126)
extern const uint8_t font8x16[95][16];

// 24x48 高精度科技感无衬线数字字库 ('0'-'9' 以及 '-', ' ', '.')
extern const uint8_t font24x48_nums[13][144];

// 获取数字字符在 font24x48_nums 中的索引
int get_font24x48_index(char c);
