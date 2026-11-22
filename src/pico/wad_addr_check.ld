/* Implicit linker script (augments the SDK's default one): fail the link if the program
 * would overlap the WHD/WHX data which is loaded separately at TINY_WAD_ADDR. */
ASSERT(__flash_binary_end <= DOOM_WAD_ADDR, "Doom binary overlaps the WAD data at TINY_WAD_ADDR - it must shrink or the WAD must move")
