/*
 * STM32 DAC audio output — implements audio_codec_api.
 *
 * Hardware: DAC1 CH1 (PA4) triggered by TIM6 via DMAMUX/DMA1.
 * The timer fires at the configured sample rate; each tick moves one 16-bit
 * sample from memory to the DAC DHR12R1 register.
 *
 * Application flow:
 *   audio_codec_configure()            — set sample rate, configure HW
 *   audio_codec_register_done_callback() — register TX-done callback
 *   audio_codec_write(dev, buf, size)  — prime first buffer into DMA
 *   audio_codec_start(dev, TX)         — enable DAC + timer
 *   [in callback] audio_codec_write()  — reload next buffer continuously
 *   audio_codec_stop(dev, TX)          — stop timer + DMA + DAC
 */

#define DT_DRV_COMPAT st_stm32_dac_audio

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/audio/codec.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_stm32.h>
#include <zephyr/drivers/clock_control/stm32_clock_control.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/cache.h>
#include <zephyr/logging/log.h>
#include <soc.h>
#include <stm32_ll_dac.h>
#include <stm32_ll_tim.h>

LOG_MODULE_REGISTER(dac_audio_stm32, CONFIG_AUDIO_CODEC_LOG_LEVEL);

struct dac_audio_cfg {
	DAC_TypeDef *dac;
	TIM_TypeDef *tim;
	const struct device *dma_dev;
	uint32_t dma_channel;
	uint32_t dma_slot;
	const struct pinctrl_dev_config *pcfg;
	const struct stm32_pclken *pclken;
	size_t pclk_len;
};

struct dac_audio_data {
	bool running;
	bool dma_configured;
	uint32_t dest_addr;
	struct dma_config dma_cfg;
	struct dma_block_config dma_block;
	audio_codec_tx_done_callback_t tx_cb;
	void *tx_cb_user_data;
};

static void dma_tx_callback(const struct device *dma_dev, void *user_data,
			     uint32_t channel, int status)
{
	const struct device *dev = user_data;
	const struct dac_audio_cfg *cfg = dev->config;
	struct dac_audio_data *data = dev->data;

	ARG_UNUSED(dma_dev);
	ARG_UNUSED(channel);

	if (status != 0) {
		LOG_ERR("DMA error status=%d, stopping", status);
		LL_TIM_DisableCounter(cfg->tim);
		LL_DAC_DisableDMAReq(cfg->dac, LL_DAC_CHANNEL_1);
		LL_DAC_Disable(cfg->dac, LL_DAC_CHANNEL_1);
		data->running = false;
		return;
	}

	if (data->tx_cb != NULL) {
		data->tx_cb(dev, data->tx_cb_user_data);
	}
}

static int dac_audio_configure(const struct device *dev,
				struct audio_codec_cfg *audio_cfg)
{
	const struct dac_audio_cfg *cfg = dev->config;
	struct dac_audio_data *data = dev->data;
	const struct device *clk = DEVICE_DT_GET(STM32_CLOCK_CONTROL_NODE);
	uint32_t sample_rate = (uint32_t)audio_cfg->dai_cfg.pcm.samplerate;
	uint32_t apb_rate, tim_clk, arr;
	int ret;

	/* Enable DAC and timer clocks */
	for (size_t i = 0; i < cfg->pclk_len; i++) {
		ret = clock_control_on(clk, (clock_control_subsys_t)&cfg->pclken[i]);
		if (ret < 0) {
			LOG_ERR("clock_control_on[%zu] failed: %d", i, ret);
			return ret;
		}
	}

	/* Get timer APB clock rate; timer clock = 2× APB when APB prescaler ≠ 1 */
	ret = clock_control_get_rate(clk,
				     (clock_control_subsys_t)&cfg->pclken[1],
				     &apb_rate);
	if (ret < 0) {
		LOG_ERR("clock_control_get_rate failed: %d", ret);
		return ret;
	}
	tim_clk = apb_rate * 2;

	if (sample_rate == 0 || (tim_clk % sample_rate) != 0) {
		LOG_ERR("Sample rate %u Hz not exactly achievable (tim_clk=%u)",
			sample_rate, tim_clk);
		return -EINVAL;
	}
	arr = tim_clk / sample_rate - 1;

