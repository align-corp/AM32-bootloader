/*
  DroneCAN-only bootloader for custom firmware

  based on https://github.com/AlkaMotors/AT32F421_AM32_Bootloader

  This is a stripped down variant of the AM32 bootloader: the serial
  bit-bang flashing protocol (and its comms pin) and all of the AM32
  settings/EEPROM handling have been removed. The only way in is via
  DroneCAN firmware update.

  On boot we validate the application image (app-signature magic, board
  id, CRC and vector table) and, if valid, jump straight to it so the
  app can start without waiting for any CAN traffic. Otherwise we stay
  in the bootloader and service a DroneCAN firmware update.

  Board configuration (flash size, signature magic, board id) lives in
  Inc/targets.h, selected by a -DBOARD_xxx define from the Makefile.
 */
#include <main.h>

// targets.h provides the selected board configuration
#include "targets.h"

#include <stdbool.h>

// when there is no app fw yet, disable jump()
//#define DISABLE_JUMP

#ifndef MCU_FLASH_START
#define MCU_FLASH_START 0x08000000
#endif

#ifndef FIRMWARE_RELATIVE_START
#define FIRMWARE_RELATIVE_START 0x4000
#endif

// this TU owns the single definition of Error_Handler() and SystemInit()
#define BL_DEFINES_SYSTEM
#include <blutil.h>

#if DRONECAN_SUPPORT
#include "DroneCAN/DroneCAN.h"
#include "DroneCAN/sys_can.h"
#else
#error "DroneCAN-only bootloader requires DRONECAN_SUPPORT=1"
#endif

#define APPLICATION_ADDRESS (uint32_t)(MCU_FLASH_START + FIRMWARE_RELATIVE_START)

/*
  jump to the application firmware, but only if the image passes all of
  the sanity checks. Silently returns (staying in the bootloader) if not.
 */
static void jump()
{
#ifndef DISABLE_JUMP
#ifndef DISABLE_APP_HEADER_CHECKS
  /*
    first word of the app is the stack pointer - make sure that it is in range
   */
  const uint32_t *app = (uint32_t*)(MCU_FLASH_START + FIRMWARE_RELATIVE_START);
  const uint32_t ram_start = 0x20000000;
  const uint32_t ram_limit_kb = 64;
  const uint32_t ram_end = ram_start+ram_limit_kb*1024;
  if (app[0] < ram_start || app[0] > ram_end) {
    return;
  }
  /*
    2nd word is the entry point of the main app. Ensure that is in range
   */
  const uint32_t flash_limit_kb = 256;
  if (app[1] < APPLICATION_ADDRESS || app[1] > APPLICATION_ADDRESS+flash_limit_kb*1024) {
    // outside a 256k range, really unlikely to be a valid
    // application, don't jump
    return;
  }
#endif // DISABLE_APP_HEADER_CHECKS

  // app-signature magic, board id and CRC checks
  if (!DroneCAN_boot_ok()) {
    return;
  }

  sys_can_disable_IRQ();

  jump_to_application();
#endif // DISABLE_JUMP
}

int main(void)
{
  bl_clock_config();
  bl_timer_init();
  bl_gpio_init();

  /*
    fast path: if a valid application is already present, boot it
    immediately. jump() returns if the image is not valid.
   */
  jump();

  /*
    no valid app (or we were asked to stay for a firmware update):
    service DroneCAN and retry the jump once the image becomes valid.
   */
  while (1) {
    if (DroneCAN_update()) {
      jump();
    }
  }
}
