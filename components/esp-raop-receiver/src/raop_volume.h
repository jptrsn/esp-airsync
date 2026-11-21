/*
 * AirPlay volume -> digital gain transfer function.
 *
 * The AirPlay 1 (RAOP) SET_PARAMETER "volume" attribute is a *position* on the
 * remote's slider, not a dBFS gain figure: 0.0 is loudest, -30.0 is quietest
 * and -144.0 is a mute sentinel. Feeding that number straight into a dBFS
 * scale (the pre-fix behaviour) leaves the bottom of the slider audible
 * (-30 dBFS on a fixed-output PCM5102A line level) and the top at exactly
 * 0 dBFS, so loud material clips at the DAC/analog stage.
 *
 * This module is the single place that turns an AirPlay volume into a gain:
 * it maps the slider through the shairport-sync "standard" three-segment
 * attenuation curve onto a capped dBFS range and returns a cached fixed-point
 * gain for the audio hot path. Pure arithmetic, no ESP-IDF dependencies, so it
 * is covered by the host unit test in test/test_raop_volume.c.
 */

#ifndef RAOP_VOLUME_H
#define RAOP_VOLUME_H

#include <stddef.h>
#include <stdint.h>

/* AirPlay RAOP slider range and mute sentinel (raw SET_PARAMETER dB values). */
#define RAOP_VOLUME_MAX_AIRPLAY_DB   0.0f
#define RAOP_VOLUME_MIN_AIRPLAY_DB   (-30.0f)
#define RAOP_VOLUME_MUTE_DB          (-144.0f)

/*
 * Digital output range the slider is mapped onto, in dBFS. One place on
 * purpose: trim the ceiling here (or override it per build) to suit an amp
 * without touching the curve.
 *
 * MAX leaves 6 dB of headroom so full-scale programme material and
 * inter-sample peaks cannot clip the PCM5102A's analog stage; MIN is 48 dB
 * below MAX, which is silent at living-room listening levels while still
 * leaving ~7 bits of resolution at the bottom of the slider.
 */
#define RAOP_VOLUME_MAX_DBFS         (-6.0f)
#define RAOP_VOLUME_MIN_DBFS         (-54.0f)

/* Q16 unity gain, matching shairport-sync's fix_volume scale. */
#define RAOP_VOLUME_GAIN_UNITY       65536

/**
 * @brief Map an AirPlay volume onto the configured dBFS range.
 *
 * @param airplay_db  Raw SET_PARAMETER volume: 0.0 (loudest) to -30.0
 *                    (quietest). Values outside that range fall to
 *                    RAOP_VOLUME_MIN_DBFS (upstream's fail-safe: a malformed
 *                    high value must never map to full volume);
 *                    RAOP_VOLUME_MUTE_DB (-144.0) yields RAOP_VOLUME_MIN_DBFS
 *                    here and a hard zero from raop_volume_to_gain_q16().
 * @return Target level in dBFS, within [RAOP_VOLUME_MIN_DBFS, RAOP_VOLUME_MAX_DBFS].
 */
float raop_volume_to_dbfs(float airplay_db);

/**
 * @brief Linear gain for an AirPlay volume, in Q16 fixed point.
 *
 * Computed once per volume change (never per sample) and cached in the
 * receiver handle; the audio output task only multiplies by it.
 *
 * @param airplay_db  Raw SET_PARAMETER volume, see raop_volume_to_dbfs().
 * @return 0 for the mute sentinel (exact digital silence), otherwise
 *         1 .. RAOP_VOLUME_GAIN_UNITY-1 — the Q16 ceiling is capped below
 *         unity by RAOP_VOLUME_MAX_DBFS.
 */
int32_t raop_volume_to_gain_q16(float airplay_db);

/**
 * @brief Apply a Q16 gain to 16-bit PCM samples, in place.
 *
 * The only PCM scaling stage in the audio path. Integer multiply-and-shift
 * with saturation; gain 0 memsets and gain >= unity is a no-op so the common
 * cases cost nothing.
 *
 * @param samples    16-bit PCM samples (any channel count)
 * @param count      Number of samples
 * @param gain_q16   Gain from raop_volume_to_gain_q16()
 */
void raop_volume_scale_s16(int16_t *samples, size_t count, int32_t gain_q16);

#endif /* RAOP_VOLUME_H */
