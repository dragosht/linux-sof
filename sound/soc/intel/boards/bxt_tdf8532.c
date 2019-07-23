/*
 * Intel Broxton-P I2S Machine Driver for IVI reference platform
 * Copyright (c) 2017, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>

static const struct snd_kcontrol_new broxton_tdf8532_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

static const struct snd_soc_dapm_widget broxton_tdf8532_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("DiranaCp", NULL),
	SND_SOC_DAPM_HP("DiranaPb", NULL),
	SND_SOC_DAPM_MIC("HdmiIn", NULL),
	SND_SOC_DAPM_MIC("TestPinCp", NULL),
	SND_SOC_DAPM_HP("TestPinPb", NULL),
	SND_SOC_DAPM_MIC("BtHfpDl", NULL),
	SND_SOC_DAPM_HP("BtHfpUl", NULL),
	SND_SOC_DAPM_MIC("ModemDl", NULL),
	SND_SOC_DAPM_HP("ModemUl", NULL),
};

static const struct snd_soc_dapm_route broxton_tdf8532_map[] = {
	/* Speaker BE connections */
	{ "Speaker", NULL, "ssp4 Tx"},
	{ "ssp4 Tx", NULL, "codec0_out"},

	{ "dirana_in", NULL, "ssp2 Rx"},
	{ "ssp2 Rx", NULL, "DiranaCp"},

	{ "dirana_aux_in", NULL, "ssp2 Rx"},
	{ "ssp2 Rx", NULL, "DiranaCp"},

	{ "dirana_tuner_in", NULL, "ssp2 Rx"},
	{ "ssp2 Rx", NULL, "DiranaCp"},

	{ "DiranaPb", NULL, "ssp2 Tx"},
	{ "ssp2 Tx", NULL, "dirana_out"},

	{ "hdmi_ssp1_in", NULL, "ssp1 Rx"},
	{ "ssp1 Rx", NULL, "HdmiIn"},

	{ "TestPin_ssp5_in", NULL, "ssp5 Rx"},
	{ "ssp5 Rx", NULL, "TestPinCp"},

	{ "TestPinPb", NULL, "ssp5 Tx"},
	{ "ssp5 Tx", NULL, "TestPin_ssp5_out"},

	{ "BtHfp_ssp0_in", NULL, "ssp0 Rx"},
	{ "ssp0 Rx", NULL, "BtHfpDl"},

	{ "BtHfpUl", NULL, "ssp0 Tx"},
	{ "ssp0 Tx", NULL, "BtHfp_ssp0_out"},

	{ "Modem_ssp3_in", NULL, "ssp3 Rx"},
	{ "ssp3 Rx", NULL, "ModemDl"},

	{ "ModemUl", NULL, "ssp3 Tx"},
	{ "ssp3 Tx", NULL, "Modem_ssp3_out"},
};

static int bxt_tdf8532_ssp2_fixup(struct snd_soc_pcm_runtime *rtd,
				  struct snd_pcm_hw_params *params)
{
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* set SSP to 32 bit */
	snd_mask_none(fmt);
	snd_mask_set(fmt, SNDRV_PCM_FORMAT_S32_LE);

	return 0;
}

