#include "lv_conf.h"

// => Hardware select
#define LILYGO_LILYPI_V1

//NOT SUPPORT ...
// #define LILYGO_WATCH_2020_V1
// #define LILYGO_WATCH_2019_WITH_TOUCH
// #define LILYGO_WATCH_2019_NO_TOUCH
// #define LILYGO_WATCH_BLOCK

//NOT SUPPORT ...
// => Function select
//#define LILYGO_BLOCK_ST7796S_MODULE          //Use ST7796S
#define LILYGO_BLOCK_ILI9481_MODULE         //Use ILI9841
#define LILYGO_WATCH_LVGL                   //To use LVGL, you need to enable the macro LVGL
//#define TWATCH_USE_PSRAM_ALLOC_LVGL
//#define TWATCH_LVGL_DOUBLE_BUFFER
#define ENABLE_LVGL_FLUSH_DMA
#include <LilyGoWatch.h>