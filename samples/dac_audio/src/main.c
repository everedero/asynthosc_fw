/*
 * DAC audio sample — 440 Hz sine wave via audio_codec_api.
 *
 * The driver is configured for 48 kHz.  A Q16.16 fixed-point phase
 * accumulator selects entries from a pre-computed 256-point sine table
 * scaled to the DAC's 12-bit full-scale range (0–4095, midpoint 2048).
 *
 * Audio flows entirely through DMA callbacks; main() sleeps forever after
 * priming the first buffer and starting the codec.
 */

#include <zephyr/kernel.h>
#include <zephyr/audio/codec.h>
#include <zephyr/logging/log.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

LOG_MODULE_REGISTER(dac_audio_sample, LOG_LEVEL_INF);

#define SAMPLE_RATE   48000U
#define BLOCK_SAMPLES 256U
#define BLOCK_BYTES   (BLOCK_SAMPLES * sizeof(uint16_t))

/* DDS phase step: freq × 2^32 / sample_rate; index = top 8 bits of accumulator */
#define FREQ_STEP_440HZ  ((uint32_t)((440ULL << 32) / SAMPLE_RATE))

static const struct device *codec_dev = DEVICE_DT_GET(DT_ALIAS(codec0));

/* Two alternating buffers; the callback fills whichever just completed */
static uint16_t audio_bufs[2][BLOCK_SAMPLES];
static uint8_t  active_buf;

/* Phase accumulator shared between fill calls (ISR-only, no lock needed) */
static uint32_t phase_acc;

static volatile uint32_t callback_count;

/* Sine table: 256 entries, 12-bit amplitude centred at 2048 */
static uint16_t sine_table[256];

/*
 * ~160 mV peak on 3.3 V DAC reference — instrument level for Focusrite
 * Scarlett Hi-Z input.  Full-scale (2047) would be ~1.65 V peak (+3.6 dBu),
 * roughly 18 dB too hot for a passive guitar jack.
 */
#define SINE_AMPLITUDE 200

static void build_sine_table(void)
{
	for (int i = 0; i < 256; i++) {
		sine_table[i] = (uint16_t)(2048.0 + SINE_AMPLITUDE * sin(2.0 * M_PI * i / 256.0));
	}
}

static void fill_buffer(uint16_t *buf)
{
	for (uint32_t i = 0; i < BLOCK_SAMPLES; i++) {
		uint8_t idx = (uint8_t)(phase_acc >> 24);
		buf[i] = sine_table[idx];
		phase_acc += FREQ_STEP_440HZ;
	}
}

static void tx_done(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	callback_count++;

	/* Toggle buffer and fill the one that just finished */
	active_buf ^= 1;
	fill_buffer(audio_bufs[active_buf]);
	audio_codec_write(dev, (uint8_t *)audio_bufs[active_buf], BLOCK_BYTES);
}

int main(void)
{
	struct audio_codec_cfg codec_cfg = {
		.mclk_freq = 0,
		.dai_type  = AUDIO_DAI_TYPE_PCM,
		.dai_route = AUDIO_ROUTE_PLAYBACK,
		.dai_cfg.pcm = {
			.dir          = AUDIO_DAI_DIR_TX,
			.pcm_width    = AUDIO_PCM_WIDTH_16_BITS,
			.channels     = 1,
			.block_size   = BLOCK_BYTES,
			.samplerate   = AUDIO_PCM_RATE_48K,
		},
	};
	int ret;

	if (!device_is_ready(codec_dev)) {
		LOG_ERR("codec device not ready");
		return -ENODEV;
	}

	build_sine_table();

	ret = audio_codec_configure(codec_dev, &codec_cfg);
	if (ret < 0) {
		LOG_ERR("audio_codec_configure failed: %d", ret);
		return ret;
	}

	ret = audio_codec_register_done_callback(codec_dev, tx_done, NULL, NULL, NULL);
	if (ret < 0) {
		LOG_ERR("audio_codec_register_done_callback failed: %d", ret);
		return ret;
	}

	/* Prime buffer 0 into DMA (transfer starts when start() enables the timer) */
	fill_buffer(audio_bufs[0]);
	active_buf = 0;

	ret = audio_codec_write(codec_dev, (uint8_t *)audio_bufs[0], BLOCK_BYTES);
	if (ret < 0) {
		LOG_ERR("audio_codec_write (prime) failed: %d", ret);
		return ret;
	}

	ret = audio_codec_start(codec_dev, AUDIO_DAI_DIR_TX);
	if (ret < 0) {
		LOG_ERR("audio_codec_start failed: %d", ret);
		return ret;
	}

	LOG_INF("440 Hz sine running at %u Hz on PA4", SAMPLE_RATE);

	/* Print DMA callback rate once per second — 48000/256 ≈ 187 cb/s expected */
	while (true) {
		k_sleep(K_SECONDS(1));
		LOG_INF("DMA callbacks/s: %u", callback_count);
		callback_count = 0;
	}

	return 0;
}
