/*******************************************************************************
 * Size: 18 px
 * Bpp: 1
 * Opts: --bpp 1 --size 18 --font C:/Users/Ignacio/Documents/YouTube_Studio/EuroTruck_Sim_Project/Squareline/assets/Inter.ttf -o C:/Users/Ignacio/Documents/YouTube_Studio/EuroTruck_Sim_Project/Squareline/assets\ui_font_FontUIRegular.c --format lvgl -r 0x20-0x7f --no-compress --no-prefilter
 ******************************************************************************/

#include "ui.h"

#ifndef UI_FONT_FONTUIREGULAR
#define UI_FONT_FONTUIREGULAR 1
#endif

#if UI_FONT_FONTUIREGULAR

/*-----------------
 *    BITMAPS
 *----------------*/

/*Store the image of the glyphs*/
static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xff, 0xff, 0xc3, 0xc0,

    /* U+0022 "\"" */
    0xde, 0xf7, 0xbd, 0x80,

    /* U+0023 "#" */
    0x8, 0xc1, 0x18, 0x62, 0x3f, 0xf1, 0x8, 0x23,
    0x4, 0x60, 0x8c, 0xff, 0xc6, 0x20, 0x84, 0x11,
    0x82, 0x30,

    /* U+0024 "$" */
    0x8, 0x4, 0x2, 0x7, 0xc6, 0xb6, 0x4f, 0x21,
    0xd0, 0x78, 0x1f, 0x83, 0xe1, 0x70, 0x9e, 0x4f,
    0xac, 0x7c, 0x8, 0x4, 0x0,

    /* U+0025 "%" */
    0x70, 0x23, 0x60, 0x8d, 0x84, 0x36, 0x20, 0x70,
    0x80, 0x4, 0x0, 0x20, 0x0, 0x9e, 0x4, 0xcc,
    0x23, 0x31, 0xc, 0xc4, 0x33, 0x20, 0x78,

    /* U+0026 "&" */
    0x3e, 0xc, 0x61, 0x8c, 0x31, 0x87, 0xe0, 0x78,
    0xe, 0x3, 0xe6, 0xe6, 0xd8, 0xfb, 0xe, 0x31,
    0xe3, 0xec,

    /* U+0027 "'" */
    0xff, 0xc0,

    /* U+0028 "(" */
    0x13, 0x26, 0x6c, 0xcc, 0xcc, 0xcc, 0xc6, 0x63,
    0x30,

    /* U+0029 ")" */
    0x8c, 0x46, 0x62, 0x33, 0x33, 0x33, 0x36, 0x6c,
    0xc0,

    /* U+002A "*" */
    0x10, 0x23, 0xf0, 0x87, 0xc2, 0x4, 0x0,

    /* U+002B "+" */
    0xc, 0x3, 0x0, 0xc0, 0x30, 0xff, 0xc3, 0x0,
    0xc0, 0x30, 0xc, 0x0,

    /* U+002C "," */
    0x69, 0x68,

    /* U+002D "-" */
    0xfc,

    /* U+002E "." */
    0xf0,

    /* U+002F "/" */
    0xc, 0x30, 0x82, 0x18, 0x61, 0x4, 0x30, 0xc2,
    0x8, 0x61, 0x84, 0x10,

    /* U+0030 "0" */
    0x3e, 0x31, 0x98, 0xd8, 0x3c, 0x1e, 0xf, 0x7,
    0x83, 0xc1, 0xe0, 0xd8, 0xcc, 0x63, 0xe0,

    /* U+0031 "1" */
    0x3b, 0xf6, 0x31, 0x8c, 0x63, 0x18, 0xc6, 0x31,
    0x80,

    /* U+0032 "2" */
    0x3c, 0x66, 0xc3, 0xc3, 0x3, 0x7, 0x6, 0xc,
    0x1c, 0x38, 0x70, 0x60, 0xff,

    /* U+0033 "3" */
    0x1e, 0xc, 0xc6, 0x18, 0x6, 0x1, 0x80, 0xc0,
    0xe0, 0x6, 0x0, 0xc0, 0x36, 0xd, 0xc6, 0x3f,
    0x0,

    /* U+0034 "4" */
    0x7, 0x1, 0xc0, 0xf0, 0x6c, 0x13, 0xc, 0xc2,
    0x31, 0x8c, 0xc3, 0x3f, 0xf0, 0x30, 0xc, 0x3,
    0x0,

    /* U+0035 "5" */
    0x7f, 0x20, 0x10, 0x18, 0xf, 0xe6, 0x18, 0xe,
    0x3, 0x1, 0x80, 0xf0, 0x6c, 0x63, 0xe0,

    /* U+0036 "6" */
    0x1e, 0x11, 0x98, 0x78, 0xd, 0xe7, 0x1b, 0x7,
    0x83, 0xc1, 0xe0, 0xd0, 0x6c, 0x61, 0xe0,

    /* U+0037 "7" */
    0xff, 0x3, 0x2, 0x6, 0x4, 0xc, 0xc, 0x18,
    0x18, 0x30, 0x30, 0x20, 0x60,

    /* U+0038 "8" */
    0x3e, 0x31, 0xb0, 0x78, 0x3c, 0x1b, 0x18, 0xf8,
    0xc6, 0xc1, 0xe0, 0xf0, 0x6c, 0x63, 0xe0,

    /* U+0039 "9" */
    0x3c, 0x31, 0xb0, 0x58, 0x3c, 0x1e, 0xd, 0x8e,
    0x7b, 0x1, 0xe0, 0xf0, 0xcc, 0x43, 0xc0,

    /* U+003A ":" */
    0xf0, 0x3, 0xc0,

    /* U+003B ";" */
    0x6c, 0x0, 0x3, 0x49, 0x20,

    /* U+003C "<" */
    0x0, 0x81, 0xc3, 0x87, 0xe, 0x7, 0x0, 0xf0,
    0x1e, 0x3, 0x80, 0x40,

    /* U+003D "=" */
    0xff, 0x0, 0x0, 0x0, 0xff,

    /* U+003E ">" */
    0x80, 0x70, 0x1e, 0x1, 0xc0, 0x38, 0x38, 0x70,
    0xe0, 0xc0, 0x0, 0x0,

    /* U+003F "?" */
    0x3c, 0x66, 0x43, 0x3, 0x3, 0x6, 0x1c, 0x18,
    0x18, 0x0, 0x0, 0x18, 0x18,

    /* U+0040 "@" */
    0x7, 0xe0, 0x1c, 0x18, 0x30, 0xc, 0x60, 0x6,
    0x67, 0xe3, 0xcc, 0xe3, 0xd8, 0x63, 0xd8, 0x63,
    0xd8, 0x63, 0xd8, 0x63, 0xcc, 0xe6, 0x67, 0x3c,
    0x60, 0x0, 0x30, 0x0, 0x1c, 0x10, 0x7, 0xf0,

    /* U+0041 "A" */
    0xe, 0x1, 0x40, 0x28, 0xd, 0x81, 0x30, 0x63,
    0xc, 0x61, 0x4, 0x7f, 0xcc, 0x19, 0x1, 0x60,
    0x3c, 0x6,

    /* U+0042 "B" */
    0xfe, 0x61, 0xb0, 0x78, 0x3c, 0x1e, 0x1b, 0xf9,
    0x86, 0xc1, 0xe0, 0xf0, 0x78, 0x6f, 0xe0,

    /* U+0043 "C" */
    0x1f, 0x86, 0x19, 0x81, 0xb0, 0x1c, 0x1, 0x80,
    0x30, 0x6, 0x0, 0xc0, 0xc, 0x5, 0x81, 0x98,
    0x61, 0xf8,

    /* U+0044 "D" */
    0xfe, 0x18, 0x33, 0x3, 0x60, 0x6c, 0x7, 0x80,
    0xf0, 0x1e, 0x3, 0xc0, 0x78, 0x1b, 0x3, 0x60,
    0xcf, 0xe0,

    /* U+0045 "E" */
    0xff, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xff, 0xc0,
    0xc0, 0xc0, 0xc0, 0xc0, 0xff,

    /* U+0046 "F" */
    0xff, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xff, 0xc0,
    0xc0, 0xc0, 0xc0, 0xc0, 0xc0,

    /* U+0047 "G" */
    0x1f, 0x6, 0x39, 0x83, 0x60, 0x3c, 0x1, 0x80,
    0x30, 0xfe, 0x3, 0xc0, 0x78, 0xd, 0x83, 0x18,
    0xe1, 0xf0,

    /* U+0048 "H" */
    0xc0, 0xf0, 0x3c, 0xf, 0x3, 0xc0, 0xf0, 0x3f,
    0xff, 0x3, 0xc0, 0xf0, 0x3c, 0xf, 0x3, 0xc0,
    0xc0,

    /* U+0049 "I" */
    0xff, 0xff, 0xff, 0xc0,

    /* U+004A "J" */
    0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3, 0x3,
    0x3, 0xc3, 0xc3, 0x66, 0x3c,

    /* U+004B "K" */
    0xc0, 0xf0, 0x6c, 0x33, 0x18, 0xcc, 0x36, 0xf,
    0xc3, 0xb0, 0xc6, 0x30, 0xcc, 0x1b, 0x6, 0xc0,
    0xc0,

    /* U+004C "L" */
    0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0, 0xc0,
    0xc0, 0xc0, 0xc0, 0xc0, 0xff,

    /* U+004D "M" */
    0xe0, 0x3f, 0x1, 0xfc, 0x1f, 0xe0, 0xbd, 0x5,
    0xec, 0x6f, 0x62, 0x79, 0x13, 0xcd, 0x9e, 0x28,
    0xf1, 0x47, 0x8e, 0x3c, 0x21, 0x80,

    /* U+004E "N" */
    0xe0, 0x7c, 0xf, 0xc1, 0xfc, 0x3d, 0x87, 0x98,
    0xf3, 0x1e, 0x33, 0xc3, 0x78, 0x6f, 0x7, 0xe0,
    0x7c, 0xe,

    /* U+004F "O" */
    0x1f, 0x83, 0xc, 0x60, 0x66, 0x6, 0xc0, 0x3c,
    0x3, 0xc0, 0x3c, 0x3, 0xc0, 0x36, 0x6, 0x60,
    0x63, 0xc, 0x1f, 0x0,

    /* U+0050 "P" */
    0xfe, 0x61, 0xb0, 0x78, 0x3c, 0x1e, 0xf, 0xd,
    0xfc, 0xc0, 0x60, 0x30, 0x18, 0xc, 0x0,

    /* U+0051 "Q" */
    0x1f, 0x83, 0xc, 0x60, 0x66, 0x6, 0xc0, 0x3c,
    0x3, 0xc0, 0x3c, 0x3, 0xc0, 0x36, 0x26, 0x61,
    0x63, 0x1c, 0x1f, 0xc0, 0x6,

    /* U+0052 "R" */
    0xfe, 0x30, 0xcc, 0x1b, 0x6, 0xc1, 0xb0, 0x6c,
    0x33, 0xf8, 0xc6, 0x31, 0x8c, 0x33, 0xe, 0xc1,
    0x80,

    /* U+0053 "S" */
    0x3e, 0x31, 0xb0, 0x78, 0xe, 0x3, 0xc0, 0xfc,
    0x1f, 0x3, 0x80, 0xf0, 0x7c, 0x63, 0xe0,

    /* U+0054 "T" */
    0xff, 0xc3, 0x0, 0xc0, 0x30, 0xc, 0x3, 0x0,
    0xc0, 0x30, 0xc, 0x3, 0x0, 0xc0, 0x30, 0xc,
    0x0,

    /* U+0055 "U" */
    0xc0, 0xf0, 0x3c, 0xf, 0x3, 0xc0, 0xf0, 0x3c,
    0xf, 0x3, 0xc0, 0xf0, 0x3c, 0xd, 0x86, 0x1e,
    0x0,

    /* U+0056 "V" */
    0x40, 0x36, 0x2, 0x60, 0x62, 0x6, 0x30, 0x43,
    0xc, 0x18, 0xc1, 0x88, 0x9, 0x80, 0xd0, 0xd,
    0x0, 0x70, 0x6, 0x0,

    /* U+0057 "W" */
    0x40, 0xc1, 0xb0, 0x60, 0xd8, 0x70, 0x6c, 0x3c,
    0x22, 0x16, 0x31, 0x99, 0x18, 0xcc, 0xc8, 0x24,
    0x64, 0x12, 0x16, 0xf, 0xb, 0x7, 0x87, 0x1,
    0x83, 0x80, 0xc0, 0xc0,

    /* U+0058 "X" */
    0x40, 0x4c, 0x18, 0xc6, 0x8, 0x81, 0xb0, 0x1c,
    0x3, 0x80, 0x50, 0x1b, 0x6, 0x30, 0xc6, 0x30,
    0x6c, 0x6,

    /* U+0059 "Y" */
    0xc0, 0x36, 0x6, 0x30, 0xc3, 0xc, 0x19, 0x80,
    0xf0, 0xf, 0x0, 0x60, 0x6, 0x0, 0x60, 0x6,
    0x0, 0x60, 0x6, 0x0,

    /* U+005A "Z" */
    0xff, 0x80, 0xc0, 0xc0, 0x40, 0x60, 0x60, 0x20,
    0x30, 0x30, 0x10, 0x18, 0x18, 0xf, 0xf8,

    /* U+005B "[" */
    0xfc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcf,

    /* U+005C "\\" */
    0x41, 0x6, 0x18, 0x20, 0x83, 0xc, 0x10, 0x41,
    0x86, 0x8, 0x20, 0xc3,

    /* U+005D "]" */
    0xf3, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3f,

    /* U+005E "^" */
    0x30, 0x71, 0xa2, 0x6c, 0x58, 0xc0,

    /* U+005F "_" */
    0xff,

    /* U+0060 "`" */
    0x44,

    /* U+0061 "a" */
    0x3c, 0x63, 0x3, 0x3, 0x7f, 0xf3, 0xc3, 0xc3,
    0xc7, 0x7b,

    /* U+0062 "b" */
    0xc0, 0x60, 0x30, 0x1b, 0xce, 0x36, 0xf, 0x7,
    0x83, 0xc1, 0xe0, 0xf0, 0x7c, 0x6d, 0xe0,

    /* U+0063 "c" */
    0x1e, 0x31, 0xb8, 0x78, 0xc, 0x6, 0x3, 0x0,
    0xc3, 0x63, 0xf, 0x0,

    /* U+0064 "d" */
    0x1, 0x80, 0xc0, 0x67, 0xb6, 0x3e, 0xf, 0x7,
    0x83, 0xc1, 0xe0, 0xf0, 0x6c, 0x73, 0xd8,

    /* U+0065 "e" */
    0x3e, 0x31, 0xb0, 0x78, 0x3f, 0xfe, 0x3, 0x1,
    0x82, 0x63, 0xf, 0x0,

    /* U+0066 "f" */
    0x1e, 0x60, 0xc7, 0xe3, 0x6, 0xc, 0x18, 0x30,
    0x60, 0xc1, 0x83, 0x0,

    /* U+0067 "g" */
    0x3d, 0xb1, 0xf0, 0x78, 0x3c, 0x1e, 0xf, 0x7,
    0x83, 0x63, 0x9e, 0xc0, 0x60, 0x36, 0x31, 0xf0,

    /* U+0068 "h" */
    0xc0, 0xc0, 0xc0, 0xde, 0xe7, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3,

    /* U+0069 "i" */
    0xf3, 0xff, 0xff, 0xc0,

    /* U+006A "j" */
    0x33, 0x3, 0x33, 0x33, 0x33, 0x33, 0x33, 0x33,
    0xe0,

    /* U+006B "k" */
    0xc0, 0x60, 0x30, 0x18, 0x6c, 0x66, 0x63, 0x61,
    0xb0, 0xf8, 0x66, 0x33, 0x98, 0xcc, 0x30,

    /* U+006C "l" */
    0xff, 0xff, 0xff, 0xc0,

    /* U+006D "m" */
    0xdd, 0xee, 0x73, 0xc6, 0x3c, 0x63, 0xc6, 0x3c,
    0x63, 0xc6, 0x3c, 0x63, 0xc6, 0x3c, 0x63,

    /* U+006E "n" */
    0xde, 0xe7, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xc3, 0xc3,

    /* U+006F "o" */
    0x3e, 0x31, 0xb8, 0x58, 0x3c, 0x1e, 0xf, 0x7,
    0x83, 0x63, 0x1f, 0x0,

    /* U+0070 "p" */
    0xde, 0x71, 0xb0, 0x78, 0x3c, 0x1e, 0xf, 0x7,
    0x83, 0xe3, 0x6f, 0x30, 0x18, 0xc, 0x6, 0x0,

    /* U+0071 "q" */
    0x3d, 0xb1, 0xf0, 0x78, 0x3c, 0x1e, 0xf, 0x7,
    0x83, 0x63, 0x9e, 0xc0, 0x60, 0x30, 0x18, 0xc,

    /* U+0072 "r" */
    0xdf, 0x31, 0x8c, 0x63, 0x18, 0xc6, 0x0,

    /* U+0073 "s" */
    0x3c, 0xe6, 0xc0, 0xe0, 0x7c, 0x3f, 0x7, 0x43,
    0xc6, 0x7c,

    /* U+0074 "t" */
    0x63, 0x19, 0xf6, 0x31, 0x8c, 0x63, 0x18, 0xc3,
    0x80,

    /* U+0075 "u" */
    0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3, 0xc3,
    0xe7, 0x7b,

    /* U+0076 "v" */
    0x40, 0x98, 0x66, 0x18, 0x84, 0x33, 0x4, 0xc1,
    0x20, 0x78, 0xc, 0x3, 0x0,

    /* U+0077 "w" */
    0x86, 0x1e, 0x38, 0xf1, 0x44, 0x8a, 0x66, 0xd3,
    0x34, 0xd0, 0xa2, 0x85, 0x1c, 0x38, 0xe1, 0x86,
    0x0,

    /* U+0078 "x" */
    0x61, 0x31, 0x8d, 0x82, 0x81, 0xc0, 0xe0, 0xd0,
    0x6c, 0x63, 0x60, 0x80,

    /* U+0079 "y" */
    0x40, 0x98, 0x66, 0x18, 0x84, 0x33, 0xc, 0xc1,
    0x20, 0x78, 0xe, 0x3, 0x0, 0xc0, 0x20, 0x18,
    0x1c, 0x0,

    /* U+007A "z" */
    0xff, 0x7, 0x6, 0xc, 0x18, 0x18, 0x30, 0x60,
    0x60, 0xff,

    /* U+007B "{" */
    0x1c, 0xc3, 0xc, 0x30, 0xc3, 0x38, 0x20, 0xc3,
    0xc, 0x30, 0xc3, 0x7,

    /* U+007C "|" */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xf0,

    /* U+007D "}" */
    0xe0, 0xc3, 0xc, 0x30, 0xc3, 0x7, 0x10, 0xc3,
    0xc, 0x30, 0xc3, 0x38,

    /* U+007E "~" */
    0x71, 0xe4, 0xf1, 0xc0
};


