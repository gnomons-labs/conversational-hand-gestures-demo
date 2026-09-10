/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.0 BSP 2.0
 *
 *    Copyright (C) 2024-2026 by Ken Sakamura.
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 *
 *    Released by TRON Forum(http://www.tron.org) at 2026/04.
 *
 *----------------------------------------------------------------------
 */

#include <sys/machine.h>
#if defined(MTKBSP_RAFSP) && defined(MTKBSP_CPU_CORE_ARMV8M)

/*
 *	sys_start.c (RA FSP & ARMv8-M)
 *	Kernel start routine 
 */
#include <tk/tkernel.h>
#include <kernel.h>
#include "sysdepend.h"

#include <cmsis_gcc.h>

/* Exception handler table (RAM) */
EXPORT UW knl_exctbl[sizeof(UW)*(N_SYSVEC + N_INTVEC)]
	__attribute__((section(".mtk_exctbl"))) __attribute__ ((aligned(EXCTBL_ALIGN)));

EXPORT UW *knl_exctbl_o;	// Exception handler table (Origin)

EXPORT void		*knl_lowmem_top;	// Head of area (Low address)
EXPORT void		*knl_lowmem_limit;	// End of area (High address)

/* Start address of free space in RAM */
IMPORT const void	*__mtk3_SYSMEM_START;
#define	SYSMEM_TOP	(__mtk3_SYSMEM_START)

#if USE_STATIC_SYS_MEM
EXPORT UW knl_system_mem[SYSTEM_MEM_SIZE/sizeof(UW)] __attribute__((section(".mtk_sysmem")));
#endif

#if USE_DEBUG_SYSMEMINFO
EXPORT void		*knl_sysmem_top	= 0;
EXPORT void		*knl_sysmem_end	= 0;
#endif

/* ------------------------------------------------------------------------ */
/* NOT in the original mtk3_bsp2 source. Added for this port.
 *
 * knl_exctbl is the relocated exception table and it lives in SRAM. On this
 * device the Cortex-M85 data cache is enabled and write-back
 * (BSP_CFG_DCACHE_ENABLED 1 and BSP_CFG_DCACHE_FORCE_WRITETHROUGH 0 in
 * ra_cfg/fsp_cfg/bsp/bsp_mcu_family_cfg.h; confirmed on the board by CCR bit
 * DC = 1). The stores that fill knl_exctbl therefore stay in the data cache
 * and are not in memory yet. The processor's vector fetch does not see them,
 * reads the stale SRAM contents, and gets a word whose bit 0 is clear. The
 * core then takes UsageFault INVSTATE together with BusFault IBUSERR, the
 * fault escalates through HardFault, and the core locks up (PC = 0xEFFFFFFE)
 * before any handler runs. Measured on hardware: SRAM held the correct table
 * while the core had fetched 0x00000000 for PendSV and 0x00F0F000 for
 * UsageFault.
 *
 * The source project did not hit this because FreeRTOS keeps FSP's vector
 * table in MRAM and never relocates it.
 *
 * Fix: clean the cache lines that cover knl_exctbl to the point of coherency,
 * then DSB and ISB. Called after every write to the table.
 * DCCMVAC = "Clean data cache line by address to PoC", in the Armv8-M System
 * Control Block. Line size is 64 bytes on Cortex-M85; a 32-byte step is used
 * so the loop is correct for either size (it just visits a line twice).
 */
#define SCB_DCCMVAC		0xE000EF68
#define EXCTBL_CACHE_LINE	32

EXPORT void knl_flush_exctbl(void)
{
	UW	adr = (UW)knl_exctbl & ~(UW)(EXCTBL_CACHE_LINE - 1);
	UW	end = (UW)knl_exctbl + sizeof(knl_exctbl);

	Asm("dsb 0xf" : : : "memory");
	while(adr < end) {
		out_w(SCB_DCCMVAC, adr);
		adr += EXCTBL_CACHE_LINE;
	}
	Asm("dsb 0xf" : : : "memory");
	Asm("isb 0xf" : : : "memory");
}

EXPORT void knl_start_mtkernel(void)
{
	UW	*src, *top;
	UW	reg;
	INT	i;

	disint();		// Disable Interrupt

	knl_startup_hw();

	/* Copy exception handler (ROM -> RAM) */
	src = knl_exctbl_o = (UW*)in_w(SCB_VTOR);
	top = (UW*)knl_exctbl;
	for(i=0; i < (N_SYSVEC + N_INTVEC); i++) {
		*top++ = *src++;
	}
	knl_flush_exctbl();	/* added for this port - see knl_flush_exctbl() */
	out_w(SCB_VTOR, (UW)knl_exctbl);

	/* Configure exception priorities */
	reg = *(_UW*)SCB_AIRCR;
	reg = (reg & (~AIRCR_PRIGROUP3)) | AIRCR_PRIGROUP0;	// PRIGRP:SUBPRI = 4 : 4
	*(_UW*)SCB_AIRCR = (reg & 0x0000FFFF) | AIRCR_VECTKEY;

	/* Enable UsageFault & BusFault & MemFault */
	out_w(SCB_SHCSR, SHCSR_USGFAULTENA | SHCSR_BUSFAULTENA | SHCSR_MEMFAULTENA);

	out_w(SCB_SHPR2, SCB_SHPR2_VAL);			// SVC pri = 0
	out_w(SCB_SHPR3, SCB_SHPR3_VAL);			// SysTick = 1 , PendSV = 15

#if USE_IMALLOC
#if USE_STATIC_SYS_MEM
	knl_lowmem_top = knl_system_mem;
	knl_lowmem_limit = &knl_system_mem[SYSTEM_MEM_SIZE/sizeof(UW)];
#else
	/* Set System memory area */
	if(INTERNAL_RAM_START > SYSTEMAREA_TOP) {
		knl_lowmem_top = (UW*)INTERNAL_RAM_START;
	} else {
		knl_lowmem_top = (UW*)SYSTEMAREA_TOP;
	}
	if((UW)knl_lowmem_top < (UW)&SYSMEM_TOP) {
		knl_lowmem_top = (UW*)&SYSMEM_TOP;
	}

	if((SYSTEMAREA_END != 0) && (INTERNAL_RAM_END > CNF_SYSTEMAREA_END)) {
		knl_lowmem_limit = (UW*)(SYSTEMAREA_END - EXC_STACK_SIZE);
	} else {
		knl_lowmem_limit = (UW*)(INTERNAL_RAM_END - EXC_STACK_SIZE);
	}
#endif

#if USE_DEBUG_SYSMEMINFO
	knl_sysmem_top	= knl_lowmem_top;
	knl_sysmem_end	= knl_lowmem_limit;
#endif	// USE_DEBUG_MEMINFO
#endif	// USE_IMALLOC

	/* Temporarily disable stack pointer protection */
	Asm ("msr msplim, %0" : : "r" ((uint32_t)INTERNAL_RAM_START));

	/* Startup Kernel */
	knl_main();		// *** No return ****/
	while(1);		// guard - infinite loops
}

#endif	/* defined(MTKBSP_RAFSP) && defined(MTKBSP_CPU_CORE_ARMV8M) */
