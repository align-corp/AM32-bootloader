# Application requirements for the DroneCAN-only bootloader

This bootloader (board target `M460_PCU`, STM32L431, bxCAN) does **not** speak the
AM32 serial protocol and does **not** manage any settings/EEPROM page. It only:

1. on reset, validates the application image and **jumps to it immediately** if valid;
2. otherwise stays resident and accepts a **DroneCAN firmware update**
   (`uavcan.protocol.file.BeginFirmwareUpdate` + `uavcan.protocol.file.Read`).

For your application firmware to be accepted and booted, it must meet the
requirements below. The authoritative source is `bootloader/main.c` (`jump()`) and
`bootloader/DroneCAN/DroneCAN.c` (`DroneCAN_boot_ok()`); constants here are quoted
from that code.

---

## 1. Flash memory layout

| Region        | Address range               | Size   | Owner |
|---------------|-----------------------------|--------|-------|
| Bootloader    | `0x08000000` – `0x08003FFF` | 16 KB  | bootloader — **do not erase/overwrite** |
| Application   | `0x08004000` – `0x0801FFFF` | 112 KB | application |

- The application **must be linked to start at `0x08004000`**
  (`MAIN_FW_START_ADDR` / `FIRMWARE_RELATIVE_START = 0x4000`).
- The bootloader scans / accepts an image of at most **`(128-18)*1024 = 110 KB`**
  (`app_max_len`). So the application **image must be ≤ 110 KB**, and the application
  signature (below) **must lie within the first 110 KB** of the image.
- The application must not write into the bootloader region. The last 2 KB page
  (`0x0801F800`) is no longer used by the bootloader and is free for the application
  to use as its own storage if desired.

## 2. Vector table / image header

On reset the bootloader reads the first two words at `0x08004000` and refuses to jump
unless both are sane (`jump()` in `bootloader/main.c`):

- **word[0] — initial stack pointer** must be in RAM:
  `0x20000000 … 0x20010000` (64 KB, the L431 SRAM).
- **word[1] — reset/entry vector** must be in
  `0x08004000 … 0x08044000` (a 256 KB window from the app base).

A normal Cortex-M vector table linked at `0x08004000` satisfies both. The bootloader
sets `SCB->VTOR = 0x08004000` before jumping, but the application should still
configure `VTOR` itself early in startup.

## 3. Application signature descriptor (required)

The bootloader locates and validates an **application signature** embedded in the
image. Without a valid signature it will never jump (it stays in update mode). The
struct (`struct app_signature` in `DroneCAN.c`) is 44 bytes:

```c
struct app_signature {
    uint32_t magic1;    // 0x68f058e6
    uint32_t magic2;    // 0xafcee5a0
    uint32_t fwlen;     // total image length in bytes  (filled in post-link)
    uint32_t crc1;      // crc32 of image *before* this struct (filled in post-link)
    uint32_t crc2;      // crc32 of image *after*  this struct (filled in post-link)
    char     mcu[16];   // must match BOARD_SIGNATURE_ID  ("L431")
    uint32_t unused[2];
};
```

Validation performed by `DroneCAN_boot_ok()`:

1. **Magic** — the image is scanned (`memmem`) for the two words
   `magic1 = 0x68f058e6`, `magic2 = 0xafcee5a0`
   (defined per-board in `Inc/targets.h` as `APP_SIGNATURE_MAGIC1/2`).
   The descriptor must be **4-byte aligned**.
2. **Length** — `fwlen` must be ≤ 110 KB (`app_max_len`).
3. **Board id** — the first `strlen("L431")` bytes of `mcu[]` must equal
   `BOARD_SIGNATURE_ID` (= `"L431"`, set in `Inc/targets.h`). Put `"L431"` in `mcu[]`.
4. **CRC** (enabled by default, `DRONECAN_CHECK_SIGNATURE = 1`):
   - `crc1 = crc32(image bytes before the descriptor)`
   - `crc2 = crc32(image bytes after the 44-byte descriptor, up to fwlen)`
   - both must match the stamped values.

### How the application embeds it

1. **Declare the struct** in its own section, with magics and MCU string pre-filled
   and `fwlen/crc1/crc2` left as 0 (they are stamped after linking):

   ```c
   const struct {
     uint32_t magic1, magic2, fwlen, crc1, crc2;
     char     mcu[16];
     uint32_t unused[2];
   } app_signature __attribute__((section(".app_signature"), used)) = {
     .magic1 = 0x68f058e6,
     .magic2 = 0xafcee5a0,
     .mcu    = "L431",
   };
   ```

2. **Place `.app_signature`** in the application linker script, inside the app flash
   region and within the first 110 KB (typically right after the vector table).

3. **Stamp `fwlen/crc1/crc2` post-link** using the same algorithm the bootloader uses.
   This repo ships `bl_update/set_app_signature.py <app.bin> <app.elf>`, which:
   - finds the magics in the `.bin`,
   - computes `crc1` over everything before the descriptor and `crc2` over everything
     after the 44-byte descriptor,
   - writes `fwlen` (= total `.bin` length), `crc1`, `crc2` into both `.bin` and `.elf`.

   **Caveat:** that script early-exits unless the bin **filename contains `_CAN_`**
   (`set_app_signature.py:74`) and ends in `.bin`. Either name your artifact
   accordingly (e.g. `..._CAN_....bin`) or drop that guard in your own build.

### CRC algorithm

Standard reflected CRC-32 (poly `0xEDB88320`, init `0`, no final XOR) — identical in
`DroneCAN.c` (`crc32()`) and `set_app_signature.py` (`crc32()`). Use that exact
routine if you stamp the image yourself.

## 4. Bring-up shortcut

To get booting before the CRC tooling is wired up, build the **bootloader** with
`-DDRONECAN_CHECK_SIGNATURE=0`. Then only the magics + `mcu[]` match are enforced and
you can skip the `fwlen/crc1/crc2` stamping step. Re-enable it for production.

## 5. Triggering a firmware update from the application

The bootloader boots a valid app immediately, so the application is responsible for
asking to stay in the bootloader for an update. It does this via the **RTC backup
registers** (read in `DroneCAN.c`, marker in `bootloader/DroneCAN/sys_can.h`):

- **Backup register 0**: low 16 bits = `RTC_BKUP0_FWUPDATE` (`0x42c7`).
  Optional: `bits[31:24]` = preferred CAN node id, `bits[23:16]` = file-server node id.
- **Backup registers 1 & 2** (optional): the 8-byte firmware file path, to start the
  update immediately from a known server/path.

Sequence in the application:
1. Enable the backup domain / PWR backup access and write the register(s) above.
2. Issue a software reset (`NVIC_SystemReset()`).

On the next boot the bootloader sees the marker, **refuses to jump**, and waits for /
starts a DroneCAN firmware update. Writing only `0x42c7` to register 0 (no node/path)
is enough to make it stay and wait for a `BeginFirmwareUpdate` request.

> Note: the application's own node id is **not** taken from flash/EEPROM anymore. With
> no forced-update marker the bootloader uses DroneCAN **dynamic node allocation
> (DNA)** for its own node id; this does not constrain the application.

## 6. Clock & hardware notes

- The bootloader runs from the **internal oscillator (HSI)**. The application must set
  up its own clock tree on entry (the bootloader disables its timer before jumping).
- HSI is fine for the DroneCAN file-update transfer, but for reliable CAN operation an
  **external crystal is recommended** at the board level.

## 7. What the application must NOT assume

- No serial bootloader protocol exists — there is no comms pin.
- The bootloader does **not** initialize, validate, or populate any settings/EEPROM
  page. If the application needs persistent storage it must manage it itself.
- Do not place application data in, or erase, the first 16 KB (bootloader region).

---

## Quick checklist

- [ ] Linked at `0x08004000`, image ≤ 110 KB.
- [ ] Valid Cortex-M vector table (SP in SRAM, reset vector in app flash) at base.
- [ ] `.app_signature` struct embedded, within first 110 KB, 4-byte aligned,
      `magic1/magic2` set, `mcu[] = "L431"`.
- [ ] `fwlen/crc1/crc2` stamped post-link (or bootloader built with
      `DRONECAN_CHECK_SIGNATURE=0` during bring-up).
- [ ] Application configures its own clocks on startup.
- [ ] (If self-update needed) writes `RTC_BKUP0_FWUPDATE` to backup reg 0 and resets.
- [ ] Never erases/writes the bootloader region.