	/* Configure PA4 as DAC analog output */
	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("pinctrl_apply_state failed: %d", ret);
		return ret;
	}

	/* TIM6: basic up-counter, update event → TRGO, period = 1/sample_rate */
	LL_TIM_SetPrescaler(cfg->tim, 0);
	LL_TIM_SetAutoReload(cfg->tim, arr);
	LL_TIM_SetTriggerOutput(cfg->tim, LL_TIM_TRGO_UPDATE);
	LL_TIM_DisableMasterSlaveMode(cfg->tim);

	/* DAC1 CH1: TIM6 TRGO trigger, DMA enabled, output buffer on, GPIO connected */
	LL_DAC_SetTriggerSource(cfg->dac, LL_DAC_CHANNEL_1,
				LL_DAC_TRIG_EXT_TIM6_TRGO);
	LL_DAC_EnableTrigger(cfg->dac, LL_DAC_CHANNEL_1);
	LL_DAC_SetOutputMode(cfg->dac, LL_DAC_CHANNEL_1,
			     LL_DAC_OUTPUT_MODE_NORMAL);
	LL_DAC_SetOutputBuffer(cfg->dac, LL_DAC_CHANNEL_1,
			       LL_DAC_OUTPUT_BUFFER_ENABLE);
	LL_DAC_SetOutputConnection(cfg->dac, LL_DAC_CHANNEL_1,
				   LL_DAC_OUTPUT_CONNECT_GPIO);

	/* Cache the DMA destination address (12-bit right-aligned register) */
	data->dest_addr = LL_DAC_DMA_GetRegAddr(cfg->dac, LL_DAC_CHANNEL_1,
						 LL_DAC_DMA_REG_DATA_12BITS_RIGHT_ALIGNED);

	/* Pre-build the DMA config; block is updated per write() call */
	memset(&data->dma_cfg, 0, sizeof(data->dma_cfg));
	data->dma_cfg.dma_slot            = cfg->dma_slot;
	data->dma_cfg.channel_direction   = MEMORY_TO_PERIPHERAL;
	data->dma_cfg.source_data_size    = 2;
	data->dma_cfg.dest_data_size      = 2;
	data->dma_cfg.source_burst_length = 1;
	data->dma_cfg.dest_burst_length   = 1;
	data->dma_cfg.complete_callback_en = 1;
	data->dma_cfg.error_callback_dis  = 0;
	data->dma_cfg.dma_callback        = dma_tx_callback;
	data->dma_cfg.user_data           = (void *)dev;
	data->dma_cfg.block_count         = 1;
	data->dma_cfg.head_block          = &data->dma_block;

	data->dma_configured = false;

	LOG_INF("configured: %u Hz, ARR=%u, tim_clk=%u Hz", sample_rate, arr, tim_clk);
	return 0;
}

static int dac_audio_write(const struct device *dev, uint8_t *buf, size_t size)
{
	const struct dac_audio_cfg *cfg = dev->config;
	struct dac_audio_data *data = dev->data;
	int ret;

#if defined(CONFIG_CACHE_MANAGEMENT) && defined(CONFIG_CPU_HAS_DCACHE)
	sys_cache_data_flush_range(buf, size);
#endif

	if (!data->dma_configured) {
		/* First write: full DMA configuration */
		memset(&data->dma_block, 0, sizeof(data->dma_block));
		data->dma_block.source_address  = (uint32_t)buf;
		data->dma_block.dest_address    = data->dest_addr;
		data->dma_block.block_size      = size;
		data->dma_block.source_addr_adj = DMA_ADDR_ADJ_INCREMENT;
		data->dma_block.dest_addr_adj   = DMA_ADDR_ADJ_NO_CHANGE;

		ret = dma_config(cfg->dma_dev, cfg->dma_channel, &data->dma_cfg);
		if (ret < 0) {
			LOG_ERR("dma_config failed: %d", ret);
			return ret;
		}
		data->dma_configured = true;
	} else {
		/* Subsequent writes from callback: reload src/size only */
		ret = dma_reload(cfg->dma_dev, cfg->dma_channel,
				 (uint32_t)buf, data->dest_addr, size);
		if (ret < 0) {
			LOG_ERR("dma_reload failed: %d", ret);
			return ret;
		}
	}

	ret = dma_start(cfg->dma_dev, cfg->dma_channel);
	if (ret < 0) {
		LOG_ERR("dma_start failed: %d", ret);
	}
	return ret;
}

static int dac_audio_start(const struct device *dev, audio_dai_dir_t dir)
{
	const struct dac_audio_cfg *cfg = dev->config;
	struct dac_audio_data *data = dev->data;

	if ((dir & AUDIO_DAI_DIR_TX) == 0) {
		return -ENOTSUP;
	}

	/* Enable DAC output; brief stabilisation delay before first conversion */
	LL_DAC_Enable(cfg->dac, LL_DAC_CHANNEL_1);
	k_busy_wait(10);

	LL_DAC_EnableDMAReq(cfg->dac, LL_DAC_CHANNEL_1);
	LL_TIM_EnableCounter(cfg->tim);

	data->running = true;
	LOG_INF("TX started");
	return 0;
}

