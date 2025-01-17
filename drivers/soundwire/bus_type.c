// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2015-17 Intel Corporation.

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/pm_domain.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include "bus.h"

/**
 * sdw_get_device_id - find the matching SoundWire device id
 * @slave: SoundWire Slave Device
 * @drv: SoundWire Slave Driver
 *
 * The match is done by comparing the mfg_id and part_id from the
 * struct sdw_device_id.
 */
static const struct sdw_device_id *
sdw_get_device_id(struct sdw_slave *slave, struct sdw_driver *drv)
{
	const struct sdw_device_id *id = drv->id_table;

	while (id && id->mfg_id) {
		if (slave->id.mfg_id == id->mfg_id &&
		    slave->id.part_id == id->part_id)
			return id;
		id++;
	}

	return NULL;
}

static int sdw_bus_match(struct device *dev, struct device_driver *ddrv)
{
	struct sdw_slave *slave;
	struct sdw_driver *drv;
	struct sdw_master_device *md;
	struct sdw_master_driver *mdrv;
	int ret = 0;

	if (is_sdw_slave(dev)) {
		slave = dev_to_sdw_dev(dev);
		drv = drv_to_sdw_driver(ddrv);

		ret = !!sdw_get_device_id(slave, drv);
	} else {
		md = dev_to_sdw_master_device(dev);
		mdrv = drv_to_sdw_master_driver(ddrv);

		/*
		 * we don't have any hardware information so
		 * match with a hopefully unique string
		 */
		ret = !strncmp(md->master_name, mdrv->driver.name,
			       strlen(md->master_name));
	}
	return ret;
}

static int sdw_slave_modalias(const struct sdw_slave *slave, char *buf,
			      size_t size)
{
	/* modalias is sdw:m<mfg_id>p<part_id> */

	return snprintf(buf, size, "sdw:m%04Xp%04X\n",
			slave->id.mfg_id, slave->id.part_id);
}

static int sdw_master_modalias(const struct sdw_master_device *md,
			       char *buf, size_t size)
{
	/* modalias is sdw:<string> since we don't have any hardware info */

	return snprintf(buf, size, "sdw:%s\n",
			md->master_name);
}

static int sdw_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct sdw_slave *slave;
	struct sdw_master_device *md;
	char modalias[32];

	if (is_sdw_slave(dev)) {
		slave = dev_to_sdw_dev(dev);

		sdw_slave_modalias(slave, modalias, sizeof(modalias));

	} else {
		md = dev_to_sdw_master_device(dev);

		sdw_master_modalias(md, modalias, sizeof(modalias));
	}

	if (add_uevent_var(env, "MODALIAS=%s", modalias))
		return -ENOMEM;

	return 0;
}

struct bus_type sdw_bus_type = {
	.name = "soundwire",
	.match = sdw_bus_match,
	.uevent = sdw_uevent,
};
EXPORT_SYMBOL_GPL(sdw_bus_type);

static int sdw_drv_probe(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct sdw_driver *drv = drv_to_sdw_driver(dev->driver);
	const struct sdw_device_id *id;
	int ret;

	id = sdw_get_device_id(slave, drv);
	if (!id)
		return -ENODEV;

	slave->ops = drv->ops;

	/*
	 * attach to power domain but don't turn on (last arg)
	 */
	ret = dev_pm_domain_attach(dev, false);
	if (ret)
		return ret;

	ret = drv->probe(slave, id);
	if (ret) {
		dev_err(dev, "Probe of %s failed: %d\n", drv->name, ret);
		dev_pm_domain_detach(dev, false);
		return ret;
	}

	/* device is probed so let's read the properties now */
	if (slave->ops && slave->ops->read_prop)
		slave->ops->read_prop(slave);

	/*
	 * Check for valid clk_stop_timeout, use DisCo worst case value of
	 * 300ms
	 *
	 * TODO: check the timeouts and driver removal case
	 */
	if (slave->prop.clk_stop_timeout == 0)
		slave->prop.clk_stop_timeout = 300;

	slave->bus->clk_stop_timeout = max_t(u32, slave->bus->clk_stop_timeout,
					     slave->prop.clk_stop_timeout);

	slave->probed = true;
	complete(&slave->probe_complete);

	return 0;
}

static int sdw_drv_remove(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct sdw_driver *drv = drv_to_sdw_driver(dev->driver);
	int ret = 0;

	if (drv->remove)
		ret = drv->remove(slave);

	dev_pm_domain_detach(dev, false);

	return ret;
}