/* broxton digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(speaker,
	DAILINK_COMP_ARRAY(COMP_CPU("Speaker Pin")));

SND_SOC_DAILINK_DEF(dirana_capture,
	DAILINK_COMP_ARRAY(COMP_CPU("Dirana Cp Pin")));

SND_SOC_DAILINK_DEF(dirana_playback,
	DAILINK_COMP_ARRAY(COMP_CPU("Dirana Pb Pin")));

SND_SOC_DAILINK_DEF(testpin_capture,
	DAILINK_COMP_ARRAY(COMP_CPU("TestPin Cp Pin")));

SND_SOC_DAILINK_DEF(testpin_playback,
	DAILINK_COMP_ARRAY(COMP_CPU("TestPin Pb Pin")));

SND_SOC_DAILINK_DEF(bthfp_capture,
	DAILINK_COMP_ARRAY(COMP_CPU("BtHfp Cp Pin")));

SND_SOC_DAILINK_DEF(bthfp_playback,
	DAILINK_COMP_ARRAY(COMP_CPU("BtHfp Pb Pin")));

SND_SOC_DAILINK_DEF(modem_capture,
	DAILINK_COMP_ARRAY(COMP_CPU("Modem Cp Pin")));

SND_SOC_DAILINK_DEF(modem_playback,
	DAILINK_COMP_ARRAY(COMP_CPU("Modem Pb Pin")));

SND_SOC_DAILINK_DEF(hdmi_capture,
	DAILINK_COMP_ARRAY(COMP_CPU("HDMI Cp Pin")));

SND_SOC_DAILINK_DEF(dirana_aux_capture,
	DAILINK_COMP_ARRAY(COMP_CPU("Dirana Aux Cp Pin")));

SND_SOC_DAILINK_DEF(dirana_tuner_capture,
	DAILINK_COMP_ARRAY(COMP_CPU("Dirana Tuner Cp Pin")));

SND_SOC_DAILINK_DEF(probe_playback,
	DAILINK_COMP_ARRAY(COMP_CPU("Compress Probe0 Pin")));

SND_SOC_DAILINK_DEF(probe_capture,
	DAILINK_COMP_ARRAY(COMP_CPU("Compress Probe1 Pin")));

SND_SOC_DAILINK_DEF(tracebuffer0,
	DAILINK_COMP_ARRAY(COMP_CPU("TraceBuffer0 Pin")));

SND_SOC_DAILINK_DEF(tracebuffer1,
	DAILINK_COMP_ARRAY(COMP_CPU("TraceBuffer1 Pin")));

SND_SOC_DAILINK_DEF(ssp0_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP0 Pin")));

SND_SOC_DAILINK_DEF(ssp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP1 Pin")));

SND_SOC_DAILINK_DEF(ssp2_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP2 Pin")));

SND_SOC_DAILINK_DEF(ssp3_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP3 Pin")));

SND_SOC_DAILINK_DEF(ssp4_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP4 Pin")));

SND_SOC_DAILINK_DEF(ssp4_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-INT34C3:00",
				      "tdf8532-hifi")));

SND_SOC_DAILINK_DEF(ssp5_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP5 Pin")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("0000:00:0e.0")));

static struct snd_soc_dai_link broxton_tdf8532_dais[] = {
	/* Front End DAI links */
	{
		.name = "Speaker Port",
		.stream_name = "Speaker",
		.nonatomic = 1,
		.dynamic = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(speaker, dummy, platform),
	},
	{
		.name = "Dirana Capture Port",
		.stream_name = "Dirana Cp",
		.init = NULL,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(dirana_capture, dummy, platform),
	},
	{
		.name = "Dirana Playback Port",
		.stream_name = "Dirana Pb",
		.nonatomic = 1,
		.dynamic = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(dirana_playback, dummy, platform),
	},
	{
		.name = "TestPin Capture Port",
		.stream_name = "TestPin Cp",
		.init = NULL,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(testpin_capture, dummy, platform),
	},
	{
		.name = "TestPin Playback Port",
		.stream_name = "TestPin Pb",
		.nonatomic = 1,
		.dynamic = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(testpin_playback, dummy, platform),
	},
	{
		.name = "BtHfp Capture Port",
		.stream_name = "BtHfp Cp",
		.init = NULL,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(bthfp_capture, dummy, platform),
	},
	{
		.name = "BtHfp Playback Port",
		.stream_name = "BtHfp Pb",
		.nonatomic = 1,
		.dynamic = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(bthfp_playback, dummy, platform),
	},
	{
		.name = "Modem Capture Port",
		.stream_name = "Modem Cp",
		.init = NULL,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(modem_capture, dummy, platform),
	},
	{
		.name = "Modem Playback Port",
		.stream_name = "Modem Pb",
		.nonatomic = 1,
		.dynamic = 1,
		.trigger = {
			SND_SOC_DPCM_TRIGGER_POST,
			SND_SOC_DPCM_TRIGGER_POST
		},
		.dpcm_playback = 1,
		SND_SOC_DAILINK_REG(modem_playback, dummy, platform),
	},
	{
		.name = "HDMI Capture Port",
		.stream_name = "HDMI Cp",
		.init = NULL,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(hdmi_capture, dummy, platform),
	},
	{
		.name = "Dirana Aux Capture Port",
		.stream_name = "Dirana Aux Cp",
		.init = NULL,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(dirana_aux_capture, dummy, platform),
	},
	{
		.name = "Dirana Tuner Capture Port",
		.stream_name = "Dirana Tuner Cp",
		.init = NULL,
		.dpcm_capture = 1,
		.ignore_suspend = 1,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(dirana_tuner_capture, dummy, platform),
	},
	/* Probe DAI links*/
	{
		.name = "Bxt Compress Probe playback",
		.stream_name = "Probe Playback",
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(probe_playback, dummy, platform),
	},
	{
		.name = "Bxt Compress Probe capture",
		.stream_name = "Probe Capture",
		.init = NULL,
		.nonatomic = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(probe_capture, dummy, platform),
	},
	/* Trace Buffer DAI links */
	{
		.name = "Bxt Trace Buffer0",
		.stream_name = "Core 0 Trace Buffer",
		.capture_only = true,
		.ignore_suspend = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(tracebuffer0, dummy, platform),
	},
	{
		.name = "Bxt Trace Buffer1",
		.stream_name = "Core 1 Trace Buffer",
		.capture_only = true,
		.ignore_suspend = 1,
		.dynamic = 1,
		SND_SOC_DAILINK_REG(tracebuffer1, dummy, platform),
	},
	/* Back End DAI links */
	{
		/* SSP0 - BT */
		.name = "SSP0-Codec",
		.id = 0,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(ssp0_pin, dummy, platform),
	},
	{
		/* SSP1 - HDMI-In */
		.name = "SSP1-Codec",
		.id = 1,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(ssp1_pin, dummy, platform),
	},
	{
		/* SSP2 - Dirana */
		.name = "SSP2-Codec",
		.id = 2,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.dpcm_playback = 1,
		.no_pcm = 1,
		.be_hw_params_fixup = bxt_tdf8532_ssp2_fixup,
		SND_SOC_DAILINK_REG(ssp2_pin, dummy, platform),
	},
	{
		/* SSP3 - Modem */
		.name = "SSP3-Codec",
		.id = 3,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(ssp3_pin, dummy, platform),
	},
	{
		/* SSP4 - Amplifier */
		.name = "SSP4-Codec",
		.id = 4,
		.ignore_suspend = 1,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(ssp4_pin, ssp4_codec, platform),
	},
	{
		/* SSP5 - TestPin */
		.name = "SSP5-Codec",
		.id = 5,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(ssp5_pin, dummy, platform),
	},
};

