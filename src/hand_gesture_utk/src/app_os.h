/**********************************************************************************************************************
 * File Name    : app_os.h
 * Description  : Delay and millisecond clock, callable from C++.
 *
 * WHY IT EXISTS. src\story\llm_model\llama4micro.cpp is C++ and pulls in <sstream>, <string>
 * and <vector>, which reach <ctype.h>. The micro T-Kernel headers cannot be included in that
 * translation unit, so the two operating-system calls it makes are wrapped here in C instead.
 *
 * This header is deliberately free of kernel types - uint32_t only.
 *********************************************************************************************************************/

#ifndef APP_OS_H_
#define APP_OS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wraps tk_dly_tsk(), which already takes milliseconds. */
void app_os_delay_ms(uint32_t ms);

/* Wraps tk_get_otm() and returns its low 32 bits, which are milliseconds since boot.
 * Used only for the story RNG seed. */
uint32_t app_os_millis(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_OS_H_ */