/*---------------------
 *  GLYPH DESCRIPTION
 *--------------------*/

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 81, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 83, .box_w = 2, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 5, .adv_w = 134, .box_w = 5, .box_h = 5, .ofs_x = 2, .ofs_y = 8},
    {.bitmap_index = 9, .adv_w = 182, .box_w = 11, .box_h = 13, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 27, .adv_w = 185, .box_w = 9, .box_h = 18, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 48, .adv_w = 283, .box_w = 14, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 71, .adv_w = 185, .box_w = 11, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 89, .adv_w = 86, .box_w = 2, .box_h = 5, .ofs_x = 2, .ofs_y = 8},
    {.bitmap_index = 91, .adv_w = 105, .box_w = 4, .box_h = 17, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 100, .adv_w = 105, .box_w = 4, .box_h = 17, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 109, .adv_w = 144, .box_w = 7, .box_h = 7, .ofs_x = 1, .ofs_y = 6},
    {.bitmap_index = 116, .adv_w = 191, .box_w = 10, .box_h = 9, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 128, .adv_w = 83, .box_w = 3, .box_h = 5, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 130, .adv_w = 132, .box_w = 6, .box_h = 1, .ofs_x = 1, .ofs_y = 5},
    {.bitmap_index = 131, .adv_w = 83, .box_w = 2, .box_h = 2, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 132, .adv_w = 104, .box_w = 6, .box_h = 16, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 144, .adv_w = 182, .box_w = 9, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 159, .adv_w = 117, .box_w = 5, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 168, .adv_w = 176, .box_w = 8, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 181, .adv_w = 178, .box_w = 10, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 198, .adv_w = 186, .box_w = 10, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 215, .adv_w = 171, .box_w = 9, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 230, .adv_w = 179, .box_w = 9, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 245, .adv_w = 163, .box_w = 8, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 258, .adv_w = 178, .box_w = 9, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 273, .adv_w = 179, .box_w = 9, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 288, .adv_w = 83, .box_w = 2, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 291, .adv_w = 87, .box_w = 3, .box_h = 12, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 296, .adv_w = 191, .box_w = 9, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 308, .adv_w = 191, .box_w = 8, .box_h = 5, .ofs_x = 2, .ofs_y = 3},
    {.bitmap_index = 313, .adv_w = 191, .box_w = 9, .box_h = 10, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 325, .adv_w = 147, .box_w = 8, .box_h = 13, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 338, .adv_w = 278, .box_w = 16, .box_h = 16, .ofs_x = 1, .ofs_y = -4},
    {.bitmap_index = 370, .adv_w = 199, .box_w = 11, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 388, .adv_w = 188, .box_w = 9, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 403, .adv_w = 210, .box_w = 11, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 421, .adv_w = 208, .box_w = 11, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 439, .adv_w = 173, .box_w = 8, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 452, .adv_w = 170, .box_w = 8, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 465, .adv_w = 215, .box_w = 11, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 483, .adv_w = 214, .box_w = 10, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 500, .adv_w = 77, .box_w = 2, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 504, .adv_w = 164, .box_w = 8, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 517, .adv_w = 194, .box_w = 10, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 534, .adv_w = 163, .box_w = 8, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 547, .adv_w = 260, .box_w = 13, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 569, .adv_w = 217, .box_w = 11, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 587, .adv_w = 220, .box_w = 12, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 607, .adv_w = 184, .box_w = 9, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 622, .adv_w = 220, .box_w = 12, .box_h = 14, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 643, .adv_w = 185, .box_w = 10, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 660, .adv_w = 185, .box_w = 9, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 675, .adv_w = 186, .box_w = 10, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 692, .adv_w = 214, .box_w = 10, .box_h = 13, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 709, .adv_w = 199, .box_w = 12, .box_h = 13, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 729, .adv_w = 284, .box_w = 17, .box_h = 13, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 757, .adv_w = 196, .box_w = 11, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 775, .adv_w = 195, .box_w = 12, .box_h = 13, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 795, .adv_w = 181, .box_w = 9, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 810, .adv_w = 105, .box_w = 4, .box_h = 16, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 818, .adv_w = 104, .box_w = 6, .box_h = 16, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 830, .adv_w = 105, .box_w = 4, .box_h = 16, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 838, .adv_w = 136, .box_w = 7, .box_h = 6, .ofs_x = 1, .ofs_y = 7},
    {.bitmap_index = 844, .adv_w = 131, .box_w = 8, .box_h = 1, .ofs_x = 0, .ofs_y = -1},
    {.bitmap_index = 845, .adv_w = 93, .box_w = 3, .box_h = 2, .ofs_x = 1, .ofs_y = 11},
    {.bitmap_index = 846, .adv_w = 162, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 856, .adv_w = 176, .box_w = 9, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 871, .adv_w = 165, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 883, .adv_w = 176, .box_w = 9, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 898, .adv_w = 168, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 910, .adv_w = 107, .box_w = 7, .box_h = 13, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 922, .adv_w = 177, .box_w = 9, .box_h = 14, .ofs_x = 1, .ofs_y = -4},
    {.bitmap_index = 938, .adv_w = 170, .box_w = 8, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 951, .adv_w = 70, .box_w = 2, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 955, .adv_w = 70, .box_w = 4, .box_h = 17, .ofs_x = -1, .ofs_y = -4},
    {.bitmap_index = 964, .adv_w = 158, .box_w = 9, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 979, .adv_w = 70, .box_w = 2, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 983, .adv_w = 252, .box_w = 12, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 998, .adv_w = 170, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1008, .adv_w = 173, .box_w = 9, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1020, .adv_w = 176, .box_w = 9, .box_h = 14, .ofs_x = 1, .ofs_y = -4},
    {.bitmap_index = 1036, .adv_w = 176, .box_w = 9, .box_h = 14, .ofs_x = 1, .ofs_y = -4},
    {.bitmap_index = 1052, .adv_w = 108, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1059, .adv_w = 152, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1069, .adv_w = 94, .box_w = 5, .box_h = 13, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1078, .adv_w = 170, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1088, .adv_w = 162, .box_w = 10, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1101, .adv_w = 236, .box_w = 13, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1118, .adv_w = 157, .box_w = 9, .box_h = 10, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1130, .adv_w = 162, .box_w = 10, .box_h = 14, .ofs_x = 0, .ofs_y = -4},
    {.bitmap_index = 1148, .adv_w = 159, .box_w = 8, .box_h = 10, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 1158, .adv_w = 123, .box_w = 6, .box_h = 16, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1170, .adv_w = 96, .box_w = 2, .box_h = 22, .ofs_x = 2, .ofs_y = -4},
    {.bitmap_index = 1176, .adv_w = 123, .box_w = 6, .box_h = 16, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 1188, .adv_w = 191, .box_w = 9, .box_h = 3, .ofs_x = 1, .ofs_y = 4}
};

