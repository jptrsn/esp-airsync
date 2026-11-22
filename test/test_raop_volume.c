/*
 * Host unit test for the AirPlay volume transfer function.
 *
 * Not built by idf.py (the IDF build only walks components/). Build and run
 * natively:
 *
 *   make -C test && ./test/test_raop_volume
 *
 * Covers the acceptance criteria from issue #3: endpoints, monotonicity,
 * continuity, the mute sentinel, headroom below full scale, and the
 * non-linear (audio-taper) shape.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raop_volume.h"

#define TEST_PI 3.14159265358979323846

static int failures = 0;

#define CHECK(cond, ...)                                                      \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                      \
            printf(__VA_ARGS__);                                              \
            printf("\n");                                                     \
            failures++;                                                       \
        }                                                                     \
    } while (0)

#define CHECK_NEAR(a, b, eps, ...) CHECK(fabsf((a) - (b)) <= (eps), __VA_ARGS__)

/* ---------- endpoints ---------- */

static void test_endpoints(void)
{
    /* Slider at maximum sits at the capped ceiling, not 0 dBFS. */
    CHECK_NEAR(raop_volume_to_dbfs(0.0f), RAOP_VOLUME_MAX_DBFS, 0.01f,
               "volume 0.0 -> %.3f dBFS, expected %.3f",
               raop_volume_to_dbfs(0.0f), RAOP_VOLUME_MAX_DBFS);

    /* Slider at minimum reaches the floor: 48 dB below the ceiling. */
    CHECK_NEAR(raop_volume_to_dbfs(-30.0f), RAOP_VOLUME_MIN_DBFS, 0.01f,
               "volume -30.0 -> %.3f dBFS, expected %.3f",
               raop_volume_to_dbfs(-30.0f), RAOP_VOLUME_MIN_DBFS);

    /*
     * >= 45 dB of attenuation from top to bottom of the slider
     * (issue target: min must be inaudible at normal listening levels).
     */
    float span = raop_volume_to_dbfs(0.0f) - raop_volume_to_dbfs(-30.0f);
    CHECK(span >= 45.0f, "slider spans only %.2f dB, need >= 45 dB", span);

    /* Out-of-range slider values fail safe to the floor (upstream
     * vol2attn behaviour): a malformed high value must never map to full
     * volume, and a below-range value must not be louder than the floor. */
    CHECK_NEAR(raop_volume_to_dbfs(5.0f), RAOP_VOLUME_MIN_DBFS, 0.01f,
               "volume +5.0 must fail safe to the floor");
    CHECK(raop_volume_to_gain_q16(5.0f) <= raop_volume_to_gain_q16(-30.0f),
          "volume +5.0 must not be louder than the slider bottom");
    CHECK_NEAR(raop_volume_to_dbfs(-100.0f), RAOP_VOLUME_MIN_DBFS, 0.01f,
               "volume -100 must clamp to the floor");

    /* NaN (e.g. `volume: nan` from a corrupt SET_PARAMETER) must fail safe
     * to the floor too: every float comparison is false for NaN, so the
     * range test must be shaped to catch it. Regression pin for review
     * round 2. */
    CHECK(raop_volume_to_dbfs(NAN) == RAOP_VOLUME_MIN_DBFS,
          "NaN must fail safe to the floor");
    CHECK(raop_volume_to_gain_q16(NAN) == raop_volume_to_gain_q16(-30.0f),
          "NaN gain must equal the floor gain, not the ceiling");
    CHECK(raop_volume_to_gain_q16(NAN) < RAOP_VOLUME_GAIN_UNITY / 2,
          "NaN gain must be far below unity");
}

/* ---------- initial state by volume mode ---------- */

static void test_initial_gain(void)
{
    /* SOFTWARE: slider top, capped below unity (headroom). */
    CHECK(raop_volume_initial_gain_q16(1) ==
              raop_volume_to_gain_q16(RAOP_VOLUME_MAX_AIRPLAY_DB),
          "SOFTWARE initial gain must be the slider-top gain");
    CHECK(raop_volume_initial_gain_q16(1) < RAOP_VOLUME_GAIN_UNITY,
          "SOFTWARE initial gain must sit below unity");

    /* HARDWARE: exact unity — the library must never scale PCM. */
    CHECK(raop_volume_initial_gain_q16(0) == RAOP_VOLUME_GAIN_UNITY,
          "HARDWARE initial gain must be exact unity");
}

/* ---------- mute sentinel ---------- */

static void test_mute(void)
{
    CHECK(raop_volume_to_gain_q16(RAOP_VOLUME_MUTE_DB) == 0,
          "mute sentinel must produce a zero gain, got %d",
          (int)raop_volume_to_gain_q16(RAOP_VOLUME_MUTE_DB));
    CHECK(raop_volume_to_gain_q16(-200.0f) == 0,
          "anything at/below the mute sentinel must produce a zero gain");

    /* Exact digital silence through the scaler, even from full-scale input. */
    int16_t samples[64];
    for (size_t i = 0; i < 64; i++) {
        samples[i] = (i % 2) ? 32767 : -32768;
    }
    raop_volume_scale_s16(samples, 64, raop_volume_to_gain_q16(RAOP_VOLUME_MUTE_DB));
    for (size_t i = 0; i < 64; i++) {
        CHECK(samples[i] == 0, "mute left sample[%zu] = %d", i, samples[i]);
    }
}

