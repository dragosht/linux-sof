// SPDX-License-Identifier: (GPL-2.0 OR BSD-3-Clause)
//
// This file is provided under a dual BSD/GPLv2 license.  When using or
// redistributing this file, you may do so under either license.
//
// Copyright(c) 2018 Intel Corporation. All rights reserved.
//
// Author: Liam Girdwood <liam.r.girdwood@linux.intel.com>
//

/* Mixer Controls */

#include <linux/pm_runtime.h>
#include "sof-priv.h"

/* return the widget type of the comp the kcontrol is attached to */
static int get_widget_type(struct snd_sof_dev *sdev,
			   struct snd_sof_control *scontrol)
{
	struct snd_sof_widget *swidget;

	list_for_each_entry(swidget, &sdev->widget_list, list) {
		if (swidget->comp_id == scontrol->comp_id)
			return swidget->id;
	}

	/* standalone kcontrol */
	return -EINVAL;
}

/* helper function to send pcm params ipc */
static int siggen_pcm_params(struct snd_sof_control *scontrol,
			     struct snd_sof_dev *sdev)
{
	struct sof_ipc_pcm_params_reply ipc_params_reply;
	struct sof_ipc_pcm_params pcm;
	int ret = 0;

	memset(&pcm, 0, sizeof(pcm));

	/* set IPC PCM parameters */
	pcm.hdr.size = sizeof(pcm);
	pcm.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | SOF_IPC_STREAM_PCM_PARAMS;
	pcm.comp_id = scontrol->comp_id;
	pcm.params.hdr.size = sizeof(pcm.params);
	pcm.params.channels = scontrol->num_channels;
	pcm.params.direction = SOF_IPC_STREAM_PLAYBACK;

	dev_err(sdev->dev, "setting siggen pcm params: size: %d, channels: %d\n",
		pcm.hdr.size, pcm.params.channels);

	/* send IPC to the DSP */
	ret = sof_ipc_tx_message(sdev->ipc, pcm.hdr.cmd, &pcm, sizeof(pcm),
				 &ipc_params_reply, sizeof(ipc_params_reply));
	if (ret < 0)
		dev_err(sdev->dev, "error: setting pcm params for siggen\n");

	return ret;
}

/* helper function to send stream trigger ipc for siggen pipeline */
static int siggen_trigger(struct snd_sof_control *scontrol,
			  struct snd_sof_dev *sdev, int cmd)
{
	struct sof_ipc_stream stream;
	struct sof_ipc_reply reply;
	int ret = 0;

	/* set IPC stream params */
	stream.hdr.size = sizeof(stream);
	stream.hdr.cmd = SOF_IPC_GLB_STREAM_MSG | cmd;
	stream.comp_id = scontrol->comp_id;

	/* send IPC to the DSP */
	ret = sof_ipc_tx_message(sdev->ipc, stream.hdr.cmd, &stream,
				 sizeof(stream), &reply, sizeof(reply));
	if (ret < 0)
		dev_err(sdev->dev, "error: failed to trigger stream\n");

	return ret;
}

/* set the active status for playback/capture for the virtual FE */
static int set_vfe_active_status(struct snd_sof_control *scontrol,
				 struct snd_soc_card *card, int status)
{
	struct snd_soc_pcm_runtime *rtd;

	list_for_each_entry(rtd, &card->rtd_list, list) {
		if (!strcmp(rtd->dai_link->name, scontrol->vfe_link_name)) {

			/* set playback status */
			if (rtd->dai_link->dpcm_playback) {
				rtd->cpu_dai->playback_active = status;
				rtd->codec_dai->playback_active = status;
			}

			/* set capture status */
			if (rtd->dai_link->dpcm_capture) {
				rtd->cpu_dai->capture_active = status;
				rtd->codec_dai->capture_active = status;
			}

			if (status) {
				/* increment the active count for cpu dai */
				rtd->cpu_dai->active++;
			} else {
				/* decrement the active count for cpu dai */
				rtd->cpu_dai->active--;
			}
		}
	}
	return 0;
}

/*
 * Helper function to send ipc's to trigger siggen pipeline
 * The siggen pipeline is enabled/disabled only if the
 * control values change from the old state.
 */