/*---------------------
 *  CHARACTER MAPPING
 *--------------------*/



/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    }
};

/*-----------------
 *    KERNING
 *----------------*/


/*Pair left and right glyphs for kerning*/
static const uint8_t kern_pair_glyph_ids[] =
{
    3, 7,
    3, 13,
    3, 15,
    3, 21,
    7, 3,
    7, 8,
    7, 61,
    8, 7,
    8, 13,
    8, 15,
    8, 21,
    11, 7,
    11, 13,
    11, 15,
    11, 21,
    11, 33,
    11, 64,
    12, 19,
    12, 20,
    12, 24,
    12, 61,
    13, 3,
    13, 8,
    13, 17,
    13, 18,
    13, 20,
    13, 22,
    13, 23,
    13, 24,
    13, 25,
    13, 26,
    13, 32,
    13, 33,
    14, 19,
    14, 20,
    14, 24,
    14, 61,
    15, 3,
    15, 8,
    15, 17,
    15, 18,
    15, 20,
    15, 22,
    15, 23,
    15, 24,
    15, 25,
    15, 26,
    15, 32,
    15, 33,
    16, 13,
    16, 15,
    17, 13,
    17, 15,
    17, 24,
    17, 61,
    17, 64,
    19, 21,
    20, 11,
    20, 13,
    20, 15,
    20, 63,
    21, 11,
    21, 13,
    21, 15,
    21, 18,
    21, 63,
    22, 13,
    22, 15,
    23, 13,
    23, 15,
    23, 64,
    24, 4,
    24, 7,
    24, 13,
    24, 15,
    24, 17,
    24, 20,
    24, 21,
    24, 22,
    24, 23,
    24, 24,
    24, 25,
    24, 26,
    24, 27,
    24, 28,
    24, 29,
    24, 64,
    25, 11,
    25, 13,
    25, 15,
    25, 63,
    26, 13,
    26, 15,
    26, 24,
    26, 61,
    26, 64,
    27, 61,
    28, 61,
    30, 61,
    31, 24,
    31, 61,
    33, 13,
    33, 15,
    33, 16,
    33, 61,
    33, 64,
    61, 3,
    61, 8,
    61, 11,
    61, 12,
    61, 14,
    61, 16,
    61, 18,
    61, 30,
    61, 32,
    61, 33,
    61, 61,
    61, 63,
    61, 95,
    63, 7,
    63, 13,
    63, 15,
    63, 21,
    63, 33,
    63, 64,
    64, 11,
    64, 17,
    64, 18,
    64, 20,
    64, 21,
    64, 22,
    64, 23,
    64, 25,
    64, 26,
    64, 33,
    64, 61,
    64, 63,
    64, 93,
    95, 19,
    95, 20,
    95, 24,
    95, 61
};

