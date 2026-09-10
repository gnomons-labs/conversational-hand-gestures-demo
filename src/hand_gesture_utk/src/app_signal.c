/**********************************************************************************************************************
 * File Name    : app_signal.c
 * Description  : Stage 1 bodies of the signalling wrappers - FreeRTOS event group.
 *
 * New file. Not copied from sample_code. The Stage 1 call forms it replaced were taken from
 * the vision fork:
 *   xEventGroupSetBits          hand_gesture_five\src\ai_inference_thread_entry.c:118, :122
 *   xEventGroupSetBitsFromISR   hand_gesture_five\src\camera_layer\camera_layer.c:606
 *   xEventGroupWaitBits         hand_gesture_five\src\camera_display_thread_entry.c:163, :174
 *   xEventGroupClearBits        hand_gesture_five\src\camera_display_thread_entry.c:258
 *
 * THE OBJECT IS GENERATED, NOT CREATED HERE. g_ai_app_event is a static FreeRTOS event group
 * built by the generated g_common_init() (ra_gen\common_data.c:681-698), which runs inside
 * g_hal_init() after the scheduler has started and before any task entry function runs
 * during start-up. Nothing in this file creates it, and the configurator's Event Group object
 * must not be removed while Stage 1 lasts.
 *
 * STAGE 2 IS NOW DONE. Every function keeps its name, its arguments and its meaning. The four
 * bodies are now tk_set_flg, tk_set_flg from a handler, tk_wai_flg and tk_clr_flg with the
 * COMPLEMENT of the bits. app_signal.h did not change, and none of the 13 call sites changed.
 *
 * Every kernel name below is confirmed against the vendored kernel:
 *   ER tk_set_flg(ID, UINT setptn)                             syscall.h:744
 *   ER tk_clr_flg(ID, UINT clrptn)                             syscall.h:745
 *   ER tk_wai_flg(ID, UINT waiptn, UINT wfmode, UINT *, TMO)   syscall.h:746
 *   TWF_ANDW 0x00000000U, TWF_BITCLR 0x00000020U               syscall.h:89-92
 *   TMO_FEVR (-1)                                              typedef.h:126
 *********************************************************************************************************************/

#include <stddef.h>

/* was: #include "FreeRTOS.h", "task.h" and "event_groups.h". */
#include <tk/tkernel.h>

/* was: #include "common_data.h", for the generated event group g_ai_app_event.
 * The object is now one micro T-Kernel event flag created by tk_cre_flg in src/app_main.c. */
#include "common_util.h"    /* the EV_* bit names */
#include "app_signal.h"

/**********************************************************************************************************************
 * Private data
 *********************************************************************************************************************/

/* THE OBJECT. Created by tk_cre_flg() in src/app_main.c, inside usermain(), BEFORE the four
 * tasks are created. Defined there and used here.
 *
 * was: the generated FreeRTOS event group g_ai_app_event (ra_gen/common_data.c), which the
 * configurator built inside g_hal_init() after the scheduler had started. That object no
 * longer exists - the Event Group was removed from the configurator with the rest of FreeRTOS.
 *
 * It is <= 0 until tk_cre_flg has run. That matters more than it used to: g_hal_init() now
 * runs in bare-metal main(), BEFORE the kernel and before this object exists, so an interrupt
 * that fires in that window reaches app_sig_set_isr() with no flag to set. */
extern ID g_evt;

/* Bits a FreeRTOS event group cannot hold. The top eight are control bits, and
 * xEventGroupWaitBits asserts if any of them is asked for
 * (ra\aws\FreeRTOS\FreeRTOS\Source\include\event_groups.h, eventEVENT_BITS_CONTROL_BYTES).
 *
 * KEPT UNCHANGED ON PURPOSE. A micro T-Kernel flag pattern is a full 32-bit UINT, so this mask
 * is no longer required - but the highest bit the design uses is EV_BTN2 = (1 << 19), so it
 * costs nothing, it keeps the two stages comparable, and removing it would be a cleanup, which
 * a port does not do. */
