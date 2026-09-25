#pragma once

/* Kconfig wrappers for the ILI9341 SPI panel (values set in menuconfig ->
 * "TianshangPulse Display Configuration"). All pin constants live here so a
 * bring-up fix is a one-place edit; see docs/HARDWARE.md for the wiring table.
 *
 * NOTE (not hardware-verified yet): panel rotation/mirror and SPI clock
 * stability must be tuned on the real board; every tunable is in this file
 * or exposed via Kconfig. */

#include <stdint.h>
#include "sdkconfig.h"

#define KDisplayEnable          CONFIG_DISPLAY_ENABLE
#define KDisplayHorRes          (240)
#define KDisplayVerRes          (320)

#define KDisplaySpiSckGpio      CONFIG_DISPLAY_SPI_SCK_GPIO
#define KDisplaySpiMosiGpio     CONFIG_DISPLAY_SPI_MOSI_GPIO
#define KDisplayCsGpio          CONFIG_DISPLAY_SPI_CS_GPIO
#define KDisplayDcGpio          CONFIG_DISPLAY_DC_GPIO
#define KDisplayRstGpio         CONFIG_DISPLAY_RST_GPIO
#define KDisplayBlGpio          CONFIG_DISPLAY_BL_GPIO
#define KDisplaySpiClockHz      CONFIG_DISPLAY_SPI_CLOCK_HZ

/* Draw buffer = ONE BAND of scanlines, not the full frame.
 *
 * A full frame is 240*320 px = 150 KB. That is wrong twice over:
 *   1. With CONFIG_SPIRAM=n it lands in internal SRAM and eats the RAM that
 *      FreeRTOS still needs for queues and spinlocks -- the three-crashes-one-
 *      cause failure recorded in SCREEN_BRINGUP_FINDINGS.md §三.
 *   2. Even with PSRAM up it forces flags.buff_spiram, i.e. the display path
 *      then depends on PSRAM having initialised.
 * 40 rows = 9,600 px = 19,200 B, which fits internal DMA-capable RAM. LVGL
 * renders partial buffers by redrawing in bands on its own; a status screen
 * does not need a frame buffer. */
#define KDisplayBitsPerPixel    (16)
#define KDisplayBandRows        (40)
#define KDisplayDrawBufPix      (KDisplayHorRes * KDisplayBandRows)
#define KDisplayTransBytes      (KDisplayHorRes * 10 * (KDisplayBitsPerPixel / 8))