/* Kerning between the respective left and right glyphs
 * 4.4 format which needs to scaled with `kern_scale`*/
static const int8_t kern_pair_values[] =
{
    -10, -24, -24, -18, -10, -10, -18, -10,
    -24, -24, -18, -10, -39, -39, -13, -3,
    -20, -10, -3, -6, -15, -24, -24, -7,
    -25, -8, -4, -7, -4, -6, -2, -25,
    -13, -10, -3, -6, -15, -24, -24, -7,
    -25, -8, -4, -7, -4, -6, -2, -25,
    -13, -11, -11, -7, -7, -6, -3, -13,
    -4, -4, -6, -6, -4, -6, -10, -10,
    -6, -6, -8, -8, -10, -10, -13, -16,
    -13, -36, -36, -4, -5, -17, -3, -4,
    6, -4, -3, -10, -10, -26, -46, -4,
    -6, -6, -4, -7, -7, -6, -3, -13,
    -18, -18, -20, -21, -21, -13, -13, -11,
    -10, -11, -23, -23, -23, -10, -10, 6,
    -10, -15, -18, -10, -14, -23, -10, -10,
    -39, -39, -13, -3, -20, -20, -13, -31,
    -13, -16, -13, -13, -13, -13, -11, -23,
    -20, 10, -10, -3, -6, -15
};

/*Collect the kern pair's data in one place*/
static const lv_font_fmt_txt_kern_pair_t kern_pairs =
{
    .glyph_ids = kern_pair_glyph_ids,
    .values = kern_pair_values,
    .pair_cnt = 142,
    .glyph_ids_size = 0
};

/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = &kern_pairs,
    .kern_scale = 16,
    .cmap_num = 1,
    .bpp = 1,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t ui_font_FontUIRegular = {
#else
lv_font_t ui_font_FontUIRegular = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 22,          /*The maximum line height required by the font*/
    .base_line = 4,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -3,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = NULL,
#endif
    .user_data = NULL,
};



#endif /*#if UI_FONT_FONTUIREGULAR*/