/* ---------- headroom ---------- */

static void test_headroom(void)
{
    /* >= 3 dB of headroom below full scale at the top of the slider. */
    float headroom_db = -raop_volume_to_dbfs(0.0f);
    CHECK(headroom_db >= 3.0f, "only %.2f dB of headroom at volume 0.0, need >= 3",
          headroom_db);
    CHECK(raop_volume_to_gain_q16(0.0f) < RAOP_VOLUME_GAIN_UNITY,
          "gain at volume 0.0 must stay below unity (no 0 dBFS path)");

    /* Full-scale sine: no sample may clip at the DAC input. */
    enum { N = 4410 };  /* one period of a 10 Hz tone at 44.1 kHz */
    int16_t sine[N];
    for (int i = 0; i < N; i++) {
        sine[i] = (int16_t)lround(32767.0 * sin(2.0 * TEST_PI * i / N));
    }
    int32_t peak_in = 0;
    for (int i = 0; i < N; i++) {
        int32_t a = sine[i] < 0 ? -sine[i] : sine[i];
        if (a > peak_in) peak_in = a;
    }
    CHECK(peak_in == 32767, "test sine must exercise full scale, peak=%d", (int)peak_in);

    raop_volume_scale_s16(sine, N, raop_volume_to_gain_q16(0.0f));
    for (int i = 0; i < N; i++) {
        int32_t a = sine[i] < 0 ? -sine[i] : sine[i];
        /* ~6 dB ceiling => peak lands near half scale, nowhere near the rail. */
        CHECK(a < 32767, "sine reached the full-scale rail at sample %d", i);
    }

    /* Full-scale worst-case signal (alternating +/-32767 rail-to-rail). */
    int16_t noise[512];
    for (size_t i = 0; i < 512; i++) {
        noise[i] = (i % 2) ? 32767 : -32768;
    }
    raop_volume_scale_s16(noise, 512, raop_volume_to_gain_q16(0.0f));
    for (size_t i = 0; i < 512; i++) {
        CHECK(noise[i] > -32768 && noise[i] < 32767,
              "full-scale rail-to-rail reached the rail at sample %zu", i);
    }
}

/* ---------- monotonicity and continuity ---------- */

static void test_monotonic_continuous(void)
{
    const float step = 0.05f;  /* sub-unit steps: no plateau may hide */
    float prev = raop_volume_to_dbfs(0.0f);
    for (float v = -step; v >= RAOP_VOLUME_MIN_AIRPLAY_DB - 0.001f; v -= step) {
        float cur = raop_volume_to_dbfs(v);

        /* Monotonic: quieter slider must never get louder. */
        CHECK(cur <= prev, "reversal at volume %.2f: %.3f dBFS after %.3f dBFS",
              v, cur, prev);
        /* Continuous: no jump beyond what one step of the steepest segment
         * (48 dB over 13 slider units) plus float slack can explain. */
        CHECK(prev - cur <= 48.0f * step / 13.0f + 0.05f,
              "discontinuity at volume %.2f: %.3f dB jump", v, prev - cur);
        /* Within the declared range. */
        CHECK(cur <= RAOP_VOLUME_MAX_DBFS + 0.001f && cur >= RAOP_VOLUME_MIN_DBFS - 0.001f,
              "volume %.2f escaped the range: %.3f dBFS", v, cur);
        prev = cur;
    }

    /* The gain quantisation must not introduce plateaus either: every slider
     * unit is expected to move the gain by at least one Q16 count. */
    int32_t prev_gain = raop_volume_to_gain_q16(0.0f);
    for (float v = -1.0f; v >= RAOP_VOLUME_MIN_AIRPLAY_DB; v -= 1.0f) {
        int32_t gain = raop_volume_to_gain_q16(v);
        CHECK(gain < prev_gain, "gain plateaued between AirPlay %.1f and %.1f (Q16 %d)",
              v + 1.0f, v, (int)gain);
        prev_gain = gain;
    }
}

/* ---------- non-linearity (audio taper) ---------- */

