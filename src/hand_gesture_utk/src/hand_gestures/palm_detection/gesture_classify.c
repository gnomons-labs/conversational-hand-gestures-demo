/*
 * gesture_classify.c
 *
 * Rule-based gesture recognition from the 21 hand landmark points.
 *
 * Derived from:
 *   sample_code/ek_ra8p1_vision_palm_detection_hand_landmarkmodel_gesture_recognition_
 *   camera_LCD_FSP640/src/ai_application/palm_detection/gesture_classify.c
 *
 * What changed against that file
 * ------------------------------
 * The sample ran a trained 3-class MLP (Open, Close, Pointer) on the 42 wrist-
 * relative coordinates. It divided all 42 values by the largest absolute value,
 * so one outlier joint set the scale, and it never took out hand rotation. It
 * was measured to misread even its own three classes.
 *
 * The model is gone. Five shapes are now worked out by three geometry tests on
 * the landmark points. Two of the tests are ratios of distances, so hand size
 * and rotation cancel out. The third one, thumb direction, is deliberately NOT
 * tilt-free: up and down are properties of the world, not of the hand.
 *
 * Joint numbering is the sample's own skeleton table
 * (display_layer/detection_screen_mipi.c:146-152):
 *   0        wrist
 *   1  -  4  thumb, knuckle first, tip last
 *   5  -  8  index
 *   9  - 12  middle
 *   13 - 16  ring
 *   17 - 20  little
 */

#include <stddef.h>
#include <string.h>
#include <math.h>

#include "gesture_classify.h"

/* ------------------------------------------------------------------ */
/* Step 3 - the eight tunable limits, all in one block                 */
/* Starting values worked out from the geometry. NOT measured.         */
/* The bench session tunes these eight and no other gesture limit.     */
/* ------------------------------------------------------------------ */

/* Test 1: finger straightness ratio r = dist(tip,wrist) / dist(middle joint,wrist).
 * A straight finger puts its tip far past its own middle joint, so r is large.
 * A curled finger folds the tip back toward the palm, so r drops below 1.
 * Between the two limits the finger is undecided and the whole hand is refused. */
#define GESTURE_FINGER_STRAIGHT_MIN   (1.25f)   /* r >= this: straight */
#define GESTURE_FINGER_CURLED_MAX     (1.15f)   /* r <= this: curled   */
/* Measured on the board, 2026-08-10 (screenshots/raw_fist.jpg, raw_vic.jpg):
 *   curled   0.85 to 1.02  (fist: 0.93 0.90 0.85 0.85; victory ring 1.02, little 0.95)
 *   straight 1.42 to 1.43  (victory index and middle)
 * Nothing was seen between 1.02 and 1.42. The first value of 1.05 left victory's
 * ring finger only 0.03 clear of the undecided band, so normal hand wobble
 * refused the whole hand and the label rarely latched. 1.15 sits in the empty
 * gap. STRAIGHT_MIN stays at 1.25 because open palm already works with it and
 * the ring and little straight ratios have not been measured. */

/* Test 2: thumb-out ratio t = dist(joint 4, joint 5) / s.
 * A thumb folded across the palm sits near the index knuckle, so t is small. */
#define GESTURE_THUMB_OUT_MIN         (0.58f)   /* t >= this: thumb is out    */
#define GESTURE_THUMB_IN_MAX          (0.45f)   /* t <= this: thumb is folded */
/* Measured on the board, 2026-08-10 (screenshots/thump_up.jpg, thump_down.jpg,
 * raw_fist.jpg, raw_vic.jpg):
 *   thumb out    0.66 (thumbs down)  0.76 (thumbs up)
 *   thumb folded 0.15 (fist)         0.51 (victory, thumb only resting)
 * The first limits of 0.75 and 0.55 left thumbs up 0.01 clear and put thumbs
 * down at 0.66 inside the undecided band, so thumbs down never matched at all.
 * Only a fist has to read as folded, because victory and open palm do not test
 * the thumb, and a fist measured 0.15. That frees the folded limit to drop to
 * 0.45 and the out limit to 0.58, which clears a fist by 0.30, thumbs down by
 * 0.08 and thumbs up by 0.18. */

/* Test 3: thumb direction d = sign * (y of joint 4 - y of wrist) / s.
 * Image coordinates, so y grows downward. Inside the dead band, no match. */
#define GESTURE_THUMB_DIR_BAND        (0.6f)    /* up if d <= -this, down if d >= +this */

/* Victory: the two straight tips must be this far apart, as a fraction of s. */
#define GESTURE_VICTORY_TIP_GAP_MIN   (0.35f)

/* Step 6 rejection limits.
 *
 * MOVED TO gesture_classify.h ON 2026-08-13, and only moved: the values, 40.0f and 0.5f, are
 * unchanged. The gesture gate applies the same two limits as its own rule 2, and a tuning
 * limit that is written down in two files is a limit that will one day disagree with itself.
 * Nothing else about this file's behaviour changes. */

/* ------------------------------------------------------------------ */
/* Step 4 - the thumb-direction sign                                   */
/* ------------------------------------------------------------------ */

/* CONFIRMED ON THE BOARD, 2026-08-10 (screenshots/raw_fist.jpg, raw_vic.jpg).
 *
 * +1.0f means "image up is the child's up", so a thumb held above the wrist
 * gives a NEGATIVE d. An upright fist read DIR -0.95 and an upright victory
 * read DIR -1.40, both with the thumb tip above the wrist on screen. The sign
 * is right.
 *
 * The earlier argument from the camera sensor registers does not survive
 * checking and is withdrawn: camera_layer/camera_layer.c:165-166 writes
 * {0x3820, 0x41} and {0x3821, 0x01}, and the file's own comments show both
 * differ from the stated default only in bit 0, which those comments do not
 * name as a flip or a mirror bit. detection_screen_mipi.c:176-177 does say the
 * image is mirrored, but a mirror is horizontal and does not change up and
 * down. No document in ref_docs/ covers this camera sensor. */
#define GESTURE_THUMB_DIR_SIGN        (+1.0f)

/* ------------------------------------------------------------------ */
/* Joint numbers                                                       */
/* ------------------------------------------------------------------ */

#define JOINT_WRIST         (0)
#define JOINT_THUMB_TIP     (4)
#define JOINT_INDEX_MCP     (5)    /* index knuckle, the thumb-out reference */
#define JOINT_INDEX_PIP     (6)
#define JOINT_INDEX_TIP     (8)
#define JOINT_MIDDLE_MCP    (9)    /* middle knuckle, the palm-length end    */
#define JOINT_MIDDLE_PIP    (10)
#define JOINT_MIDDLE_TIP    (12)
#define JOINT_RING_PIP      (14)
#define JOINT_RING_TIP      (16)
#define JOINT_LITTLE_PIP    (18)
#define JOINT_LITTLE_TIP    (20)

/* Result of one three-way test. */
typedef enum {
    TEST_NO = 0,
    TEST_YES,
    TEST_UNDECIDED
} test_result_t;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */

static float joint_distance(const landmark_result_t* lm, int a, int b)
{
    float dx = (float)(lm->pts[a].x - lm->pts[b].x);
    float dy = (float)(lm->pts[a].y - lm->pts[b].y);
    return sqrtf((dx * dx) + (dy * dy));
}

/* A ratio that always lands between the two finger limits, so a broken
 * measurement refuses the whole hand instead of being read as a curled finger. */
#define GESTURE_FINGER_RATIO_REFUSE \
    ((GESTURE_FINGER_STRAIGHT_MIN + GESTURE_FINGER_CURLED_MAX) * 0.5f)

/* Test 1 for one finger. tip and pip are that finger's tip and middle joint. */
static float finger_ratio(const landmark_result_t* lm, int tip, int pip)
{
    float base = joint_distance(lm, pip, JOINT_WRIST);
    if (base < 1e-3f) {
        /* Middle joint sits on the wrist: the landmarks are broken. */
        return GESTURE_FINGER_RATIO_REFUSE;
    }
    return joint_distance(lm, tip, JOINT_WRIST) / base;
}

static test_result_t finger_is_straight(float r)
{
    if (r >= GESTURE_FINGER_STRAIGHT_MIN) {
        return TEST_YES;
    }
    if (r <= GESTURE_FINGER_CURLED_MAX) {
        return TEST_NO;
    }
    return TEST_UNDECIDED;
}

