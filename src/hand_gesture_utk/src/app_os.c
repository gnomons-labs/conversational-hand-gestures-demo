/**********************************************************************************************************************
 * File Name    : app_os.c
 * Description  : Delay and millisecond clock. See app_os.h.
 *
 * NEW FILE, added by the FreeRTOS -> micro T-Kernel 3.0 BSP2 port.
 *
 * API names confirmed against the vendored kernel:
 *   ER tk_dly_tsk(RELTIM dlytim)   mtk3_bsp2\mtkernel\include\tk\syscall.h:718
 *   ER tk_get_otm(SYSTIM *pk_tim)  syscall.h:787
 *   RELTIM is UW; SYSTIM is { W hi; UW lo; }  include\tk\typedef.h:108, :110-113
 *********************************************************************************************************************/

#include <tk/tkernel.h>

#include "app_os.h"

void app_os_delay_ms(uint32_t ms)
{
    /* was: vTaskDelay(pdMS_TO_TICKS(ms)).
     * CNF_TIMER_PERIOD is 1, so a kernel tick is 1 ms, exactly as configTICK_RATE_HZ 1000 was.
     * tk_dly_tsk takes milliseconds directly, so no conversion macro is needed. */
    (void) tk_dly_tsk((RELTIM) ms);
}

uint32_t app_os_millis(void)
{
    /* was: xTaskGetTickCount() */
    SYSTIM tim;

    if (tk_get_otm(&tim) < E_OK)
    {
        return 0U;
    }

    /* Only the low word is needed: the single caller uses it as an RNG seed. */
    return (uint32_t) tim.lo;
}