static void sdw_drv_shutdown(struct device *dev)
{
	struct sdw_slave *slave = dev_to_sdw_dev(dev);
	struct sdw_driver *drv = drv_to_sdw_driver(dev->driver);

	if (drv->shutdown)
		drv->shutdown(slave);
}

/**
 * __sdw_register_driver() - register a SoundWire Slave driver
 * @drv: driver to register
 * @owner: owning module/driver
 *
 * Return: zero on success, else a negative error code.
 */
int __sdw_register_driver(struct sdw_driver *drv, struct module *owner)
{
	drv->driver.bus = &sdw_bus_type;

	if (!drv->probe) {
		pr_err("driver %s didn't provide SDW probe routine\n",
		       drv->name);
		return -EINVAL;
	}

	drv->driver.owner = owner;
	drv->driver.probe = sdw_drv_probe;

	if (drv->remove)
		drv->driver.remove = sdw_drv_remove;

	if (drv->shutdown)
		drv->driver.shutdown = sdw_drv_shutdown;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__sdw_register_driver);

/**
 * sdw_unregister_driver() - unregisters the SoundWire Slave driver
 * @drv: driver to unregister
 */
void sdw_unregister_driver(struct sdw_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(sdw_unregister_driver);

static int sdw_master_drv_probe(struct device *dev)
{
	struct sdw_master_device *md = dev_to_sdw_master_device(dev);
	struct sdw_master_driver *mdrv = drv_to_sdw_master_driver(dev->driver);
	int ret;

	/*
	 * attach to power domain but don't turn on (last arg)
	 */
	ret = dev_pm_domain_attach(dev, false);
	if (ret)
		return ret;

	ret = mdrv->probe(md, md->pdata);
	if (ret) {
		dev_err(dev, "Probe of %s failed: %d\n",
			mdrv->driver.name, ret);
		dev_pm_domain_detach(dev, false);
		return ret;
	}

	return 0;
}

static int sdw_master_drv_remove(struct device *dev)
{
	struct sdw_master_device *md = dev_to_sdw_master_device(dev);
	struct sdw_master_driver *mdrv = drv_to_sdw_master_driver(dev->driver);
	int ret = 0;

	if (mdrv->remove)
		ret = mdrv->remove(md);

	dev_pm_domain_detach(dev, false);

	return ret;
}

static void sdw_master_drv_shutdown(struct device *dev)
{
	struct sdw_master_device *md = dev_to_sdw_master_device(dev);
	struct sdw_master_driver *mdrv = drv_to_sdw_master_driver(dev->driver);

	if (mdrv->shutdown)
		mdrv->shutdown(md);
}

/**
 * __sdw_register_master_driver() - register a SoundWire Master driver
 * @mdrv: 'Master driver' to register
 * @owner: owning module/driver
 *
 * Return: zero on success, else a negative error code.
 */
int __sdw_register_master_driver(struct sdw_master_driver *mdrv,
				 struct module *owner)
{
	mdrv->driver.bus = &sdw_bus_type;

	if (!mdrv->probe) {
		pr_err("driver %s didn't provide SDW probe routine\n",
		       mdrv->driver.name);
		return -EINVAL;
	}

	mdrv->driver.owner = owner;
	mdrv->driver.probe = sdw_master_drv_probe;

	if (mdrv->remove)
		mdrv->driver.remove = sdw_master_drv_remove;

	if (mdrv->shutdown)
		mdrv->driver.shutdown = sdw_master_drv_shutdown;

	return driver_register(&mdrv->driver);
}
EXPORT_SYMBOL_GPL(__sdw_register_master_driver);

/**
 * sdw_unregister_master_driver() - unregisters the SoundWire Master driver
 * @mdrv: driver to unregister
 */
void sdw_unregister_master_driver(struct sdw_master_driver *mdrv)
{
	driver_unregister(&mdrv->driver);
}
EXPORT_SYMBOL_GPL(sdw_unregister_master_driver);

static int __init sdw_bus_init(void)
{
	sdw_debugfs_init();
	return bus_register(&sdw_bus_type);
}

static void __exit sdw_bus_exit(void)
{
	sdw_debugfs_exit();
	bus_unregister(&sdw_bus_type);
}

postcore_initcall(sdw_bus_init);
module_exit(sdw_bus_exit);

MODULE_DESCRIPTION("SoundWire bus");
MODULE_LICENSE("GPL v2");
