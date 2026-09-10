/*
 *----------------------------------------------------------------------
 *    micro T-Kernel 3.0 BSP 2.0
 *
 *    Copyright (C) 2023-2026 by Ken Sakamura.
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 *
 *    Released by TRON Forum(http://www.tron.org) at 2026/04.
 *
 *----------------------------------------------------------------------
 */

/*
 *	config_bsp.h
 *	BSP Configuration Definition (RA FSP)
 */

#ifndef	_MTKBSP_BSP_CONFIG_DEVENV_H_
#define	_MTKBSP_BSP_CONFIG_DEVENV_H_

/* ------------------------------------------------------------------------ */
/*
 * Static allocation of system memory
 *     Enabling this setting statically allocates system memory space as variables.
 */
/* Left at the shipped default (0), which is the BSP2 manual's documented route: the kernel
 * heap runs from the linker symbol __mtk3_SYSMEM_START to the top of SRAM, and that symbol
 * comes from mtk3_bsp2/etc/linker/mtkernel.ld, which must be added as a SECOND linker
 * script (manual section 4.2.2 (5)). mtkernel.ld's RAM region name matches FSP's
 * fsp_gen.lld, so the two scripts compose.
 *
 * History: this was set to (1) with SYSTEM_MEM_SIZE 32 KB earlier in the port, to avoid
 * adding a second linker script under LLVM lld. That built and linked, but the image
 * faulted before reaching usermain() (UsageFault INVSTATE during the first dispatch), and
 * it was the largest deviation from the one configuration proven to work on a Cortex-M85
 * (sample_code/faceDetect_uT_FSP5). Reverted to the documented route.
 * A side benefit: mtkernel.ld marks the section (NOLOAD), which also removes the 34.5 KB
 * of zeros the (1) route put into the downloaded image at 0x22000600. */
#define USE_STATIC_SYS_MEM	(0)		// 1:Valid   0:invalid
#define SYSTEM_MEM_SIZE		(15*1024)	// Memory size to statically allocate. (unused when the above is 0)

/* ------------------------------------------------------------------------ */
/*
 *  System memory area information (For debugging)
 */
#define USE_DEBUG_SYSMEMINFO   (1)		// 1:Valid   0:invalid

/* ------------------------------------------------------------------------ */
/*
 *  Stack pointer monitoring function
 */
#define USE_SPMON		(1)		// 1:Valid   0:invalid

/* ------------------------------------------------------------------------ */
/* Device usage settings
 *	1: Use   0: Do not use
 */
#define DEVCNF_USE_HAL_IIC		0	// I2C communication device (Use IIC )
#define DEVCNF_USE_HAL_SCI_IIC		0	// I2C communication device (Use SCI )
#define DEVCNF_USE_HAL_I3C_IIC		0	// I2C communication device (Use I3C )
#define DEVCNF_USE_HAL_ADC		0	// A/D conversion device (Use ADC12)
#define DEVCNF_USE_HAL_ADHB		0	// A/D conversion device (Use ADC16H)

#endif	/* _MTKBSP_BSP_CONFIG_DEVENV_H_ */
