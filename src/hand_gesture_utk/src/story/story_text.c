/**********************************************************************************************************************
 * File Name    : story_text.c
 * Description  : The story text ring buffer. One writer (T_STORY), one reader (T_UI).
 *
 * New file. Not copied from sample_code.
 *
 * WHY THERE IS NO LOCK. There is exactly one writer and exactly one reader, and each owns one
 * index: the writer moves g_head and only reads g_tail, the reader moves g_tail and only reads
 * g_head. Both indices are single 32-bit words, so a reader can never see half of an update on
 * this core. That makes the buffer safe with no critical section and no priority inversion,
 * which matters because T_STORY is the lowest priority task in the demo and T_UI must never
 * wait for it.
 *
 * The buffer is in on-chip SRAM and both ends are the processor, so it needs no cache
 * maintenance.
 *********************************************************************************************************************/

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "common_util.h"        /* EV_STORY_TEXT */
#include "app_signal.h"
#include "story_text.h"

/**********************************************************************************************************************
 * Private data
 *********************************************************************************************************************/

/* 4,096 bytes. One byte of the buffer is always left empty, which is how a full buffer is told
 * from an empty one without a third variable. So it holds 4,095 bytes. */
static char g_ring[STORY_TEXT_RING_BYTES];

static volatile uint32_t g_head = 0U;   /* written by T_STORY only */
static volatile uint32_t g_tail = 0U;   /* written by T_UI only    */

/* How many bytes the writer had to throw away because the reader had not caught up. It cannot
 * happen in normal running - the generator writes a few bytes per token at about four tokens a
 * second, and T_UI drains the whole buffer every panel refresh - so a value above zero means
 * T_UI stopped drawing. Reported, never silently ignored. */
static volatile uint32_t g_dropped = 0U;

/**********************************************************************************************************************
 * Public functions
 *********************************************************************************************************************/

void story_text_reset(void)
{
    /* Called by T_UI, on the first frame of a new story, beside ui_story_text_clear()
     * (src\tasks\task_ui.c). T_UI is the reader and g_tail is the reader's index, so this keeps
     * the one-writer-per-index rule of the file heading.
     *
     * T_STORY used to call this, which gave g_tail two writers. It is safe where it is now
     * because T_UI starts the story itself, in conv_step, and does not block before it gets
     * here - and T_STORY is the lower priority of the two, so it cannot have written a byte
     * yet.
     *
     * The order matters if that rule is ever broken: moving the tail up to the head can only
     * ever lose text, never invent it. */
    g_tail = g_head;
}

void story_text_put(const char * piece)
{
    uint32_t head;
    uint32_t next;
    bool     wrote = false;

    if (NULL == piece)
    {
        return;
    }

    head = g_head;

    while ('\0' != *piece)
    {
        next = head + 1U;

        if (next >= STORY_TEXT_RING_BYTES)
        {
            next = 0U;
        }

        if (next == g_tail)
        {
            /* Full. The oldest text is the part the visitor has already read, but dropping from
             * the tail would mean writing g_tail, which belongs to the reader. So the newest
             * text is dropped instead and the loss is counted. */
            g_dropped++;
            break;
        }

        g_ring[head] = *piece;
        head         = next;
        piece++;
        wrote        = true;
    }

    /* The index moves only after every byte is in place, so the reader can never see a byte
     * that has not been written yet. */
    g_head = head;

    if (wrote)
    {
        app_sig_set(EV_STORY_TEXT);
    }
}

size_t story_text_get(char * dst, size_t max)
{
    uint32_t tail;
    uint32_t head;
    size_t   n = 0U;

    if ((NULL == dst) || (0U == max))
    {
        return 0U;
    }

    tail = g_tail;
    head = g_head;

    /* max includes the closing zero, so at most max - 1 bytes of text are taken. */
    while ((n < (max - 1U)) && (tail != head))
    {
        dst[n] = g_ring[tail];
        n++;
        tail++;

        if (tail >= STORY_TEXT_RING_BYTES)
        {
            tail = 0U;
        }
    }

    dst[n] = '\0';

    /* Only now, so the writer never sees space free before it has been read out. */
    g_tail = tail;

    return n;
}

uint32_t story_text_dropped_get(void)
{
    return g_dropped;
}