static int siggen_pipeline_trigger(struct snd_sof_control *scontrol,
				   struct snd_sof_dev *sdev,
				   struct snd_soc_card *card,
				   int new_state)
{
	int ret = 0;

	if (!new_state) {

		/* set runtime status as inactive for virtual FE */
		set_vfe_active_status(scontrol, card, 0);

		/* free pcm and reset pipeline */
		ret = siggen_trigger(scontrol, sdev,
				     SOF_IPC_STREAM_PCM_FREE);
	} else {

		/* set runtime status as active for virtual FE */
		set_vfe_active_status(scontrol, card, 1);

		/* enable BE DAI */
		snd_soc_dpcm_runtime_update(card);

		/* set pcm params */
		ret = siggen_pcm_params(scontrol, sdev);
		if (ret < 0)
			return ret;

		/* enable siggen */
		ret = siggen_trigger(scontrol, sdev,
				     SOF_IPC_STREAM_TRIG_START);
	}

	return ret;
}

static inline u32 mixer_to_ipc(unsigned int value, u32 *volume_map, int size)
{
	if (value >= size)
		return volume_map[size - 1];

	return volume_map[value];
}

static inline u32 ipc_to_mixer(u32 value, u32 *volume_map, int size)
{
	int i;

	for (i = 0; i < size; i++) {
		if (volume_map[i] >= value)
			return i;
	}

	return i - 1;
}

int snd_sof_volume_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *sm =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_sof_control *scontrol = sm->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	unsigned int i, channels = scontrol->num_channels;
	int err, ret;

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err_ratelimited(sdev->dev,
				    "error: volume get failed to resume %d\n",
				    ret);
		pm_runtime_put_noidle(sdev->dev);
		return ret;
	}

	/* get all the mixer data from DSP */
	snd_sof_ipc_set_get_comp_data(sdev->ipc, scontrol,
				      SOF_IPC_COMP_GET_VALUE,
				      SOF_CTRL_TYPE_VALUE_CHAN_GET,
				      SOF_CTRL_CMD_VOLUME,
				      false);

	/* read back each channel */
	for (i = 0; i < channels; i++)
		ucontrol->value.integer.value[i] =
			ipc_to_mixer(cdata->chanv[i].value,
				     scontrol->volume_table, sm->max + 1);

	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err_ratelimited(sdev->dev,
				    "error: volume get failed to idle %d\n",
				    err);
	return 0;
}

int snd_sof_volume_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *sm =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_sof_control *scontrol = sm->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	unsigned int i, channels = scontrol->num_channels;
	int ret, err;

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err_ratelimited(sdev->dev,
				    "error: volume put failed to resume %d\n",
				    ret);
		pm_runtime_put_noidle(sdev->dev);
		return ret;
	}

	/* update each channel */
	for (i = 0; i < channels; i++) {
		cdata->chanv[i].value =
			mixer_to_ipc(ucontrol->value.integer.value[i],
				     scontrol->volume_table, sm->max + 1);
		cdata->chanv[i].channel = i;
	}

	/* notify DSP of mixer updates */
	snd_sof_ipc_set_get_comp_data(sdev->ipc, scontrol,
				      SOF_IPC_COMP_SET_VALUE,
				      SOF_CTRL_TYPE_VALUE_CHAN_GET,
				      SOF_CTRL_CMD_VOLUME,
				      true);

	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err_ratelimited(sdev->dev,
				    "error: volume put failed to idle %d\n",
				    err);
	return 0;
}

int snd_sof_switch_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *sm =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_sof_control *scontrol = sm->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	unsigned int i, channels = scontrol->num_channels;
	int err, ret;

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err_ratelimited(sdev->dev,
				    "error: switch get failed to resume %d\n",
				    ret);
		pm_runtime_put_noidle(sdev->dev);
		return ret;
	}

	/* get all the mixer data from DSP */
	snd_sof_ipc_set_get_comp_data(sdev->ipc, scontrol,
				      SOF_IPC_COMP_GET_VALUE,
				      SOF_CTRL_TYPE_VALUE_CHAN_GET,
				      SOF_CTRL_CMD_SWITCH,
				      false);

	/* read back each channel */
	for (i = 0; i < channels; i++)
		ucontrol->value.integer.value[i] = cdata->chanv[i].value;

	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err_ratelimited(sdev->dev,
				    "error: switch get failed to idle %d\n",
				    err);
	return 0;
}

