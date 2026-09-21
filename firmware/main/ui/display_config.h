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

/* Full-frame draw buffer in PSRAM (MEMORY_LAYOUT.md: big buffers -> PSRAM);
 * a small SRAM chunk streams PSRAM -> SPI DMA. Transfers are synchronous
 * (trans_queue_depth = 0): ~30ms/frame @40MHz is fine for a status screen
 * and keeps flush ordering trivially correct. */
#define KDisplayDrawBufPix      (KDisplayHorRes * KDisplayVerRes)
#define KDisplayTransBytes      (KDisplayHorRes * 10 * (KDisplayBitsPerPixel / 8))
#define KDisplayBitsPerPixel    (16)