#if !IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL)
static int bxt_add_dai_link(struct snd_soc_card *card,
			    struct snd_soc_dai_link *link)
{
	//link->platform_name = "0000:00:0e.0";
	link->nonatomic = 1;
	return 0;
}
#endif

/* broxton audio machine driver for TDF8532 */
static struct snd_soc_card broxton_tdf8532 = {
	.name = "broxton_tdf8532",
	.dai_link = broxton_tdf8532_dais,
	.num_links = ARRAY_SIZE(broxton_tdf8532_dais),
	.controls = broxton_tdf8532_controls,
	.num_controls = ARRAY_SIZE(broxton_tdf8532_controls),
	.dapm_widgets = broxton_tdf8532_widgets,
	.num_dapm_widgets = ARRAY_SIZE(broxton_tdf8532_widgets),
	.dapm_routes = broxton_tdf8532_map,
	.num_dapm_routes = ARRAY_SIZE(broxton_tdf8532_map),
	.fully_routed = true,
#if !IS_ENABLED(CONFIG_SND_SOC_SOF_INTEL)
	.add_dai_link = bxt_add_dai_link,
#endif
};

static int broxton_tdf8532_audio_probe(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "%s registering %s\n", __func__, pdev->name);
	broxton_tdf8532.dev = &pdev->dev;
	return snd_soc_register_card(&broxton_tdf8532);
}

static int broxton_tdf8532_audio_remove(struct platform_device *pdev)
{
	snd_soc_unregister_card(&broxton_tdf8532);
	return 0;
}

static struct platform_driver broxton_tdf8532_audio = {
	.probe = broxton_tdf8532_audio_probe,
	.remove = broxton_tdf8532_audio_remove,
	.driver = {
		.name = "bxt_tdf8532",
		.pm = &snd_soc_pm_ops,
	},
};

module_platform_driver(broxton_tdf8532_audio)

/* Module information */
MODULE_DESCRIPTION("Intel SST Audio for Broxton GP MRB");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:gpmrb_machine");
MODULE_ALIAS("platform:bxt_tdf8532");
