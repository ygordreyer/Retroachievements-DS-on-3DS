@---------------------------------------------------------------------------------
@ Entry point for cardenginei_arm9_ra, the RetroAchievements code that does not fit
@ in the ARM9 cardengine's 12K window. Loaded into DSi WRAM and called once per
@ frame; see docs/retroachievements.md.
@
@ The first branch preserves the colour LUT loader contract. Flags at +4 are set by
@ the loader; RAO1 at +12 identifies the paused offer entry at +8.
@---------------------------------------------------------------------------------
	.section ".init"
@---------------------------------------------------------------------------------
	.align	4
	.arm

card_engine_start:

	b ra_wram_tick

.global raWramFlags
raWramFlags:
.word 0

	b ra_wram_offer
.word 0x314F4152 @ RAO1

.pool

card_engine_end:
