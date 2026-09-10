#include <sstream>
#include <string>
#include <vector>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <unistd.h>

/* was: #include "FreeRTOS.h" and #include "task.h", for vTaskDelay, pdMS_TO_TICKS
 * and xTaskGetTickCount. The micro T-Kernel headers cannot be included in this
 * translation unit - it is C++ and pulls <sstream>/<string>/<vector>, which reach
 * <ctype.h> - so the two calls go through the C wrapper in src/app_os.h. */
#include "app_os.h"

/* The story's own allocation wrappers. The header carries its own extern "C"
 * guard, so it is included outside the block below. */
#include "story_alloc.h"

/* The story interface. story_api.h brings story_run's own declaration,
 * STORY_MAX_TOKENS, STORY_PROMPT_COUNT and story_abandon_requested, which the
 * token loop inside llama2.h tests; story_text.h brings story_text_put, which
 * story_emit writes each piece into. Both carry their own extern "C" guard, and
 * both must come BEFORE llama2.h is included below. */
#include "story_api.h"
#include "story_text.h"

/* The demo's own time base, for edit 9. */
#include "time_counter.h"

/* After this many tokens the story stops at the first piece of text that ends a
 * sentence. */
#define STORY_SENTENCE_STOP_POS  (96)

/* Stub implementations for embedded environment - avoid linking to full libc */
extern "C" {
    #include "console_output.h"
    #include "hal_data.h"
    #include "bsp_api.h"

    /* Stub FILE objects for stderr and stdout */
    struct {
        int dummy;
    } __stdin = {0};
    struct {
        int dummy;
    } __stdout = {0};
    struct {
        int dummy;
    } __stderr = {0};

    FILE* const stdin = (FILE*)&__stdin;
    FILE* const stdout = (FILE*)&__stdout;
    FILE* const stderr = (FILE*)&__stderr;

    /* Override fprintf to redirect to UART console via r_sci_b_uart.
     * There is only one console, so the stream is deliberately ignored - naming it here clears
     * the "unused parameter 'stream'" warning from the 2026-08-14 build log. */
    int fprintf(FILE* stream, const char* format, ...) {
        (void)stream;
        char buffer[512];
        va_list args;
        va_start(args, format);
        int result = vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        if (result > 0) {
            print_to_console(buffer);
        }
        return result;
    }

    /* Override printf to redirect to UART console via r_sci_b_uart */
    int printf(const char* format, ...) {
        char buffer[512];
        va_list args;
        va_start(args, format);
        int result = vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);

        if (result > 0) {
            print_to_console(buffer);
        }
        return result;
    }

    /* Stub for _exit - prevent actual program termination */
    void _exit(int status) {
        (void)status;
        printf("ERROR: Attempted to call _exit(%d) - continuing execution\n", status);
        /* Don't actually exit in embedded system */
        while(1);
    }

    /* Stub exit() - just return without exiting */
    void exit(int status) {
        _exit(status);
    }

    /* Stub fflush to prevent the standard libc from trying to parse our fake stdout */
    int fflush(FILE* stream) {
        (void)stream;
        return 0; // Return success
    }
}

// Override the six global new and delete forms so every C++ allocation in the
// story goes through the demo's own wrappers. At Stage 1 those wrappers are
// pvPortMalloc / vPortFree, which is what this file did inline before, plus the
// failure latch.
//
// The two "throw std::bad_alloc()" lines that used to sit here are gone. This
// project compiles C++ with -fno-exceptions, because the vision fork does, so a
// throw does not build, and an exception is not wanted here either: the request
// returns NULL, the story feature is disabled, and the heap is never asked
// again in that run. So the
// operators simply hand back what story_malloc returns, and story_malloc has
// already latched and reported the failure.
void* operator new(size_t size) {
    return story_malloc(size);
}

void* operator new[](size_t size) {
    return story_malloc(size);
}

void operator delete(void* ptr) noexcept {
    story_free(ptr);
}

void operator delete[](void* ptr) noexcept {
    story_free(ptr);
}

// C++14 sized deallocation overrides
void operator delete(void* ptr, size_t) noexcept {
    story_free(ptr);
}

