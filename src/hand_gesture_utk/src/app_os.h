/**********************************************************************************************************************
 * File Name    : app_os.h
 * Description  : Delay and millisecond clock, callable from C++.
 *
 * NEW FILE, added by the FreeRTOS -> micro T-Kernel 3.0 BSP2 port.
 *
 * WHY IT EXISTS. src\story\llm_model\llama4micro.cpp is C++ and pulls in <sstream>, <string>
 * and <vector>, which reach <ctype.h>. The micro T-Kernel headers cannot be included in that
 * translation unit, so the two operating-system calls it makes are wrapped here in C instead.
 * That is exactly the pair the earlier ra8p1_llm_utk port needed (src\llm_os.h / llm_os.c).
 *
 * This header is deliberately free of kernel types - uint32_t only.
 *********************************************************************************************************************/

#ifndef APP_OS_H_
#define APP_OS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* was: vTaskDelay(pdMS_TO_TICKS(ms)). Wraps tk_dly_tsk(), which already takes milliseconds. */
void app_os_delay_ms(uint32_t ms);

/* was: xTaskGetTickCount(), which counted 1 ms ticks (configTICK_RATE_HZ 1000).
 * Wraps tk_get_otm() and returns its low 32 bits, which are milliseconds since boot.
 * Used only for the story RNG seed. */
uint32_t app_os_millis(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_OS_H_ */
