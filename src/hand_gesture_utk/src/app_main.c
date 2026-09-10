/**********************************************************************************************************************
 * File Name    : app_main.c
 * Description  : usermain() - creates every kernel object and starts the four tasks.
 *
 * NEW FILE, added by the FreeRTOS -> micro T-Kernel 3.0 BSP2 port.
 *
 * WHAT THIS REPLACES.
 * The whole generated FreeRTOS start-up:
 *   ra_gen\main.c            created a counting semaphore, called the four *_thread_create()
 *                            functions, then vTaskStartScheduler().
 *   ra_gen\cam_thread.c and the three others held one xTaskCreateStatic() each, plus a static
 *                            stack array in section .stack.<name> and a static TCB.
 *   rtos_startup_common_init() called g_hal_init() INSIDE the first thread to run, guarded by
 *                            that semaphore so only one thread did it.
 * All of it is gone. FSP generates none of it with BSP_CFG_RTOS 0.
 *
 * THE NEW ORDER OF EVENTS.
 *   ra_gen\main.c  ->  g_hal_init()            FSP peripherals, now BEFORE the kernel
 *                  ->  hal_entry()             src\hal_entry.c
 *                  ->  knl_start_mtkernel()    the kernel starts its own initial task
 *                  ->  usermain()              THIS FILE
 *
 * usermain() runs on the kernel initial task at INITTASK_ITSKPRI (1), which is above all four
 * application priorities, so nothing below runs until this function reaches tk_slp_tsk().
 * ref_docs\bsp2_ra_fsp_jp.md section 4.3.2.
 *
 * NOTE ON REPORTING. The console is opened by T_UI (src\tasks\task_ui.c:118), so it does NOT
 * exist yet while this function runs. handle_error() still lights LED3 and still blocks for
 * ever on APP_FATAL, which is the visible failure the booth needs; the console line it also
 * tries to print is simply lost this early. That is unchanged behaviour - the FreeRTOS
 * start-up could not print either.
 *
 * Every kernel name below is confirmed against the vendored kernel:
 *   T_CFLG { exinf, flgatr, iflgptn }        mtk3_bsp2\mtkernel\include\tk\syscall.h:241-248
 *   ID tk_cre_flg(CONST T_CFLG *)            syscall.h:742
 *   T_CTSK { exinf, tskatr, task, itskpri, stksz, [dsname,] bufptr }  syscall.h:167-179
 *   ID tk_cre_tsk(CONST T_CTSK *)            syscall.h:699
 *   ER tk_sta_tsk(ID, INT stacd)             syscall.h:701
 *   ER tk_slp_tsk(TMO)                       syscall.h:715
 *   TA_TFIFO 0x0, TA_WMUL 0x8, TA_HLNG 0x1, TA_RNG3 0x300            syscall.h:68,85,29,36
 *********************************************************************************************************************/

#include <tk/tkernel.h>

#include "common_util.h"     /* handle_error, VISION_AI_APP_ERR_KERNEL_OBJECT */
#include "app_pool.h"

/**********************************************************************************************************************
 * THE EVENT FLAG that replaces the generated FreeRTOS event group g_ai_app_event.
 *
 * Defined here and used by src\app_signal.c, which declares it extern. Every one of the demo
 * 13 signalling call sites goes through app_sig_* and never sees this object.
 *********************************************************************************************************************/
ID g_evt = 0;

/* The four task IDs. Kept so a later phase can reference them; nothing needs them today. */
static ID g_tsk_cam   = 0;
static ID g_tsk_ai    = 0;
static ID g_tsk_ui    = 0;
static ID g_tsk_story = 0;

/* The four task bodies. C5 changed their signature from void f(void *) to the kernel
 * void f(INT stacd, void *exinf). The bodies themselves did not change. */
extern void cam_thread_entry(INT stacd, void * exinf);
extern void ai_thread_entry(INT stacd, void * exinf);
extern void ui_thread_entry(INT stacd, void * exinf);
extern void story_thread_entry(INT stacd, void * exinf);

/**********************************************************************************************************************
 * Create one task and start it.
 *
 * was: xTaskCreateStatic(func, "name", bytes/4, &params, prio, static_stack, &static_tcb)
 *      in the four generated ra_gen\*_thread.c files.
 *
 * Two things flip and both are easy to get backwards:
 *   - stksz is in BYTES here; xTaskCreateStatic took WORDS, which is why the generated code
 *     read 0x1000/4. The byte figures below are therefore the SAME stacks as before.
 *   - itskpri is LOWER = HIGHER priority; FreeRTOS was the other way round.
 *
 * No TA_USERBUF, so the kernel takes the stack from its own system memory
 * (task_manage.c:55-69, the USE_IMALLOC branch) instead of the .stack.<name> sections FreeRTOS
 * used. It allocates stksz + DEFAULT_SYS_STKSZ per task.
 *
 * @param[in] entry    the task body
 * @param[in] itskpri  1 is highest
 * @param[in] stksz    bytes
 * @return    the task ID, or a negative error code
 *********************************************************************************************************************/