void operator delete[](void* ptr, size_t) noexcept {
    story_free(ptr);
}

/* Redirect memory functions strictly for the llama2.h C code.
 * The old targets were pvPortMalloc, vPortFree and a local my_calloc
 * helper; my_calloc is deleted because story_calloc does the same job. To
 * revert: point these three back at pvPortMalloc / vPortFree and restore
 * my_calloc. */
#define malloc story_malloc
#define free story_free
#define calloc story_calloc

/* Force printf and fprintf to output directly to the UART console */
#define printf(...) do { \
    char _buf[256]; \
    snprintf(_buf, sizeof(_buf), __VA_ARGS__); \
    print_to_console(_buf); \
} while(0)

#define fprintf(stream, ...) printf(__VA_ARGS__)

#include "llama2.h"

/* Clean up macros so they don't affect other C++ code further down */
#undef malloc
#undef free
#undef calloc

#include "stories15M_q80.h"
#include "tokenizer.h"

// Number of tokens (steps) to generate. Override at build time with
// -DLLAMA_STEPS=<n> (e.g. add "LLAMA_STEPS=64" to the project's C/C++
// preprocessor defines). Capped at the model's seq_len in LoadLlamaModel().
//
// THE DEFAULT IS NOW STORY_MAX_TOKENS, from src\story\story_api.h, so the 128 of
// 128 is ONE build constant in ONE place. The build-time override still works
// and is still the way a shorter story is taken.
#ifndef LLAMA_STEPS
#define LLAMA_STEPS STORY_MAX_TOKENS
#endif

// Llama model inference parameters.
const float kTemperature = 1.0f;
const float kTopP = 0.9f;
const int kSteps = (int)LLAMA_STEPS;

// THE FOUR PROMPTS. One is picked at random for
// each story, so a visitor who watches twice does not see the same opening. All four
// have the same shape: a scene-setting phrase that ends in the middle, so the model
// has to continue it. The first is the sample's own prompt (llama4micro.cpp:157 in
// the story fork), kept as the proven one.
const char* kPrompts[STORY_PROMPT_COUNT] = {
    "Once upon a time, there was a ",
    "One sunny morning, a little ",
    "Deep in the forest, a small ",
    "Once upon a time, a robot named ",
};

// Llama model data structures.
Transformer transformer;
int group_size;
int steps = kSteps;
Tokenizer tokenizer;
Sampler sampler;

// Loads the Llama model and tokenizer into memory and sets up data structures.
void LoadLlamaModel() {
  printf(">>> Loading Llama model...\n");
  fflush(stdout);  // Ensure printf output is sent before heavy operations

  // Give SDRAM time to stabilize after system initialization
  // and allow console output to flush
  app_os_delay_ms(100);   /* was: vTaskDelay(pdMS_TO_TICKS(100)) */

  // Run the weights DIRECTLY from OSPI (8D-8D-8D high-speed, read-in-place).
  // OSPI is memory-mapped at 0x90000000 and the OSPI protocol was already switched
  // to DOPI in new_thread0_entry(). We point the transformer straight at that address,
  // so there is NO 16.3 MB copy to SDRAM. The weight tensors (int8 values + fp32 scales)
  // are read from OSPI in place by matmul(). Only the dequantized token-embedding table
  // and the activation/KV buffers still live in SDRAM (allocated inside build_transformer).
  build_transformer(&transformer, (uint8_t*)stories15M_q80_bin, &group_size);

  /* The header check and the run-state allocation inside build_transformer RETURN on failure
   * instead of spinning in exit(). Stop loading here: transformer.config holds nothing usable,
   * so the tokeniser and the sampler would be built from rubbish. T_STORY reads
   * story_model_failed() next and disables the story feature with the failing check already
   * named on the console. */
  if (story_model_failed()) {
    return;
  }

  if (steps == 0 || steps > transformer.config.seq_len) {
    steps = transformer.config.seq_len;
  }

  printf(">>> Loading Llama tokenizer...\n");
  fflush(stdout);

  // Tokenizer is also read directly from OSPI (zero-copy).
  //
  // The third argument is the size of the whole tokeniser blob - a size that cannot
  // be wrong. build_tokenizer copies all 32,000 strings into one block of that size
  // instead of taking one small block per token. Only this file can measure it,
  // because tokenizer.h is included below llama2.h.
  build_tokenizer(&tokenizer, (uint8_t*)tokenizer_bin, transformer.config.vocab_size,
                  sizeof(tokenizer_bin));

  // Use FreeRTOS tick count for RNG seed instead of time() to avoid
  // linking against standard library symbols (stdout, stderr, gettimeofday, _exit)
  /* was: (unsigned int)xTaskGetTickCount() - both count milliseconds since boot. */
  unsigned long long rng_seed = (unsigned int)app_os_millis()^ SysTick->VAL;
  build_sampler(&sampler, transformer.config.vocab_size, kTemperature, kTopP,
                rng_seed);
}

