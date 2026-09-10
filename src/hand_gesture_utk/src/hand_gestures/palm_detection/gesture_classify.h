/*
 * gesture_classify.h
 *
 * Rule-based gesture recognition from the 21 hand landmark points.
 *
 * Derived from:
 *   sample_code/ek_ra8p1_vision_palm_detection_hand_landmarkmodel_gesture_recognition_
 *   camera_LCD_FSP640/src/ai_application/palm_detection/gesture_classify.h
 *
 * The sample used a trained 3-class MLP (Open, Close, Pointer). That model is
 * dropped. Five shapes are now worked out by geometry on the landmark points:
 * open palm, thumbs up, thumbs down, fist, victory.
 */
#ifndef GESTURE_CLASSIFY_H_
#define GESTURE_CLASSIFY_H_

#include <stdbool.h>

#include "landmark_postprocess.h"   /* landmark_point_t, landmark_result_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GESTURE_OPEN_PALM = 0,  /* all four fingers straight, thumb not tested   */
    GESTURE_THUMBS_UP,      /* four fingers curled, thumb out and pointing up */
    GESTURE_THUMBS_DOWN,    /* four fingers curled, thumb out, pointing down  */
    GESTURE_FIST,           /* four fingers curled, thumb folded in          */
    GESTURE_VICTORY,        /* index and middle straight and apart           */
    GESTURE_COUNT           /* number of gestures                            */
} gesture_t;

#define GESTURE_UNKNOWN  GESTURE_COUNT  /* alias for "no match this frame" */

/* The two quality limits a result must pass before any shape rule looks at it.
 *
 * These lived in gesture_classify.c until 2026-08-13 and were moved here unchanged, because
 * the gesture gate applies the same two as its own rule 2 and adds no third test of its own.
 * One place, one value each. */
#define GESTURE_MIN_PALM_PIXELS       (40.0f)   /* smallest usable palm length s */
#define GESTURE_MIN_LANDMARK_SCORE    (0.5f)    /* landmark score floor          */

/* Live values of the three geometry tests, for the tuning overlay (step 9).
 * Filled on every call to gesture_classify_ex, even when the answer is
 * GESTURE_UNKNOWN, so the bench session can see why a shape was refused.
 *
 * `u` is a MEASUREMENT ONLY. No shape rule reads it. It exists to settle the
 * open fist-versus-thumbs-up question: if a fist held with the thumb alongside
 * the hand reaches the thumb-out limit, the second test that separates the two
 * is "the thumb tip must sit clearly above the index knuckle", and this is the
 * number that test would use. Read it on the bench before adding any rule. */
typedef struct {
    float r_index;      /* finger straightness ratio, index finger  */
    float r_middle;     /* finger straightness ratio, middle finger */
    float r_ring;       /* finger straightness ratio, ring finger   */
    float r_little;     /* finger straightness ratio, little finger */
    float t;            /* thumb-out ratio                          */
    float d;            /* thumb direction, negative is image up    */
    float u;            /* thumb tip above index knuckle, negative is image up */
    float s;            /* palm length in camera pixels             */
    float hand_score;   /* landmark model score of this hand, 0..1  */
    bool  usable;       /* true when 21 points, score and palm length all passed */
} gesture_metrics_t;

/* `hand_score` was added on 2026-08-13. The gesture gate takes a landmark score, and this
 * file is the only place that has one: it is copied straight out of landmark_result_t.hand_score
 * (landmark_postprocess.h). Nothing was removed or renamed, and no existing behaviour changed,
 * so every earlier caller keeps working. It is 0 whenever the metrics are zeroed. */

/**
 * Work out the gesture of one hand from its landmark points.
 *
 * @param lm   Pointer to a valid landmark result (num_points == 21)
 * @return     One of the five gestures, or GESTURE_UNKNOWN when no row of the
 *             shape table matches or a test lands in an undecided band.
 */
gesture_t gesture_classify(const landmark_result_t* lm);

/**
 * Same as gesture_classify, but also reports the raw test values.
 *
 * @param lm       Pointer to a landmark result (may be NULL)
 * @param metrics  [out] Test values. Zeroed when lm holds fewer than 21 points.
 * @return         Same value as gesture_classify.
 */
gesture_t gesture_classify_ex(const landmark_result_t* lm, gesture_metrics_t* metrics);

/**
 * Get a human-readable string for a gesture.
 *
 * @param g    Gesture enum value
 * @return     Static string like "Open palm", or "" for GESTURE_UNKNOWN
 */
const char* gesture_name(gesture_t g);

/* ------------------------------------------------------------------ */
/* Tracker: picks one hand out of the detection slots, then steadies   */
/* the answer over several inference results.                          */
/* ------------------------------------------------------------------ */

/**
 * Feed one new inference result to the tracker.
 * Call this once per NEW inference result, not once per display frame,
 * or the same result is counted several times and the filter does nothing.
 *
 * Of all slots holding a hand, the one with the highest landmark score is
 * used; the rest are ignored.
 *
 * @param results   Array of landmark results, one per detection slot
 * @param n_slots   Number of slots in the array
 */
void gesture_track_update(const landmark_result_t* results, int n_slots);

/**
 * The steady gesture: the last value that came back the same three inference
 * results in a row. Returns GESTURE_UNKNOWN when the hand has left the view,
 * so the label never sticks on screen.
 */
gesture_t gesture_track_stable(void);

/**
 * The gesture of the most recent inference result, before the steady filter.
 * Useful on the tuning overlay.
 */
gesture_t gesture_track_raw(void);

/**
 * Test values of the most recent inference result. Never NULL.
 */
const gesture_metrics_t* gesture_track_metrics(void);

#ifdef __cplusplus
}
#endif

#endif /* GESTURE_CLASSIFY_H_ */