int snd_sof_switch_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *sm =
		(struct soc_mixer_control *)kcontrol->private_value;
	struct snd_sof_control *scontrol = sm->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_card *card = dapm->card;
	unsigned int i, channels = scontrol->num_channels;
	int ret = 0, new_state, old_state;
	int changed = 0;
	int err;

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err_ratelimited(sdev->dev,
				    "error: switch put failed to resume %d\n",
				    ret);
		pm_runtime_put_noidle(sdev->dev);
		return ret;
	}

	switch (get_widget_type(sdev, scontrol)) {
	case snd_soc_dapm_pga:

		/*
		 * if the kcontrol is used for processing as in the case of pga,
		 * values are channel-specific
		 */
		for (i = 0; i < channels; i++) {
			new_state = ucontrol->value.integer.value[i];
			old_state = cdata->chanv[i].value;
			if (new_state != old_state)
				changed = 1;
			cdata->chanv[i].value = new_state;
			cdata->chanv[i].channel = i;
		}
		/*
		 * notify DSP of switch state update
		 * if the control values are different
		 */
		if (changed)
			snd_sof_ipc_set_get_comp_data(sdev->ipc, scontrol,
						      SOF_IPC_COMP_SET_VALUE,
						      SOF_CTRL_TYPE_VALUE_CHAN_GET,
						      SOF_CTRL_CMD_SWITCH,
						      true);
		break;
	case snd_soc_dapm_siggen:

		/*
		 * A siggen kcontrol is used as an ON/OFF switch,
		 * so all channel values are assumed to be identical
		 */
		for (i = 0; i < channels; i++) {
			new_state = ucontrol->value.integer.value[0];
			old_state = cdata->chanv[0].value;
			if (new_state != old_state)
				changed = 1;
			cdata->chanv[i].value = new_state;
			cdata->chanv[i].channel = i;
		}

		if (changed) {

			/*
			 * notify DSP of switch state update
			 * if the control values are different
			 */
			snd_sof_ipc_set_get_comp_data(sdev->ipc, scontrol,
						      SOF_IPC_COMP_SET_VALUE,
						      SOF_CTRL_TYPE_VALUE_CHAN_GET,
						      SOF_CTRL_CMD_SWITCH,
						      true);

			/* trigger siggen pipeline */
			ret = siggen_pipeline_trigger(scontrol, sdev,
						      card, new_state);
			if (ret < 0) {
				dev_err(sdev->dev,
					"error: triggering siggen pipeline\n");
				changed = ret;
			}
		}
		break;
	default:

		/*
		 * if the kcontrol is for routing or a standalone control,
		 * all channel values are assumed to be identical
		 */
		for (i = 0; i < channels; i++) {
			new_state = ucontrol->value.integer.value[0];
			old_state = cdata->chanv[0].value;
			if (new_state != old_state)
				changed = 1;
			cdata->chanv[i].value = new_state;
			cdata->chanv[i].channel = i;
		}
		/*
		 * notify DSP of switch state update
		 * if the control values are different
		 */
		if (changed)
			snd_sof_ipc_set_get_comp_data(sdev->ipc, scontrol,
						      SOF_IPC_COMP_SET_VALUE,
						      SOF_CTRL_TYPE_VALUE_CHAN_GET,
						      SOF_CTRL_CMD_SWITCH,
						      true);
		break;
	}

	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err_ratelimited(sdev->dev,
				    "error: switch put failed to idle %d\n",
				    err);
	return 0;
}

int snd_sof_enum_get(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *se =
		(struct soc_enum *)kcontrol->private_value;
	struct snd_sof_control *scontrol = se->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	unsigned int i, channels = scontrol->num_channels;
	int err, ret;

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err_ratelimited(sdev->dev,
				    "error: enum get failed to resume %d\n",
				    ret);
		pm_runtime_put_noidle(sdev->dev);
		return ret;
	}

	/* get all the enum data from DSP */
	snd_sof_ipc_set_get_comp_data(sdev->ipc, scontrol,
				      SOF_IPC_COMP_GET_VALUE,
				      SOF_CTRL_TYPE_VALUE_CHAN_GET,
				      SOF_CTRL_CMD_ENUM,
				      false);

	/* read back each channel */
	for (i = 0; i < channels; i++)
		ucontrol->value.enumerated.item[i] = cdata->chanv[i].value;

	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err_ratelimited(sdev->dev,
				    "error: enum get failed to idle %d\n",
				    err);
	return 0;
}