static test_result_t thumb_is_out(float t)
{
    if (t >= GESTURE_THUMB_OUT_MIN) {
        return TEST_YES;
    }
    if (t <= GESTURE_THUMB_IN_MAX) {
        return TEST_NO;
    }
    return TEST_UNDECIDED;
}

/* ------------------------------------------------------------------ */
/* Step 2 and step 5 - the tests and the shape table                   */
/* ------------------------------------------------------------------ */

gesture_t gesture_classify_ex(const landmark_result_t* lm, gesture_metrics_t* metrics)
{
    gesture_metrics_t m;
    memset(&m, 0, sizeof(m));

    if ((lm == NULL) || (lm->num_points < LANDMARK_NUM_POINTS)) {
        if (metrics != NULL) {
            *metrics = m;
        }
        return GESTURE_UNKNOWN;
    }

    /* The landmark model's own score for this hand. Published so the gesture gate can apply
     * its quality rule itself. This file reads it already, two lines below, for the `usable`
     * flag; the only new work is passing it on. */
    m.hand_score = lm->hand_score;

    /* One length sets the scale: wrist to middle-finger knuckle. */
    m.s = joint_distance(lm, JOINT_MIDDLE_MCP, JOINT_WRIST);

    m.r_index  = finger_ratio(lm, JOINT_INDEX_TIP,  JOINT_INDEX_PIP);
    m.r_middle = finger_ratio(lm, JOINT_MIDDLE_TIP, JOINT_MIDDLE_PIP);
    m.r_ring   = finger_ratio(lm, JOINT_RING_TIP,   JOINT_RING_PIP);
    m.r_little = finger_ratio(lm, JOINT_LITTLE_TIP, JOINT_LITTLE_PIP);

    if (m.s >= GESTURE_MIN_PALM_PIXELS) {
        m.t = joint_distance(lm, JOINT_THUMB_TIP, JOINT_INDEX_MCP) / m.s;
        m.d = GESTURE_THUMB_DIR_SIGN *
              ((float)(lm->pts[JOINT_THUMB_TIP].y - lm->pts[JOINT_WRIST].y) / m.s);

        /* Measurement only - no shape rule reads this yet. Same form as d, so
         * the sign constant flows through identically and negative still means
         * "up on screen". It measures the thumb tip against the INDEX KNUCKLE
         * instead of the wrist, which is what tells a thumbs up from a fist:
         * in a thumbs up the tip stands above the knuckle, in a fist it stays
         * roughly level with the curled fingers. See the header comment. */
        m.u = GESTURE_THUMB_DIR_SIGN *
              ((float)(lm->pts[JOINT_THUMB_TIP].y - lm->pts[JOINT_INDEX_MCP].y) / m.s);
    }

    m.usable = (lm->hand_score >= GESTURE_MIN_LANDMARK_SCORE) &&
               (m.s >= GESTURE_MIN_PALM_PIXELS);

    if (metrics != NULL) {
        *metrics = m;
    }

    /* Step 6: reject rather than guess. */
    if (!m.usable) {
        return GESTURE_UNKNOWN;
    }

    /* Every row of the shape table tests all four fingers, so one undecided
     * finger refuses the whole hand. */
    test_result_t index  = finger_is_straight(m.r_index);
    test_result_t middle = finger_is_straight(m.r_middle);
    test_result_t ring   = finger_is_straight(m.r_ring);
    test_result_t little = finger_is_straight(m.r_little);

    if ((index  == TEST_UNDECIDED) || (middle == TEST_UNDECIDED) ||
        (ring   == TEST_UNDECIDED) || (little == TEST_UNDECIDED)) {
        return GESTURE_UNKNOWN;
    }

    /* Row 1: open palm. All four straight. Thumb not tested - children hold it
     * any way they like, and the noisiest joint would then decide the answer. */
    if ((index == TEST_YES) && (middle == TEST_YES) &&
        (ring  == TEST_YES) && (little == TEST_YES)) {
        return GESTURE_OPEN_PALM;
    }

    /* Row 5: victory. Index and middle straight, ring and little curled, and
     * the two tips far enough apart. Thumb not tested. */
    if ((index == TEST_YES) && (middle == TEST_YES) &&
        (ring  == TEST_NO)  && (little == TEST_NO)) {
        float tip_gap = joint_distance(lm, JOINT_INDEX_TIP, JOINT_MIDDLE_TIP);
        if (tip_gap >= (GESTURE_VICTORY_TIP_GAP_MIN * m.s)) {
            return GESTURE_VICTORY;
        }
        return GESTURE_UNKNOWN;
    }

    /* Rows 2, 3 and 4: all four curled. The thumb decides which one. */
    if ((index == TEST_NO) && (middle == TEST_NO) &&
        (ring  == TEST_NO) && (little == TEST_NO)) {
        test_result_t thumb = thumb_is_out(m.t);

        if (thumb == TEST_NO) {
            return GESTURE_FIST;                    /* row 4 */
        }
        if (thumb == TEST_YES) {
            if (m.d <= -GESTURE_THUMB_DIR_BAND) {
                return GESTURE_THUMBS_UP;           /* row 2 */
            }
            if (m.d >= GESTURE_THUMB_DIR_BAND) {
                return GESTURE_THUMBS_DOWN;         /* row 3 */
            }
            return GESTURE_UNKNOWN;                 /* dead band */
        }
        return GESTURE_UNKNOWN;                     /* thumb undecided */
    }

    /* A finger pattern that matches no row of the table. */
    return GESTURE_UNKNOWN;
}

