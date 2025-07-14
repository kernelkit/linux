/*
 * Copyright 2025 Hitachi Energy.
 *
 * Driver for atecc boot counter access
 *
 * The driver encapsulates access to the atecc boot counter via sysfs
 *
 *    reset: Counter reset for application: By writing any value to this write-only file, 
 *           the the counter is set to zero. 
 *    value: This read-only file returns the current value of the counter. 
 *    inc:   By writing any value to this write-only file, the counter is incremented.
 * 
 *    The files appear under /sys/devices/platform/<address@pg-atecccounter>/<filename>
 * 
 * Module is configured with dt attribute base
 * - base: address of the atecc boot counter info structure in ram.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/uio_driver.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/io.h>

#define DRIVER_NAME "pg_atecccounter"

// Magic word to detect integrity of the AteccBootInfo structure.
// If the magic word in ram is not equal to MAGICWORD, BootInfo is initialized .
#define MAGICWORD 0xCAFED00D 

// AteccBootInfo structure
typedef struct  {
	u32 atecc_bootcounter; 
	u32 magicword;
} t_AteccBootInfo;

struct pg_atecccounter_data {
    struct uio_info info;
    uint base;
    void __iomem * atecc_bootinfo_addr; // mapped address of AteccBootInfo structure 
};

// Setter function for the sysfs file "reset"
static ssize_t reset_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct pg_atecccounter_data *data = dev_get_drvdata(dev);
 
    writel_relaxed(0, data->atecc_bootinfo_addr + offsetof(t_AteccBootInfo, atecc_bootcounter));

    return count;
}

static DEVICE_ATTR_WO(reset);

// Setter function for the sysfs file "inc"
static ssize_t inc_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct pg_atecccounter_data *data = dev_get_drvdata(dev);

    uint atecc_bountercounter_value = readl_relaxed(data->atecc_bootinfo_addr + offsetof(t_AteccBootInfo, atecc_bootcounter));
 
    writel_relaxed(atecc_bountercounter_value + 1, data->atecc_bootinfo_addr + offsetof(t_AteccBootInfo, atecc_bootcounter));

    return count;
}

static DEVICE_ATTR_WO(inc);

// Getter function for the sysfs file "value"
static ssize_t value_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct pg_atecccounter_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", readl_relaxed(data->atecc_bootinfo_addr + offsetof(t_AteccBootInfo, atecc_bootcounter)));
}
static DEVICE_ATTR_RO(value);

// Probe function
static int pg_atecccounter_probe(struct platform_device *pdev)
{
    struct pg_atecccounter_data *data;
    int ret;

    data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

    ret = device_property_read_u32(&pdev->dev, "base", &data->base);
	if (ret) {
        return -EINVAL; 
	}

    data->info.name = DRIVER_NAME;
    data->info.version = "1.0";
    data->info.irq = UIO_IRQ_NONE;
    data->info.mem[0].addr = data->base;
    data->info.mem[0].size = sizeof(t_AteccBootInfo);
    data->info.mem[0].memtype = UIO_MEM_PHYS;

    // map the 32-bit atecccounter for access in getter/setter functions
    data->atecc_bootinfo_addr = devm_ioremap(&pdev->dev, data->info.mem[0].addr, sizeof(t_AteccBootInfo));

   	if (IS_ERR(data->atecc_bootinfo_addr)) {
        dev_err(&pdev->dev, "Could not ioremap\n");
    	return PTR_ERR(data->atecc_bootinfo_addr);
    }

    //initialize the bootinfo structure in ram if it is not initialized yet
    if (MAGICWORD != readl_relaxed(data->atecc_bootinfo_addr + offsetof(t_AteccBootInfo, magicword))) {
        writel_relaxed(MAGICWORD, data->atecc_bootinfo_addr + offsetof(t_AteccBootInfo, magicword));
        writel_relaxed(0, data->atecc_bootinfo_addr + offsetof(t_AteccBootInfo, atecc_bootcounter));
        dev_info(&pdev->dev, "ATECC boot information structure was initialized\n");
    } 

    ret = uio_register_device(&pdev->dev, &data->info);
    if (ret){
        dev_err(&pdev->dev, "Could not register device\n");
        return ret;
    }

    // remember driver data for later access in getter/ setter functions)
    dev_set_drvdata(&pdev->dev, data);

    // create the sysfs file entries
    ret = device_create_file(&pdev->dev, &dev_attr_value);
    if (ret) {
        dev_err(&pdev->dev,"Failed to create sysfs file for attribute value\n");
        uio_unregister_device(&data->info);
        return ret;
    }

    ret = device_create_file(&pdev->dev, &dev_attr_reset);
    if (ret) {
        dev_err(&pdev->dev,"Failed to create sysfs file for attribute reset\n");
        device_remove_file(&pdev->dev, &dev_attr_value);
        uio_unregister_device(&data->info);
        return ret;
    }

    ret = device_create_file(&pdev->dev, &dev_attr_inc);
    if (ret) {
        dev_err(&pdev->dev,"Failed to create sysfs file for attribute inc\n");
        device_remove_file(&pdev->dev, &dev_attr_value);
        device_remove_file(&pdev->dev, &dev_attr_reset);
        uio_unregister_device(&data->info);
        return ret;
    } 

    return 0;
}

static int pg_atecccounter_remove(struct platform_device *pdev)
{
    struct uio_info *info = platform_get_drvdata(pdev);

    // Remove the sysfs files
    device_remove_file(&pdev->dev, &dev_attr_value);
    device_remove_file(&pdev->dev, &dev_attr_reset);
    device_remove_file(&pdev->dev, &dev_attr_inc);

    uio_unregister_device(info);

    return 0;
}

static const struct of_device_id pg_atecccounter_of_match[] = {
    { .compatible = "hitachi,pg-atecccounter" },
    {},
};
MODULE_DEVICE_TABLE(of, pg_atecccounter_of_match);

static struct platform_driver pg_atecccounter_driver = {
    .probe = pg_atecccounter_probe,
    .remove = pg_atecccounter_remove,
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(pg_atecccounter_of_match),
    },
};

module_platform_driver(pg_atecccounter_driver);

MODULE_AUTHOR("Christian Leeb");
MODULE_DESCRIPTION("Driver for accessing Hitachi ATECC counter");
MODULE_LICENSE("GPL v2");
