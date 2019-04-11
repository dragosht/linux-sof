// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Keyon Jie <yang.jie@linux.intel.com>
//

#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/platform_device.h>
#include <sound/soc.h>
#include <sound/sof.h>
#include "sof-priv.h"

/*
 * Register IO
 *
 * The sof_io_xyz() wrappers are typically referenced in snd_sof_dsp_ops
 * structures and cannot be inlined.
 */

void sof_io_write(struct snd_sof_dev *sdev, void __iomem *addr, u32 value)
{
	writel(value, addr);
}
EXPORT_SYMBOL(sof_io_write);

u32 sof_io_read(struct snd_sof_dev *sdev, void __iomem *addr)
{
	return readl(addr);
}
EXPORT_SYMBOL(sof_io_read);

void sof_io_write64(struct snd_sof_dev *sdev, void __iomem *addr, u64 value)
{
	writeq(value, addr);
}
EXPORT_SYMBOL(sof_io_write64);

u64 sof_io_read64(struct snd_sof_dev *sdev, void __iomem *addr)
{
	return readq(addr);
}
EXPORT_SYMBOL(sof_io_read64);

/*
 * IPC Mailbox IO
 */

void sof_mailbox_write(struct snd_sof_dev *sdev, u32 offset,
		       void *message, size_t bytes)
{
	void __iomem *dest = sdev->bar[sdev->mailbox_bar] + offset;

	memcpy_toio(dest, message, bytes);
}
EXPORT_SYMBOL(sof_mailbox_write);

void sof_mailbox_read(struct snd_sof_dev *sdev, u32 offset,
		      void *message, size_t bytes)
{
	void __iomem *src = sdev->bar[sdev->mailbox_bar] + offset;

	memcpy_fromio(message, src, bytes);
}
EXPORT_SYMBOL(sof_mailbox_read);

/*
 * Memory copy.
 */

void sof_block_write(struct snd_sof_dev *sdev, u32 bar, u32 offset, void *src,
		     size_t size)
{
	void __iomem *dest = sdev->bar[bar] + offset;
	const u8 *src_byte = src;
	u32 affected_mask;
	u32 tmp;
	int m, n;

	m = size / 4;
	n = size % 4;

	/* __iowrite32_copy use 32bit size values so divide by 4 */
	__iowrite32_copy(dest, src, m);

	if (n) {
		affected_mask = (1 << (8 * n)) - 1;

		/* first read the 32bit data of dest, then change affected
		 * bytes, and write back to dest. For unaffected bytes, it
		 * should not be changed
		 */
		tmp = ioread32(dest + m * 4);
		tmp &= ~affected_mask;

		tmp |= *(u32 *)(src_byte + m * 4) & affected_mask;
		iowrite32(tmp, dest + m * 4);
	}
}
EXPORT_SYMBOL(sof_block_write);

void sof_block_read(struct snd_sof_dev *sdev, u32 bar, u32 offset, void *dest,
		    size_t size)
{
	void __iomem *src = sdev->bar[bar] + offset;

	memcpy_fromio(dest, src, size);
}
EXPORT_SYMBOL(sof_block_read);

int sof_dai_set_cur_hw_config(struct snd_sof_dai *dai, int i)
{
	struct sof_ipc_dai_config *config;
	struct snd_sof_hw_config *sof_hw_config;

	if (!dai || (dai->dai_config && dai->dai_config->type != SOF_DAI_INTEL_SSP)) {
		dev_err(dai->sdev->dev, "error: invalid dai\n");

		return -EINVAL;
	}

	if (i < 0 || i >= dai->num_hw_configs) {
		dev_err(dai->sdev->dev, "error: dai: %s invalid hw_config index: %d\n",
			dai->name, i);
		return -EINVAL;
	}

	dai->cur_hw_config = i;
	sof_hw_config = &dai->hw_config[i];
	config = dai->dai_config;

	config->format = sof_hw_config->format;
	config->ssp.mclk_rate = sof_hw_config->mclk_rate;
	config->ssp.bclk_rate = sof_hw_config->bclk_rate;
	config->ssp.fsync_rate = sof_hw_config->fsync_rate;
	config->ssp.tdm_slots = sof_hw_config->tdm_slots;
	config->ssp.tdm_slot_width = sof_hw_config->tdm_slot_width;
	config->ssp.mclk_direction = sof_hw_config->mclk_direction;
	config->ssp.rx_slots = sof_hw_config->rx_slots;
	config->ssp.tx_slots = sof_hw_config->tx_slots;

	return 0;
}

int sof_dai_load_hw_config(struct snd_sof_dai *dai)
{
	struct snd_sof_dev *sdev = dai->sdev;
	struct sof_ipc_dai_config *config = dai->dai_config;
	struct sof_ipc_reply reply;
	u32 size = sizeof(*config);
	int ret = 0;

	dev_dbg(sdev->dev, "dai: %s loading hardware configuration: %d/%d\n",
		dai->name, dai->cur_hw_config, dai->num_hw_configs);

	dev_dbg(sdev->dev, "config SSP%d fmt 0x%x mclk %d bclk %d fclk %d width (%d)%d slots %d mclk id %d quirks %d\n",
		config->dai_index, config->format,
		config->ssp.mclk_rate, config->ssp.bclk_rate,
		config->ssp.fsync_rate, config->ssp.sample_valid_bits,
		config->ssp.tdm_slot_width, config->ssp.tdm_slots,
		config->ssp.mclk_id, config->ssp.quirks);

	/* send message to DSP */
	ret = sof_ipc_tx_message(sdev->ipc,
				 config->hdr.cmd, config, size, &reply,
				 sizeof(reply));

	if (ret < 0) {
		dev_err(sdev->dev, "error: failed to set DAI config for SSP%d\n",
			config->dai_index);
		return ret;
	}

	return ret;
}