gesture_t gesture_classify(const landmark_result_t* lm)
{
    return gesture_classify_ex(lm, NULL);
}

/* ------------------------------------------------------------------ */
/* Gesture name strings - one entry per enum value, checked by the     */
/* compile-time assert below so the array can never fall behind        */
/* ------------------------------------------------------------------ */

static const char* gesture_names[] = {
    "Open palm",    /* GESTURE_OPEN_PALM   = 0 */
    "Thumbs up",    /* GESTURE_THUMBS_UP   = 1 */
    "Thumbs down",  /* GESTURE_THUMBS_DOWN = 2 */
    "Fist",         /* GESTURE_FIST        = 3 */
    "Victory",      /* GESTURE_VICTORY     = 4 */
};

_Static_assert((sizeof(gesture_names) / sizeof(gesture_names[0])) == (size_t)GESTURE_COUNT,
               "gesture_names must hold one string per gesture_t value");

const char* gesture_name(gesture_t g)
{
    if (g >= GESTURE_COUNT) {
        return "";
    }
    return gesture_names[g];
}

/* ------------------------------------------------------------------ */
/* Step 8 - tracker: one hand, and a steady answer                     */
/* ------------------------------------------------------------------ */

#define GESTURE_STEADY_RESULTS  (3)   /* matching results needed to move the label */

static gesture_t         g_raw          = GESTURE_UNKNOWN;
static gesture_t         g_run_value    = GESTURE_UNKNOWN;
static uint8_t           g_run_length   = 0;
static gesture_t         g_stable       = GESTURE_UNKNOWN;
static gesture_metrics_t g_metrics      = {0};

void gesture_track_update(const landmark_result_t* results, int n_slots)
{
    const landmark_result_t* best = NULL;

    /* Two hands can be in view. Use the one with the higher landmark score and
     * ignore the other. The sample used the first filled slot instead. */
    if (results != NULL) {
        for (int i = 0; i < n_slots; i++) {
            const landmark_result_t* lm = &results[i];
            if (lm->num_points < LANDMARK_NUM_POINTS) {
                continue;
            }
            if (lm->hand_score < GESTURE_MIN_LANDMARK_SCORE) {
                continue;
            }
            if ((best == NULL) || (lm->hand_score > best->hand_score)) {
                best = lm;
            }
        }
    }

    g_raw = gesture_classify_ex(best, &g_metrics);

    if (g_raw == g_run_value) {
        if (g_run_length < GESTURE_STEADY_RESULTS) {
            g_run_length++;
        }
    } else {
        g_run_value  = g_raw;
        g_run_length = 1;
    }

    /* The label moves in both directions, so it also clears itself once the
     * hand has left. The sample never cleared it, which read as a wrong answer. */
    if (g_run_length >= GESTURE_STEADY_RESULTS) {
        g_stable = g_raw;
    }
}

gesture_t gesture_track_stable(void)
{
    return g_stable;
}

gesture_t gesture_track_raw(void)
{
    return g_raw;
}

const gesture_metrics_t* gesture_track_metrics(void)
{
    return &g_metrics;
}
