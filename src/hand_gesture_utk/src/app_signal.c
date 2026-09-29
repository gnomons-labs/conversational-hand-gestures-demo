/**********************************************************************************************************************
 * File Name    : app_signal.c
 * Description  : The bodies of the signalling wrappers, on the micro T-Kernel 3.0 event flag.
 *
 * The four bodies are tk_set_flg, tk_set_flg from a handler, tk_wai_flg, and tk_clr_flg with
 * the COMPLEMENT of the bits.
 *
 * The kernel calls used here:
 *   ER tk_set_flg(ID, UINT setptn)                             syscall.h:744
 *   ER tk_clr_flg(ID, UINT clrptn)                             syscall.h:745
 *   ER tk_wai_flg(ID, UINT waiptn, UINT wfmode, UINT *, TMO)   syscall.h:746
 *   TWF_ANDW 0x00000000U, TWF_BITCLR 0x00000020U               syscall.h:89-92
 *   TMO_FEVR (-1)                                              typedef.h:126
 *********************************************************************************************************************/

#include <stddef.h>

#include <tk/tkernel.h>

#include "common_util.h"    /* the EV_* bit names */
#include "app_signal.h"

/**********************************************************************************************************************
 * Private data
 *********************************************************************************************************************/

/* THE OBJECT. Created by tk_cre_flg() in src/app_main.c, inside usermain(), BEFORE the four
 * tasks are created. Defined there and used here.
 *
 * It is <= 0 until tk_cre_flg has run, and that matters: g_hal_init() runs in bare-metal
 * main(), BEFORE the kernel and before this object exists, so an interrupt that fires in that
 * window reaches app_sig_set_isr() with no flag to set. */
extern ID g_evt;

/* The bits the design allows. A micro T-Kernel flag pattern is a full 32-bit UINT, so this is
 * not a kernel limit, it is a design limit: the highest bit the design uses is
 * EV_BTN2 = (1 << 19), so anything above (1 << 23) can only be a programming mistake. */
#define APP_SIG_LEGAL_BITS    (0x00FFFFFFU)

/* A failed set from an interrupt is counted separately from the wait timeouts so that it is
 * never read as a lost interrupt.
 * Written from an interrupt, read from a task, so both are volatile. Each is a single word,
 * so a reader can never see half of an update on this core. */
static volatile uint32_t       g_sig_drops     = 0U;
static volatile app_sig_bits_t g_sig_drop_bits = 0U;

/* Bits asked for that the design does not allow. Counted rather than asserted, so the demo
 * keeps running. This can only be a programming mistake, and it shows up on the console beside
 * the failure count. */
static volatile uint32_t       g_sig_illegal   = 0U;

/**********************************************************************************************************************
 * Private functions
 *********************************************************************************************************************/

/**********************************************************************************************************************
 * Keep only the bits the event flag may hold, and count the rest.
 * @param[in] bits  what the caller asked for
 * @return    the bits that may be passed to the kernel
 *********************************************************************************************************************/
static app_sig_bits_t app_sig_legal(app_sig_bits_t bits)
{
    if (0U != (bits & ~APP_SIG_LEGAL_BITS))
    {
        g_sig_illegal++;
    }

    return (bits & APP_SIG_LEGAL_BITS);
}

/**********************************************************************************************************************
 * Turn a millisecond timeout into a micro T-Kernel TMO.
 *
 * A micro T-Kernel TMO is milliseconds by definition, so only the "forever" case needs
 * mapping.
 *
 * @param[in] timeout_ms  milliseconds, or APP_SIG_FOREVER
 * @return    TMO for tk_wai_flg
 *********************************************************************************************************************/
static TMO app_sig_tmo(uint32_t timeout_ms)
{
    if (APP_SIG_FOREVER == timeout_ms)
    {
        return TMO_FEVR;
    }

    return (TMO) timeout_ms;
}

/**********************************************************************************************************************
 * Public functions
 *********************************************************************************************************************/

void app_sig_set(app_sig_bits_t bits)
{
    app_sig_bits_t safe = app_sig_legal(bits);

    /* The object exists before any task entry function runs, so this can only fail if a
     * caller runs too early. Returning is safer than faulting. tk_cre_flg returns an ID, so
     * the "does not exist yet" test is g_evt <= 0. */
    if ((g_evt <= 0) || (0U == safe))
    {
        return;
    }

    (void) tk_set_flg(g_evt, (UINT) safe);
}

