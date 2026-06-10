/*
  per-board configuration for the DroneCAN-only bootloader

  Select a board by defining BOARD_<name> on the compiler command line
  (the Makefile does this, e.g. `make M460_PCU` -> -DBOARD_M460_PCU).
  Each board block applies all of the defines that board needs:

    BOARD_NAME           human readable board name
    BOARD_FLASH_SIZE     MCU flash size in KB (drives EEPROM layout)
    APP_SIGNATURE_MAGIC1 \ the two magic words scanned for in the app
    APP_SIGNATURE_MAGIC2 / image to locate the application signature
    BOARD_SIGNATURE_ID   string matched against app_signature.mcu[]

  The magic values default to the AM32 constants so they stay consistent
  with bl_update/ and bl_update/set_app_signature.py (which are unchanged).
 */
#pragma once

#if defined(BOARD_M460_PCU)
#define BOARD_NAME           "M460_PCU"
#define BOARD_FLASH_SIZE     128
#define APP_SIGNATURE_MAGIC1 0x68f058e6
#define APP_SIGNATURE_MAGIC2 0xafcee5a0
#define BOARD_SIGNATURE_ID   "L431"

#else
#error "No board selected - define a BOARD_xxx block in Inc/targets.h and pass -DBOARD_xxx"
#endif
