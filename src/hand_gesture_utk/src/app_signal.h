/**********************************************************************************************************************
 * File Name    : app_signal.h
 * Description  : The one place the demo talks to the operating system's signalling object.
 *
 * New file. Not copied from sample_code. The call forms inside app_signal.c are taken from the
 * vision fork (output_code\hand_gesture_five\src\camera_display_thread_entry.c:121, :174, :258,
 * src\ai_inference_thread_entry.c:114, :118, :126, src\camera_layer\camera_layer.c:606,
 * src\display_layer\display_layer.c:179, src\common_util.c:285).
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * The signalling call sites are the largest item of the Stage 2 port: four task loops, five
 * interrupt callbacks and the once-per-frame observed-bit pass. Every one of them calls the
 * four functions below and nothing else. So the port to micro T-Kernel 3.0 rewrites ONE file,
 * app_signal.c, and no task, no callback and no screen file changes.
 *
 * THE RULES THIS INTERFACE HIDES
 * ------------------------------
 * 1. Clearing. FreeRTOS clears the bits you name; micro T-Kernel AND-masks, so it needs the
 *    complement. app_sig_clear() always takes THE BITS TO CLEAR. Only app_signal.c ever knows
 *    which way round the underlying call wants them. This is the easiest mistake in the port,
 *    in both directions.
 * 2. Timeouts. A timed-out FreeRTOS wait still returns a valid bit pattern, so a timeout has
 *    no error code. app_sig_released() is the one test for "the wait was released", and it is
 *    true at both stages.
 * 3. Width. A FreeRTOS event group has 24 usable bits, not 32. No bit above (1 << 23) may ever
 *    be used. The highest bit the design uses is EV_BTN2 = (1 << 19).
 * 5. Dropped bits. A bit set from an interrupt can be LOST at Stage 1. app_sig_set_isr()
 *    counts every drop, in one place, so a drop is never read as a lost interrupt.
 * (Difference 4, the stack-overflow hook, is not a signalling matter and is not handled here.)
 *
 * The bit names themselves are EV_* in src\common_util.h.
 *********************************************************************************************************************/

#ifndef APP_SIGNAL_H_
#define APP_SIGNAL_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One or more EV_* bits from src\common_util.h.
 * A plain integer type on purpose: no caller has to include FreeRTOS or the kernel to
 * name a signal, so no call site changes at Stage 2. */
typedef uint32_t app_sig_bits_t;

/* Wait for ever. Stage 1 maps it to portMAX_DELAY, Stage 2 to TMO_FEVR. */
#define APP_SIG_FOREVER    (0xFFFFFFFFU)

/**********************************************************************************************************************
 * Set bits from a task.
 * @param[in] bits  one or more EV_* bits. Bits above (1 << 23) are refused and counted.
 *********************************************************************************************************************/
void app_sig_set(app_sig_bits_t bits);

/**********************************************************************************************************************
 * Set bits from an interrupt callback, and ask for a context switch if one is due.
 *
 * At Stage 1 this does NOT set the bits in the interrupt. It posts a message to the FreeRTOS
 * timer daemon task, which sets them later, so that task's priority is part of the design
 * - it must sit above all four application tasks. When the timer command queue is full the
 * bits are LOST; the loss is counted here and only here.
 *
 * @param[in] bits  one or more EV_* bits.
 *********************************************************************************************************************/
void app_sig_set_isr(app_sig_bits_t bits);

/**********************************************************************************************************************
 * Block until every bit in mask is set, or until the timeout runs out.
 *
 * @param[in] mask        the bits waited for. All of them must be set to release the wait
 *                        (an AND-wait, TWF_ANDW at Stage 2). Never pass 0.
 * @param[in] clear       true clears exactly the bits in mask on release, and nothing else.
 * @param[in] timeout_ms  milliseconds, or APP_SIG_FOREVER.
 * @return    the bit pattern reported by the wait. Test it with app_sig_released(); do not
 *            test it against zero. Other bits in the returned pattern are the once-per-frame
 *            observed bits T_UI reads without waiting, and are only to be acted on when
 *            app_sig_released() is true.
 *********************************************************************************************************************/
app_sig_bits_t app_sig_wait(app_sig_bits_t mask, bool clear, uint32_t timeout_ms);

/**********************************************************************************************************************
 * Clear bits.
 * @param[in] bits  THE BITS TO CLEAR, never their complement. See rule 1 at the top of this file.
 *********************************************************************************************************************/
void app_sig_clear(app_sig_bits_t bits);

/**********************************************************************************************************************
 * Was the wait released, or did it time out?
 *
 * The one test for it, because the two operating systems report a timeout differently.
 * Stage 1: a timed-out wait returns the group's current bits, so the mask is tested. Stage 2:
 * app_sig_wait returns 0 on E_TMOUT and the same test still holds, because a mask is never 0.
 *
 * @param[in] pattern  what app_sig_wait returned.
 * @param[in] mask     the mask that was passed to app_sig_wait.
 * @return    true when the wait was released.
 *********************************************************************************************************************/
static inline bool app_sig_released(app_sig_bits_t pattern, app_sig_bits_t mask)
{
    return ((pattern & mask) == mask);
}

/**********************************************************************************************************************
 * Failure reporting. These three are not signalling calls; they exist because a dropped bit
 * has to be counted separately from the wait timeouts and printed with the bit name. They
 * compile unchanged at Stage 2, where tk_set_flg cannot fail and the count therefore stays 0.
 *********************************************************************************************************************/

/* How many times app_sig_set_isr could not deliver its bits. */
uint32_t app_sig_drops_get(void);

/* Every bit that has ever been dropped, OR-ed together. 0 when nothing was dropped. */
app_sig_bits_t app_sig_drop_bits_get(void);

/* Name of ONE bit, for the console line. Returns "?" for 0, for more than one bit, and for
 * any bit the design does not use. Upper-case ASCII, so the same string may go on the screen,
 * where all text is plain upper-case ASCII. */
const char * app_sig_bit_name(app_sig_bits_t single_bit);

#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_H_ */
