/**********************************************************************************************************************
 * File Name    : app_signal.h
 * Description  : The one place the demo talks to the operating system's signalling object.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * The signalling call sites are spread widely: four task loops, five interrupt callbacks and
 * the once-per-frame observed-bit pass. Every one of them calls the four functions below and
 * nothing else, so the kernel's event-flag calls appear in ONE file, app_signal.c. No task, no
 * callback and no screen file names a kernel object.
 *
 * THE RULES THIS INTERFACE HIDES
 * ------------------------------
 * 1. Clearing. micro T-Kernel AND-masks the pattern it is given, so it needs the complement.
 *    app_sig_clear() always takes THE BITS TO CLEAR. Only app_signal.c ever knows which way
 *    round the underlying call wants them.
 * 2. Timeouts. app_sig_released() is the one test for "the wait was released", because a
 *    timeout is not reported to the caller as an error code.
 * 3. Width. The highest bit the design uses is EV_BTN2 = (1 << 19). No bit above (1 << 23) may
 *    ever be used; such a bit is refused and counted.
 * 4. Failed sets. A set from an interrupt is counted when it fails, in one place, so it is
 *    never read as a lost interrupt.
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
 * A plain integer type on purpose: no caller has to include the kernel headers to name a
 * signal. */
typedef uint32_t app_sig_bits_t;

/* Wait for ever. app_signal.c maps it to the kernel's TMO_FEVR. */
#define APP_SIG_FOREVER    (0xFFFFFFFFU)

/**********************************************************************************************************************
 * Set bits from a task.
 * @param[in] bits  one or more EV_* bits. Bits above (1 << 23) are refused and counted.
 *********************************************************************************************************************/
void app_sig_set(app_sig_bits_t bits);

/**********************************************************************************************************************
 * Set bits from an interrupt callback.
 *
 * The bits are set in the interrupt itself, immediately. A failed set is counted here and only
 * here, so it is never read as a lost interrupt.
 *
 * @param[in] bits  one or more EV_* bits.
 *********************************************************************************************************************/
void app_sig_set_isr(app_sig_bits_t bits);

/**********************************************************************************************************************
 * Block until every bit in mask is set, or until the timeout runs out.
 *
 * @param[in] mask        the bits waited for. All of them must be set to release the wait
 *                        (an AND-wait). Never pass 0.
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
 * The one test for it. app_sig_wait returns 0 when the wait timed out, and a mask is never 0,
 * so testing the mask is enough.
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
 * Failure reporting. These three are not signalling calls; they exist because a failed set has
 * to be counted separately from the wait timeouts and printed with the bit name. Once the event
 * flag exists the set cannot fail, so the count normally stays 0.
 *********************************************************************************************************************/

/* How many times app_sig_set_isr could not deliver its bits. Normally 0. */
uint32_t app_sig_drops_get(void);

/* Every bit whose set ever failed, OR-ed together. 0 when nothing failed. */
app_sig_bits_t app_sig_drop_bits_get(void);

/* Name of ONE bit, for the console line. Returns "?" for 0, for more than one bit, and for
 * any bit the design does not use. Upper-case ASCII, so the same string may go on the screen,
 * where all text is plain upper-case ASCII. */
const char * app_sig_bit_name(app_sig_bits_t single_bit);

#ifdef __cplusplus
}
#endif

#endif /* APP_SIGNAL_H_ */