static void test_nonlinear(void)
{
    float top_step = raop_volume_to_dbfs(0.0f) - raop_volume_to_dbfs(-1.0f);
    float bottom_step = raop_volume_to_dbfs(-29.0f) - raop_volume_to_dbfs(-30.0f);

    /*
     * Perceptually the point of the curve: the first slider unit must move at
     * least 2x less dB than the last one (fine control near the top, fast
     * roll-off into silence).
     */
    CHECK(bottom_step >= 2.0f * top_step,
          "scale too linear: first unit %.3f dB, last unit %.3f dB", top_step, bottom_step);

    /* And neither linear-in-dB nor linear-in-amplitude: the mid-point of the
     * slider must not be the mid-point of the dB range... */
    float mid = raop_volume_to_dbfs(-15.0f);
    float mid_linear_db = (RAOP_VOLUME_MAX_DBFS + RAOP_VOLUME_MIN_DBFS) / 2.0f;
    CHECK(fabsf(mid - mid_linear_db) > 1.0f,
          "curve looks linear-in-dB at the midpoint (%.3f vs %.3f dBFS)", mid, mid_linear_db);

    /* ...nor linear-in-amplitude (which would sit 20*log10(2) dB below the
     * ceiling). */
    float mid_linear_amp = RAOP_VOLUME_MAX_DBFS - 20.0f * log10f(2.0f);
    CHECK(fabsf(mid - mid_linear_amp) > 1.0f,
          "curve looks linear-in-amplitude at the midpoint (%.3f vs %.3f dBFS)",
          mid, mid_linear_amp);
}

/* ---------- scaler behaviour ---------- */

static void test_scaler(void)
{
    /* Unity gain is a no-op (early-out fast path). */
    int16_t in[8] = { 0, 1, -1, 100, -100, 32767, -32768, 12345 };
    int16_t ref[8];
    memcpy(ref, in, sizeof(in));
    raop_volume_scale_s16(in, 8, RAOP_VOLUME_GAIN_UNITY);
    CHECK(memcmp(in, ref, sizeof(in)) == 0, "unity gain altered the samples");

    /* Half scale. */
    int16_t half[4] = { 20000, -20000, 32767, -32768 };
    raop_volume_scale_s16(half, 4, RAOP_VOLUME_GAIN_UNITY / 2);
    CHECK(half[0] == 10000 && half[1] == -10000, "half gain wrong: %d %d",
          half[0], half[1]);
    CHECK(half[2] == 16383 && half[3] == -16384, "half gain wrong at rails: %d %d",
          half[2], half[3]);

    /* Gain >= unity is a documented pass-through no-op (the curve is capped
     * below unity, so a real slider position never reaches this branch). */
    int16_t hot[2] = { 32767, -32768 };
    raop_volume_scale_s16(hot, 2, RAOP_VOLUME_GAIN_UNITY * 2);
    CHECK(hot[0] == 32767 && hot[1] == -32768, "unity-or-above gain was not a no-op: %d %d",
          hot[0], hot[1]);

    /* The int32 product must be provable not to overflow before the shift:
     * worst |product| is the negative rail at the highest in-contract gain.
     * (Per-sample int16 range after the shift is a typing property, not a
     * behaviour worth asserting — this invariant is the one that could rot.) */
    CHECK((int64_t)32768 * (RAOP_VOLUME_GAIN_UNITY - 1) <= INT32_MAX,
          "Q16 product can overflow int32 at the rails");

    /* Every real slider position leaves rail inputs at the rails (saturating
     * multiply-shift must not wrap them to the opposite sign). */
    for (float v = 0.0f; v >= RAOP_VOLUME_MIN_AIRPLAY_DB; v -= 0.5f) {
        int16_t buf[16];
        for (size_t i = 0; i < 16; i++) {
            buf[i] = (i % 2) ? 32767 : -32768;
        }
        raop_volume_scale_s16(buf, 16, raop_volume_to_gain_q16(v));
        for (size_t i = 0; i < 16; i++) {
            CHECK((buf[i] >= 0) == (i % 2 == 1),
                  "volume %.2f flipped a rail sample's sign (wrapped?): %d", v, buf[i]);
        }
    }
}

/* ---------- documented curve values (regression pin) ---------- */

static void test_curve_table(void)
{
    /*
     * The transfer table from issue #3 (max=-6, min=-54). Pinning these
     * points guards against accidental retuning of the shairport-sync port.
     */
    struct { float airplay; float dbfs; } table[] = {
        {   0.0f,  -6.00f },
        {  -3.0f,  -8.40f },
        {  -6.0f, -10.80f },
        {  -9.0f, -13.20f },
        { -12.0f, -16.08f },
        { -15.0f, -20.40f },
        { -18.0f, -24.72f },
        { -21.0f, -29.04f },
        { -24.0f, -33.36f },
        { -27.0f, -42.92f },
        { -30.0f, -54.00f },
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        CHECK_NEAR(raop_volume_to_dbfs(table[i].airplay), table[i].dbfs, 0.01f,
                   "AirPlay %.1f -> %.3f dBFS, expected %.2f",
                   table[i].airplay, raop_volume_to_dbfs(table[i].airplay), table[i].dbfs);
    }
}

int main(void)
{
    test_endpoints();
    test_initial_gain();
    test_mute();
    test_headroom();
    test_monotonic_continuous();
    test_nonlinear();
    test_scaler();
    test_curve_table();

    if (failures) {
        printf("%d check(s) FAILED\n", failures);
        return 1;
    }
    printf("all raop_volume checks passed\n");
    return 0;
}