static ID app_start_task(FP entry, PRI itskpri, SZ stksz)
{
    ID id;

    /* TA_HLNG | TA_RNG3 | TA_FPU is the full set and all three are needed.
     * TA_HLNG (0x1) says the entry point is a high-level-language function - and TA_ASM is
     * 0x0, so LEAVING TA_HLNG OUT SILENTLY DECLARES AN ASSEMBLY ENTRY POINT.
     * TA_RNG3 (0x300) is the user protection level.
     * TA_FPU is TA_COP0 (0x1000) on this core
     * (mtk3_bsp2\include\tk\sysdepend\ra_fsp\cpu\core\armv8m\cpudef.h:34) and it is required:
     * the gesture rules and the whole story generator are float code. All three values are
     * inside VALID_TSKATR (task_manage.c:32-40), so tk_cre_tsk accepts them. */
    T_CTSK ctsk = {
        .exinf   = NULL,
        .tskatr  = TA_HLNG | TA_RNG3 | TA_FPU,
        .task    = entry,
        .itskpri = itskpri,
        .stksz   = stksz,
    };

    id = tk_cre_tsk(&ctsk);

    if (id > 0)
    {
        /* was: nothing - xTaskCreateStatic left the task ready to run. tk_cre_tsk leaves it
         * DORMANT, so it must be started explicitly. stacd 0 arrives as the first argument of
         * the entry function, which none of the four bodies reads. */
        (void) tk_sta_tsk(id, 0);
    }

    return id;
}

/**********************************************************************************************************************
 * The user application entry point. Called by the kernel initial task.
 *
 * ORDER MATTERS AND IS FIXED: the event flag FIRST, then the pool, then the tasks. FSP
 * peripherals are already open by now - g_hal_init() ran in main() - so an
 * interrupt can already be firing into app_sig_set_isr(), which is why that function guard
 * for "the object does not exist yet" is not optional.
 *
 * @return  never, in practice
 *********************************************************************************************************************/
INT usermain(void)
{
    /* 1. THE EVENT FLAG.
     *
     * TA_WMUL IS MANDATORY, not a nicety. With the default TA_WSGL a second task that waits on
     * this flag while another is already waiting gets E_OBJ
     * (mtk3_bsp2\mtkernel\kernel\tkernel\eventflag.c:249-253), and four tasks wait on this one
     * object at the same time.
     * iflgptn 0 matches the FreeRTOS event group, which started with no bits set. */
    T_CFLG cflg = {
        .exinf   = NULL,
        .flgatr  = TA_TFIFO | TA_WMUL,
        .iflgptn = 0,
    };

    g_evt = tk_cre_flg(&cflg);      /* RETURNS the ID; there is no out-parameter */

    if (g_evt <= 0)
    {
        /* Nothing can be signalled, so no task could ever make progress. APP_FATAL does not
         * return - it stays in a slow LED3 blink loop (common_util.c:139). */
        (void) handle_error(VISION_AI_APP_ERR_KERNEL_OBJECT, "usermain tk_cre_flg");
    }

    /* 2. THE APPLICATION HEAP.
     *
     * was: nothing at all. FreeRTOS heap 4 built its free list lazily inside the first
     * pvPortMalloc, so it needed no start-up call. This pool must be created, and until it is
     * every app_pool_alloc() returns NULL. */
    if (!app_pool_init())
    {
        /* Same reasoning as above: the story generator, every C++ new in the image and the 2D
         * drawing engine all allocate from this one pool. */
        (void) handle_error(VISION_AI_APP_ERR_KERNEL_OBJECT, "usermain app_pool_init");
    }

    /* 3. THE FOUR TASKS, in the same priority ORDER as the source project.
     *
     * FreeRTOS priority (higher = higher)  ->  itskpri (lower = higher). Stacks unchanged.
     *   T_CAM    was 4, highest  ->   5    0x1000 = 4,096 bytes
     *   T_AI     was 3           ->   6    0x4000 = 16,384
     *   T_UI     was 2           ->   7    0x2000 = 8,192
     *   T_STORY  was 1, lowest   ->  12    0x4000 = 16,384
     * CNF_MAX_TSKPRI is 32, so 12 is legal with room to spare. */
    g_tsk_cam   = app_start_task((FP) cam_thread_entry,    5, 0x1000);
    g_tsk_ai    = app_start_task((FP) ai_thread_entry,     6, 0x4000);
    g_tsk_ui    = app_start_task((FP) ui_thread_entry,     7, 0x2000);
    g_tsk_story = app_start_task((FP) story_thread_entry, 12, 0x4000);

    if ((g_tsk_cam <= 0) || (g_tsk_ai <= 0) || (g_tsk_ui <= 0) || (g_tsk_story <= 0))
    {
        /* A missing task means a missing part of the demo and, worse, a signal nobody will
         * ever answer. It is also the loud failure that catches a kernel heap that is too
         * small. */
        (void) handle_error(VISION_AI_APP_ERR_KERNEL_OBJECT, "usermain tk_cre_tsk");
    }

    /* 4. STAY ASLEEP FOR EVER.
     *
     * If usermain() returns, micro T-Kernel SHUTS DOWN and the whole demo stops
     * (ref_docs\bsp2_ra_fsp_jp.md section 4.3.2). The initial task is the highest-priority
     * task in the system, so this sleep is also what lets the four tasks above run at all.
     *
     * was: vTaskStartScheduler() at the end of the generated main(), which likewise never
     * returned. */
    (void) tk_slp_tsk(TMO_FEVR);

    return 0;
}
