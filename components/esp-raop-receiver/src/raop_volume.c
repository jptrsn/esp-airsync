#include "raop_volume.h"

#include <math.h>
#include <string.h>

/*
 * Port of vol2attn() from shairport-sync (common.c), Mike Brady, GPL v2+.
 * See https://github.com/mikebrady/shairport-sync and the tangentsoft
 * attenuator-taper notes it cites (http://tangentsoft.net/audio/atten.html):
 * a good volume control moves slowly in dB at the top of the range and much
 * faster at the bottom. Three straight segments in (x = AirPlay slider dB,
 * y = output dBFS) give that audio-taper shape: a gentle slope over the top
 * third, a steeper middle, and a steep run into the floor.
 *
 * The curve is evaluated in the dB×100 units upstream uses, so it stays
 * reproducible in the host unit test.
 */
#define AIRPLAY_SPAN_DB        (-30.0f)  /* slider travel on the x-axis, in dB */

static float to_db100(float db) {
    return db * 100.0f;
}

/* Longest (least steep) segment runs from the top of the slider down. */
static float top_segment_slope(long range_db100) {
    float slope = -(float)range_db100 / 2.0f;
    if (-(float)range_db100 > slope) {
        slope = (float)range_db100;
    }
    return slope;
}

float raop_volume_to_dbfs(float airplay_db)
{
    /*
     * Upstream's structure, deliberately: vol2attn() evaluates the curve
     * only for values *provably* in range; anything else — the mute
     * sentinel, an out-of-range figure, or NaN from a corrupt
     * SET_PARAMETER — goes to the bottom of the range ("for safety" in
     * shairport-sync's words). The negated in-range test is load-bearing:
     * every comparison is false for NaN, so an inverted check shape would
     * let NaN fall through to the ceiling (loudest slider position).
     */
    if (!(airplay_db <= RAOP_VOLUME_MAX_AIRPLAY_DB &&
          airplay_db >= RAOP_VOLUME_MIN_AIRPLAY_DB)) {
        return RAOP_VOLUME_MIN_DBFS;
    }

    const float max_db100 = to_db100(RAOP_VOLUME_MAX_DBFS);
    const float min_db100 = to_db100(RAOP_VOLUME_MIN_DBFS);
    const long range_db100 = (long)(max_db100 - min_db100);   /* positive */
    const float first_slope = top_segment_slope(range_db100);

    /* {slider dB where the segment starts, output dB*100 it reaches at -30} */
    const float lines[3][2] = {
        { 0.0f,  first_slope },
        { -5.0f, first_slope - ((float)range_db100 + first_slope) / 2.0f },
        { -17.0f, -(float)range_db100 },
    };

    float attenuation_db100 = 0.0f;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        if (airplay_db > lines[i][0]) {
            continue;
        }
        float span = AIRPLAY_SPAN_DB - lines[i][0];
        if (span == 0.0f) {
            continue;
        }
        float tvol = lines[i][1] * (airplay_db - lines[i][0]) / span;
        if (tvol < attenuation_db100) {
            attenuation_db100 = tvol;
        }
    }

    return (attenuation_db100 + max_db100) / 100.0f;
}

int32_t raop_volume_to_gain_q16(float airplay_db)
{
    /* Mute is a hard zero, not the bottom of the curve. */
    if (airplay_db <= RAOP_VOLUME_MUTE_DB) {
        return 0;
    }

    /*
     * shairport-sync's fix_volume pattern: 65536 * 10^(dB/20). The powf()
     * runs once per volume change, never in the sample loop.
     */
    float gain = (float)RAOP_VOLUME_GAIN_UNITY * powf(10.0f, raop_volume_to_dbfs(airplay_db) / 20.0f);

    if (gain >= (float)RAOP_VOLUME_GAIN_UNITY) {
        return RAOP_VOLUME_GAIN_UNITY - 1;
    }
    if (gain < 1.0f) {
        return 0;
    }
    return (int32_t)gain;
}

int32_t raop_volume_initial_gain_q16(int software_mode)
{
    /*
     * Initial state before the remote sends SET_PARAMETER volume:
     * SOFTWARE sits at the top of the slider (the dBFS ceiling, capped
     * below unity by RAOP_VOLUME_MAX_DBFS); HARDWARE pins the buffer at
     * unity because in that mode the consumer owns all gain and the
     * library must never scale PCM.
     */
    return software_mode ? raop_volume_to_gain_q16(RAOP_VOLUME_MAX_AIRPLAY_DB)
                         : (int32_t)RAOP_VOLUME_GAIN_UNITY;
}

void raop_volume_scale_s16(int16_t *samples, size_t count, int32_t gain_q16)
{
    if (!samples || count == 0) {
        return;
    }
    if (gain_q16 <= 0) {
        memset(samples, 0, count * sizeof(int16_t));
        return;
    }
    if (gain_q16 >= RAOP_VOLUME_GAIN_UNITY) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        int32_t scaled = (samples[i] * gain_q16) >> 16;
        if (scaled > 32767) {
            scaled = 32767;
        } else if (scaled < -32768) {
            scaled = -32768;
        }
        samples[i] = (int16_t)scaled;
    }
}