#define APP_SIG_LEGAL_BITS    (0x00FFFFFFU)

/* A bit set from an interrupt can be lost, and the loss is counted separately from the wait
 * timeouts so that it is never read as a lost interrupt.
 * Written from an interrupt, read from a task, so both are volatile. Each is a single word,
 * so a reader can never see half of an update on this core. */
static volatile uint32_t       g_sig_drops     = 0U;
static volatile app_sig_bits_t g_sig_drop_bits = 0U;

/* Bits asked for that the object cannot hold. Counted rather than asserted, because a booth
 * demo never stops at a breakpoint. This can only be a programming mistake, and it shows up on
 * the console beside the drop count. */
static volatile uint32_t       g_sig_illegal   = 0U;

/**********************************************************************************************************************
 * Private functions
 *********************************************************************************************************************/

/**********************************************************************************************************************
 * Keep only the bits the event group can hold, and count the rest.
 * @param[in] bits  what the caller asked for
 * @return    the bits that may be passed to FreeRTOS
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
 * was: app_sig_ticks(), returning TickType_t - portMAX_DELAY for APP_SIG_FOREVER, otherwise
 * pdMS_TO_TICKS(timeout_ms). configTICK_RATE_HZ was 1000, so a tick was already a millisecond.
 * A micro T-Kernel TMO is milliseconds by definition, so the conversion disappears entirely and
 * only the "forever" case needs mapping. Same shape, same meaning.
 *
 * @param[in] timeout_ms  milliseconds, or APP_SIG_FOREVER
 * @return    TMO for tk_wai_flg
 *********************************************************************************************************************/