// Frees the memory associated with the Llama model.
void UnloadLlamaModel() {
  printf(">>> Unloading Llama model...\n");

  free_sampler(&sampler);
  free_tokenizer(&tokenizer);
  free_transformer(&transformer);
  // No SDRAM model copy anymore - the weights stay in OSPI, so nothing to delete here.
}

// Generates a story beginning with the specified prompt.
void TellStory(const char* prompt) {
  printf(">>> Generating tokens...\n");

  // The old "float tokens_s" was never written by generate() and never read here. It is
  // replaced by the count of tokens the generator really produced, which is what the rate must
  // be divided by.
  int tokens_made = 0;

  // The FreeRTOS tick timing is replaced by the demo's own time base,
  // TimeCounter_CurrentCountGet(), which counts 100 microsecond periods
  // and is the same clock every state timeout and every gate timer reads. The old
  // xTaskGetTickCount pair measured in whole ticks and would have to change again at
  // Stage 2, where there is no such call.
  //
  // The tokens-per-second line still goes to the console, and is still formatted with
  // integer arithmetic (value x100), because this C library is built without floating
  // point in printf.
  uint32_t t0 = TimeCounter_CurrentCountGet();
  generate(&transformer, &tokenizer, &sampler, prompt, steps,
           group_size, &tokens_made);
  uint32_t t1 = TimeCounter_CurrentCountGet();

  uint32_t elapsed_ms = TimeCounter_CountValueConvertToMs(t0, t1);
  if (elapsed_ms == 0) { elapsed_ms = 1; }

  // Divide by the tokens really produced, not by the 128-token ceiling. The sentence-end
  // rule stops most stories between token 97 and 128, so dividing by `steps` over-reported
  // the rate by up to about a third. The count is printed too, so the line says what it was
  // worked out from.
  if (tokens_made <= 0) { tokens_made = 0; }

  uint32_t tok_s_x100 = (uint32_t)((uint64_t)(uint32_t)tokens_made * 100000ULL / elapsed_ms);
  printf("\n>>> %d tokens of at most %d in %u ms = %u.%02u tokens/s (weights read directly from OSPI, 8D-8D-8D)\n",
         tokens_made, steps, (unsigned)elapsed_ms,
         (unsigned)(tok_s_x100 / 100U), (unsigned)(tok_s_x100 % 100U));
}

/* ---------------------------------------------------------------------------------------------
 * run_model() is SPLIT.
 *
 *   LoadLlamaModel()   runs ONCE, from story_init(), at T_STORY's start-up.
 *   TellStory()        becomes story_run(prompt_index) and runs once per story.
 *   UnloadLlamaModel() is NOT called: the model stays loaded for the life of the demo, so the
 *                      second story starts instantly and the pool is never churned.
 *
 * run_model() itself is gone. Nothing called it but the story fork's own thread entry, which
 * src\tasks\task_story.c replaces, and that file calls these two.
 *
 * UnloadLlamaModel is kept in the file, unused, because it is the only written-down way to give
 * the pool back if a later stage ever needs to. The compiler removes it from the image.
 * ------------------------------------------------------------------------------------------ */

extern "C" void story_init(void) {
  LoadLlamaModel();
}

extern "C" void story_run(uint8_t prompt_index) {
  if (prompt_index >= STORY_PROMPT_COUNT) {
    prompt_index = 0U;
  }

  TellStory(kPrompts[prompt_index]);
}