int snd_sof_enum_put(struct snd_kcontrol *kcontrol,
		     struct snd_ctl_elem_value *ucontrol)
{
	struct soc_enum *se =
		(struct soc_enum *)kcontrol->private_value;
	struct snd_sof_control *scontrol = se->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	unsigned int i, channels = scontrol->num_channels;
	int ret, err;

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err_ratelimited(sdev->dev,
				    "error: enum put failed to resume %d\n",
				    ret);
		pm_runtime_put_noidle(sdev->dev);
		return ret;
	}

	/* update each channel */
	for (i = 0; i < channels; i++) {
		cdata->chanv[i].value = ucontrol->value.enumerated.item[i];
		cdata->chanv[i].channel = i;
	}

	/* notify DSP of enum updates */
	snd_sof_ipc_set_get_comp_data(sdev->ipc, scontrol,
				      SOF_IPC_COMP_SET_VALUE,
				      SOF_CTRL_TYPE_VALUE_CHAN_GET,
				      SOF_CTRL_CMD_ENUM,
				      true);

	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err_ratelimited(sdev->dev,
				    "error: enum put failed to idle %d\n",
				    err);
	return 0;
}

int snd_sof_bytes_get(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *)kcontrol->private_value;
	struct snd_sof_control *scontrol = be->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	struct sof_abi_hdr *data = cdata->data;
	size_t size;
	int ret, err;

	if (be->max > sizeof(ucontrol->value.bytes.data)) {
		dev_err_ratelimited(sdev->dev,
				    "error: data max %d exceeds ucontrol data array size\n",
				    be->max);
		return -EINVAL;
	}

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err_ratelimited(sdev->dev,
				    "error: bytes get failed to resume %d\n",
				    ret);
		pm_runtime_put_noidle(sdev->dev);
		return ret;
	}

	/* get all the binary data from DSP */
	snd_sof_ipc_set_get_comp_data(sdev->ipc, scontrol,
				      SOF_IPC_COMP_GET_DATA,
				      SOF_CTRL_TYPE_DATA_GET,
				      scontrol->cmd,
				      false);

	size = data->size + sizeof(*data);
	if (size > be->max) {
		dev_err_ratelimited(sdev->dev,
				    "error: DSP sent %zu bytes max is %d\n",
				    size, be->max);
		ret = -EINVAL;
		goto out;
	}

	/* copy back to kcontrol */
	memcpy(ucontrol->value.bytes.data, data, size);

out:
	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err_ratelimited(sdev->dev,
				    "error: bytes get failed to idle %d\n",
				    err);
	return ret;
}

int snd_sof_bytes_put(struct snd_kcontrol *kcontrol,
		      struct snd_ctl_elem_value *ucontrol)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *)kcontrol->private_value;
	struct snd_sof_control *scontrol = be->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	struct sof_abi_hdr *data = cdata->data;
	size_t size = data->size + sizeof(*data);
	int ret, err;

	if (be->max > sizeof(ucontrol->value.bytes.data)) {
		dev_err_ratelimited(sdev->dev,
				    "error: data max %d exceeds ucontrol data array size\n",
				    be->max);
		return -EINVAL;
	}

	if (size > be->max) {
		dev_err_ratelimited(sdev->dev,
				    "error: size too big %zu bytes max is %d\n",
				    size, be->max);
		return -EINVAL;
	}

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err_ratelimited(sdev->dev,
				    "error: bytes put failed to resume %d\n",
				    ret);
		pm_runtime_put_noidle(sdev->dev);
		return ret;
	}

	/* copy from kcontrol */
	memcpy(data, ucontrol->value.bytes.data, size);

	/* notify DSP of byte control updates */
	snd_sof_ipc_set_get_comp_data(sdev->ipc, scontrol,
				      SOF_IPC_COMP_SET_DATA,
				      SOF_CTRL_TYPE_DATA_SET,
				      scontrol->cmd,
				      true);

	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err_ratelimited(sdev->dev,
				    "error: bytes put failed to idle %d\n",
				    err);
	return ret;
}