static TMO app_sig_tmo(uint32_t timeout_ms)
{
    if (APP_SIG_FOREVER == timeout_ms)
    {
        return TMO_FEVR;    /* was: portMAX_DELAY */
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
     * caller runs too early. Returning is safer than faulting.
     *
     * was: (NULL == g_ai_app_event). tk_cre_flg returns an ID, so the "does not exist yet"
     * test is now g_evt <= 0. */
    if ((g_evt <= 0) || (0U == safe))
    {
        return;
    }

    /* was: xEventGroupSetBits(g_ai_app_event, (EventBits_t) safe) */
    (void) tk_set_flg(g_evt, (UINT) safe);
}

void app_sig_set_isr(app_sig_bits_t bits)
{
    /* was: BaseType_t higher_priority_task_woken = pdFALSE; and BaseType_t result;
     * Neither has an equivalent - see below. */
    ER             result;
    app_sig_bits_t safe = app_sig_legal(bits);

    /* THIS GUARD IS NOT OPTIONAL and it must not be removed. FSP peripherals are opened by
     * g_hal_init() in bare-metal main(), which now runs BEFORE the kernel and before
     * tk_cre_flg(). Any interrupt that fires in that window arrives here with no object.
     * was: (NULL == g_ai_app_event). */
    if ((g_evt <= 0) || (0U == safe))
    {
        return;
    }

    /* THE BITS ARE SET HERE, IN THE INTERRUPT, IMMEDIATELY.
     *
     * was: xEventGroupSetBitsFromISR(...), which with configUSE_TRACE_FACILITY at 0 was a macro
     * for xTimerPendFunctionCallFromISR - the interrupt did NOT set the bits, it posted a
     * message to the FreeRTOS timer daemon task, which set them later.
     *
     * tk_set_flg is safe from an FSP interrupt callback: it carries no CHECK_DISPATCH() and no
     * context check, only CHECK_FLGID (mtk3_bsp2/mtkernel/kernel/tkernel/eventflag.c:133-141).
     * THE RULE THIS IMPOSES: an interrupt callback may call tk_set_flg and nothing else. */
    result = tk_set_flg(g_evt, (UINT) safe);

    if (E_OK != result)
    {
        /* THE MEANING OF THIS COUNTER HAS CHANGED, and app_signal.h:107-117 already said
         * it would. There is no timer command queue any more, so there is nothing to overflow
         * and BITS CAN NO LONGER BE LOST. g_sig_drops will read 0 for the life of every run -
         * that is expected and correct, not a broken counter.
         *
         * What is counted now is a non-E_OK return from tk_set_flg, which can only be E_ID or
         * E_NOEXS, i.e. a programming mistake. The counter therefore stays honest and the
         * console line it feeds never becomes a lie. */
        g_sig_drops++;
        g_sig_drop_bits |= safe;
    }

    /* There is NO portYIELD_FROM_ISR here and none is needed. Micro T-Kernel's
     * END_CRITICAL_SECTION calls knl_dispatch() when the highest-priority ready task has
     * changed (sysdepend/ra_fsp/cpu/core/armv8m/cpu_status.h:29-34), and knl_dispatch() just
     * sets ICSR.PENDSVSET (cpu_cntl.c:191-194). PendSV runs at the lowest priority, so the
     * switch happens automatically after the interrupt returns. */
}

app_sig_bits_t app_sig_wait(app_sig_bits_t mask, bool clear, uint32_t timeout_ms)
{
    /* was: EventBits_t pattern; - UNINITIALISED, because xEventGroupWaitBits always wrote it.
     * THE = 0 IS NOT OPTIONAL NOW: tk_wai_flg writes *p_flgptn ONLY on the release path
     * (eventflag.c:257) and leaves it untouched on E_TMOUT. Without it, a re-used pattern on a
     * 200 ms vsync timeout would fire a phantom booth reset from one old EV_BTN2. */
    UINT           pattern = 0U;
    ER             result;
    app_sig_bits_t safe    = app_sig_legal(mask);

    if ((g_evt <= 0) || (0U == safe))    /* was: (NULL == g_ai_app_event) */
    {
        return 0U;
    }

    /* TWF_ANDW is the AND-wait, exactly what xWaitForAllBits = pdTRUE was. A mask must
     * therefore never carry two bits that do not arrive together. TWF_BITCLR clears ONLY the
     * waited-for bits on release - flgptn &= ~waiptn
     * (eventflag.c:260-262) - which is exactly what xClearOnExit did.
     * Both return the pattern BEFORE clearing (eventflag.c:257 then :260), as FreeRTOS did.
     *
     * was: xEventGroupWaitBits(g_ai_app_event, safe, clear?pdTRUE:pdFALSE, pdTRUE,
     *                          app_sig_ticks(timeout_ms)); */
    result = tk_wai_flg(g_evt,
                        (UINT) safe,
                        (UINT) (TWF_ANDW | (clear ? TWF_BITCLR : 0U)),
                        &pattern,
                        app_sig_tmo(timeout_ms));

    /* THE TIMEOUT PATH REPORTS DIFFERENTLY, and this is the second half of the pattern = 0
     * point above. FreeRTOS returned a valid pattern even on a timeout; tk_wai_flg returns
     * E_TMOUT and writes nothing. So a non-E_OK return must give the caller 0.
     * app_sig_released(pattern, mask) is (pattern & mask) == mask and a mask is never 0, so
     * the callers' own test keeps working unchanged at both stages - app_signal.h already
     * promises exactly this. */
    if (E_OK != result)
    {
        return 0U;
    }

    return (app_sig_bits_t) pattern;
}

void app_sig_clear(app_sig_bits_t bits)
{
    app_sig_bits_t safe = app_sig_legal(bits);

    if ((g_evt <= 0) || (0U == safe))    /* was: (NULL == g_ai_app_event) */
    {
        return;
    }

    /* THE COMPLEMENT. THIS IS THE EASIEST MISTAKE IN THE WHOLE PORT, IN BOTH DIRECTIONS.
     *
     * was: xEventGroupClearBits(g_ai_app_event, (EventBits_t) safe) - FreeRTOS cleared the
     * bits it was given. tk_clr_flg AND-masks instead: flgcb->flgptn &= clrptn
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