void app_sig_set_isr(app_sig_bits_t bits)
{
    ER             result;
    app_sig_bits_t safe = app_sig_legal(bits);

    /* THIS GUARD IS NOT OPTIONAL and it must not be removed. FSP peripherals are opened by
     * g_hal_init() in bare-metal main(), which runs BEFORE the kernel and before tk_cre_flg().
     * Any interrupt that fires in that window arrives here with no object. */
    if ((g_evt <= 0) || (0U == safe))
    {
        return;
    }

    /* THE BITS ARE SET HERE, IN THE INTERRUPT, IMMEDIATELY.
     *
     * tk_set_flg is safe from an FSP interrupt callback: it carries no CHECK_DISPATCH() and no
     * context check, only CHECK_FLGID (mtk3_bsp2/mtkernel/kernel/tkernel/eventflag.c:133-141).
     * THE RULE THIS IMPOSES: an interrupt callback may call tk_set_flg and nothing else. */
    result = tk_set_flg(g_evt, (UINT) safe);

    if (E_OK != result)
    {
        /* A non-E_OK return from tk_set_flg can only be E_ID or E_NOEXS, i.e. a programming
         * mistake, so g_sig_drops normally reads 0 for the life of a run. That is expected and
         * correct, not a broken counter. */
        g_sig_drops++;
        g_sig_drop_bits |= safe;
    }

    /* NO EXPLICIT YIELD IS NEEDED HERE. Micro T-Kernel's END_CRITICAL_SECTION calls
     * knl_dispatch() when the highest-priority ready task has changed (sysdepend/ra_fsp/cpu/core/armv8m/cpu_status.h:29-34), and knl_dispatch() just
     * sets ICSR.PENDSVSET (cpu_cntl.c:191-194). PendSV runs at the lowest priority, so the
     * switch happens by itself after the interrupt returns. */
}

app_sig_bits_t app_sig_wait(app_sig_bits_t mask, bool clear, uint32_t timeout_ms)
{
    /* THE = 0 IS NOT OPTIONAL: tk_wai_flg writes *p_flgptn ONLY on the release path
     * (eventflag.c:257) and leaves it untouched on E_TMOUT. Without it, a re-used pattern on a
     * 200 ms vsync timeout would fire a phantom reset from one old EV_BTN2. */
    UINT           pattern = 0U;
    ER             result;
    app_sig_bits_t safe    = app_sig_legal(mask);

    if ((g_evt <= 0) || (0U == safe))
    {
        return 0U;
    }

    /* TWF_ANDW is the AND-wait: every bit in the mask must be set to release it, so a mask must
     * never carry two bits that do not arrive together. TWF_BITCLR clears ONLY the waited-for
     * bits on release - flgptn &= ~waiptn (eventflag.c:260-262). Both return the pattern
     * BEFORE clearing (eventflag.c:257 then :260). */
    result = tk_wai_flg(g_evt,
                        (UINT) safe,
                        (UINT) (TWF_ANDW | (clear ? TWF_BITCLR : 0U)),
                        &pattern,
                        app_sig_tmo(timeout_ms));

    /* THE TIMEOUT PATH, and the second half of the pattern = 0 point above. tk_wai_flg returns
     * E_TMOUT and writes nothing, so a non-E_OK return must give the caller 0.
     * app_sig_released(pattern, mask) is (pattern & mask) == mask and a mask is never 0, so the
     * callers' own test keeps working - app_signal.h promises exactly this. */
    if (E_OK != result)
    {
        return 0U;
    }

    return (app_sig_bits_t) pattern;
}

void app_sig_clear(app_sig_bits_t bits)
{
    app_sig_bits_t safe = app_sig_legal(bits);

    if ((g_evt <= 0) || (0U == safe))
    {
        return;
    }

    /* THE COMPLEMENT. THIS IS THE EASIEST MISTAKE IN THIS FILE.
     *
     * tk_clr_flg AND-masks: flgcb->flgptn &= clrptn
     * (mtk3_bsp2/mtkernel/kernel/tkernel/eventflag.c:204). Passing `safe` here would clear
     * every OTHER bit in the flag and keep the ones the caller asked to clear - the exact
     * opposite of what this function means.
     *
     * The rule app_signal.h states at the top of the file is unchanged: the CALLER always
     * passes the bits to clear, and only this file knows which way round the kernel wants
     * them. */
    (void) tk_clr_flg(g_evt, (UINT) ~safe);
}

uint32_t app_sig_drops_get(void)
{
    return g_sig_drops;
}

app_sig_bits_t app_sig_drop_bits_get(void)
{
    return g_sig_drop_bits;
}

const char * app_sig_bit_name(app_sig_bits_t single_bit)
{
    switch (single_bit)
    {
        case EV_INIT_DISPLAY:   return "EV_INIT_DISPLAY";
        case EV_INIT_CAMERA:    return "EV_INIT_CAMERA";
        case EV_INIT_NPU:       return "EV_INIT_NPU";
        case EV_INIT_STORY:     return "EV_INIT_STORY";
        case EV_VSYNC:          return "EV_VSYNC";
        case EV_CAM_FRAME:      return "EV_CAM_FRAME";
        case EV_AI_INPUT_READY: return "EV_AI_INPUT_READY";
        case EV_AI_RESULT:      return "EV_AI_RESULT";
        case EV_STORY_START:    return "EV_STORY_START";
        case EV_STORY_TEXT:     return "EV_STORY_TEXT";
        case EV_STORY_DONE:     return "EV_STORY_DONE";
        case EV_BTN1:           return "EV_BTN1";
        case EV_BTN2:           return "EV_BTN2";
        default:                return "?";
    }
}