int snd_sof_bytes_ext_put(struct snd_kcontrol *kcontrol,
			  const unsigned int __user *binary_data,
			  unsigned int size)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *)kcontrol->private_value;
	struct snd_sof_control *scontrol = be->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	struct snd_ctl_tlv header;
	const struct snd_ctl_tlv __user *tlvd =
		(const struct snd_ctl_tlv __user *)binary_data;
	int ret;
	int err;

	/*
	 * The beginning of bytes data contains a header from where
	 * the length (as bytes) is needed to know the correct copy
	 * length of data from tlvd->tlv.
	 */
	if (copy_from_user(&header, tlvd, sizeof(const struct snd_ctl_tlv)))
		return -EFAULT;

	/* be->max is coming from topology */
	if (header.length > be->max) {
		dev_err_ratelimited(sdev->dev, "error: Bytes data size %d exceeds max %d.\n",
				    header.length, be->max);
		return -EINVAL;
	}

	/* Check that header id matches the command */
	if (header.numid != scontrol->cmd) {
		dev_err_ratelimited(sdev->dev,
				    "error: incorrect numid %d\n",
				    header.numid);
		return -EINVAL;
	}

	if (copy_from_user(cdata->data, tlvd->tlv, header.length))
		return -EFAULT;

	if (cdata->data->magic != SOF_ABI_MAGIC) {
		dev_err_ratelimited(sdev->dev,
				    "error: Wrong ABI magic 0x%08x.\n",
				    cdata->data->magic);
		return -EINVAL;
	}

	if (SOF_ABI_VERSION_INCOMPATIBLE(SOF_ABI_VERSION, cdata->data->abi)) {
		dev_err_ratelimited(sdev->dev, "error: Incompatible ABI version 0x%08x.\n",
				    cdata->data->abi);
		return -EINVAL;
	}

	if (cdata->data->size + sizeof(const struct sof_abi_hdr) > be->max) {
		dev_err_ratelimited(sdev->dev, "error: Mismatch in ABI data size (truncated?).\n");
		return -EINVAL;
	}

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err_ratelimited(sdev->dev,
				    "error: bytes_ext put failed to resume %d\n",
				    ret);
		pm_runtime_put_noidle(sdev->dev);
		return ret;
	}

	/* notify DSP of byte control updates */
	snd_sof_ipc_set_get_comp_data(sdev->ipc, scontrol,
				      SOF_IPC_COMP_SET_DATA,
				      SOF_CTRL_TYPE_DATA_SET,
				      scontrol->cmd,
				      true);

	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err_ratelimited(sdev->dev,
				    "error: bytes_ext put failed to idle %d\n",
				    err);

	return ret;
}

int snd_sof_bytes_ext_get(struct snd_kcontrol *kcontrol,
			  unsigned int __user *binary_data,
			  unsigned int size)
{
	struct soc_bytes_ext *be =
		(struct soc_bytes_ext *)kcontrol->private_value;
	struct snd_sof_control *scontrol = be->dobj.private;
	struct snd_sof_dev *sdev = scontrol->sdev;
	struct sof_ipc_ctrl_data *cdata = scontrol->control_data;
	struct snd_ctl_tlv header;
	struct snd_ctl_tlv __user *tlvd =
		(struct snd_ctl_tlv __user *)binary_data;
	int data_size;
	int err;
	int ret;

	ret = pm_runtime_get_sync(sdev->dev);
	if (ret < 0) {
		dev_err_ratelimited(sdev->dev,
				    "error: bytes_ext get failed to resume %d\n",
				    ret);
		pm_runtime_put_noidle(sdev->dev);
		return ret;
	}

	/*
	 * Decrement the limit by ext bytes header size to
	 * ensure the user space buffer is not exceeded.
	 */
	size -= sizeof(const struct snd_ctl_tlv);

	/* set the ABI header values */
	cdata->data->magic = SOF_ABI_MAGIC;
	cdata->data->abi = SOF_ABI_VERSION;

	/* get all the component data from DSP */
	ret = snd_sof_ipc_set_get_comp_data(sdev->ipc, scontrol,
					    SOF_IPC_COMP_GET_DATA,
					    SOF_CTRL_TYPE_DATA_GET,
					    scontrol->cmd,
					    false);

	/* Prevent read of other kernel data or possibly corrupt response */
	data_size = cdata->data->size + sizeof(const struct sof_abi_hdr);

	/* check data size doesn't exceed max coming from topology */
	if (data_size > be->max) {
		dev_err_ratelimited(sdev->dev, "error: user data size %d exceeds max size %d.\n",
				    data_size, be->max);
		ret = -EINVAL;
		goto out;
	}

	header.numid = scontrol->cmd;
	header.length = data_size;
	if (copy_to_user(tlvd, &header, sizeof(const struct snd_ctl_tlv))) {
		ret = -EFAULT;
		goto out;
	}

	if (copy_to_user(tlvd->tlv, cdata->data, data_size))
		ret = -EFAULT;

out:
	pm_runtime_mark_last_busy(sdev->dev);
	err = pm_runtime_put_autosuspend(sdev->dev);
	if (err < 0)
		dev_err_ratelimited(sdev->dev,
				    "error: bytes_ext get failed to idle %d\n",
				    err);
	return ret;
}