static int dac_audio_stop(const struct device *dev, audio_dai_dir_t dir)
{
	const struct dac_audio_cfg *cfg = dev->config;
	struct dac_audio_data *data = dev->data;

	if ((dir & AUDIO_DAI_DIR_TX) == 0) {
		return -ENOTSUP;
	}

	LL_TIM_DisableCounter(cfg->tim);
	dma_stop(cfg->dma_dev, cfg->dma_channel);
	LL_DAC_DisableDMAReq(cfg->dac, LL_DAC_CHANNEL_1);
	LL_DAC_Disable(cfg->dac, LL_DAC_CHANNEL_1);

	data->running = false;
	data->dma_configured = false;

	LOG_INF("TX stopped");
	return 0;
}

static void dac_audio_start_output(const struct device *dev)
{
	dac_audio_start(dev, AUDIO_DAI_DIR_TX);
}

static void dac_audio_stop_output(const struct device *dev)
{
	dac_audio_stop(dev, AUDIO_DAI_DIR_TX);
}

static int dac_audio_set_property(const struct device *dev,
				   audio_property_t property,
				   audio_channel_t channel,
				   audio_property_value_t val)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(property);
	ARG_UNUSED(channel);
	ARG_UNUSED(val);
	return -ENOTSUP;
}

static int dac_audio_apply_properties(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int dac_audio_register_done_callback(const struct device *dev,
					     audio_codec_tx_done_callback_t tx_cb,
					     void *tx_user_data,
					     audio_codec_rx_done_callback_t rx_cb,
					     void *rx_user_data)
{
	struct dac_audio_data *data = dev->data;

	ARG_UNUSED(rx_cb);
	ARG_UNUSED(rx_user_data);

	data->tx_cb           = tx_cb;
	data->tx_cb_user_data = tx_user_data;
	return 0;
}

static const struct audio_codec_api dac_audio_codec_api = {
	.configure              = dac_audio_configure,
	.start_output           = dac_audio_start_output,
	.stop_output            = dac_audio_stop_output,
	.set_property           = dac_audio_set_property,
	.apply_properties       = dac_audio_apply_properties,
	.start                  = dac_audio_start,
	.stop                   = dac_audio_stop,
	.write                  = dac_audio_write,
	.register_done_callback = dac_audio_register_done_callback,
};

static int dac_audio_init(const struct device *dev)
{
	const struct dac_audio_cfg *cfg = dev->config;

	if (!device_is_ready(cfg->dma_dev)) {
		LOG_ERR("DMA device not ready");
		return -ENODEV;
	}

	return 0;
}

#define DAC_AUDIO_INIT(index)							\
	PINCTRL_DT_INST_DEFINE(index);						\
										\
	static const struct stm32_pclken pclken_##index[] =			\
		STM32_DT_INST_CLOCKS(index);					\
										\
	static struct dac_audio_data dac_audio_data_##index;			\
										\
	static const struct dac_audio_cfg dac_audio_cfg_##index = {		\
		.dac         = (DAC_TypeDef *)DT_INST_REG_ADDR_BY_NAME(index, dac), \
		.tim         = (TIM_TypeDef *)DT_INST_REG_ADDR_BY_NAME(index, tim), \
		.dma_dev     = DEVICE_DT_GET(STM32_DMA_CTLR(index, tx)),	\
		.dma_channel = DT_INST_DMAS_CELL_BY_NAME(index, tx, channel),	\
		.dma_slot    = STM32_DMA_SLOT(index, tx, slot),			\
		.pcfg        = PINCTRL_DT_INST_DEV_CONFIG_GET(index),		\
		.pclken      = pclken_##index,					\
		.pclk_len    = DT_INST_NUM_CLOCKS(index),			\
	};									\
										\
	DEVICE_DT_INST_DEFINE(index, dac_audio_init, NULL,			\
			      &dac_audio_data_##index,				\
			      &dac_audio_cfg_##index,				\
			      POST_KERNEL,					\
			      CONFIG_AUDIO_CODEC_INIT_PRIORITY,			\
			      &dac_audio_codec_api);

DT_INST_FOREACH_STATUS_OKAY(DAC_AUDIO_INIT)
