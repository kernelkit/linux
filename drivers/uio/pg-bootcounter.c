/*
 * Copyright 2024 Hitachi Energy.
 *
 * Driver for pg boot counter access
 *
 * The driver encapsulates access to the bootcounter via sysfs
 *
 *    reset: Boot counter reset for application: By writing any value to this write-only file, 
 *           the application acknowledges successful start. 
 *           Executes "if bootcounter <= MaxBoot then bootcounter := 0"
 *    zero:  Boot counter reset for swupdate: By writing any value to this write-only file, 
 *           the swupdate acknowledges successful sw upgrade. Executes "bootcounter := 0"
 *    value: This read-only file returns the current value of the boot counter. 
 *           This can be used by diagnostics.
 *    max:   This read-only file returns MaxBoot.
 * 
 *    The files appear under /sys/devices/platform/<address@pg-bootcounter>/<filename>
 * 
 * Module is configured with dt attributes maxboot and base
 * - maxboot: the maximum number of boot attempts. default is 3.
 * - base: address of the boot counter info structure in ram.
 *
 * IMPORTANT:
 * The module shares the constant "MAGICWORD" and the type "t_BootInfo" with U-Boot.
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

#define DRIVER_NAME "pg_bootcounter"
// Magic word to detect integrity of the BootInfo structure.
// If the magic word in ram is not equal to MAGICWORD, this driver exits with -EINVAL.
// Make sure U-Boot uses the same magic word.
#define MAGICWORD 0xCAFED00D 

// BootInfo structure. Make sure U-Boot uses the same structure
typedef struct  {
	u32 boot_counter; 
	u32 magicword;
} t_BootInfo;

struct pg_bootcounter_data {
    struct uio_info info;
    uint base;
    uint maxboot;
    void __iomem * bootinfo_addr; // mapped address of BootInfo structure 
};

// Setter function for the sysfs file "reset"
static ssize_t reset_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct pg_bootcounter_data *data = dev_get_drvdata(dev);
 
    uint bountercounter_value = readl_relaxed(data->bootinfo_addr + offsetof(t_BootInfo, boot_counter));

    if (bountercounter_value <= data->maxboot) {
        writel_relaxed(0, data->bootinfo_addr + offsetof(t_BootInfo, boot_counter));
    }
    return count;
}

static DEVICE_ATTR_WO(reset);

// Setter function for the sysfs file "zero"
static ssize_t zero_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
    struct pg_bootcounter_data *data = dev_get_drvdata(dev);

    writel_relaxed(0, data->bootinfo_addr + offsetof(t_BootInfo, boot_counter));

    return count;
}

static DEVICE_ATTR_WO(zero);

// Getter function for the sysfs file "value"
static ssize_t value_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct pg_bootcounter_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", readl_relaxed(data->bootinfo_addr + offsetof(t_BootInfo, boot_counter)));
}
static DEVICE_ATTR_RO(value);

// Getter function for the sysfs file "max"
static ssize_t max_show(struct device *dev, struct device_attribute *attr, char *buf)
{
    struct pg_bootcounter_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", data->maxboot);
}
static DEVICE_ATTR_RO(max);

// Probe function
static int pg_bootcounter_probe(struct platform_device *pdev)
{
    struct pg_bootcounter_data *data;
    struct resource *res;
    int ret;

    data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
    if (!data)
        return -ENOMEM;

	ret = device_property_read_u32(&pdev->dev, "maxboot", &data->maxboot);
	if (ret) {
		data->maxboot = 3;
	}

    ret = device_property_read_u32(&pdev->dev, "base", &data->base);
	if (ret) {
        return -EINVAL; 
	}

    data->info.name = DRIVER_NAME;
    data->info.version = "1.0";
    data->info.irq = UIO_IRQ_NONE;
    data->info.mem[0].addr = data->base;
    data->info.mem[0].size = sizeof(t_BootInfo);
    data->info.mem[0].memtype = UIO_MEM_PHYS;

    // map the 32-bit bootcounter for access in getter/setter functions
    data->bootinfo_addr = devm_ioremap(&pdev->dev, data->info.mem[0].addr, sizeof(t_BootInfo));

   	if (IS_ERR(data->bootinfo_addr)) {
        dev_err(&pdev->dev, "Could not ioremap\n");
    	return PTR_ERR(data->bootinfo_addr);
    }

    if (MAGICWORD != readl_relaxed(data->bootinfo_addr + offsetof(t_BootInfo, magicword))) {
        dev_err(&pdev->dev, "Bootcounter magic word invalid\n");
        return -EINVAL;
    } 

    // check if maxboot is valid
    if (data->maxboot == 0) {
        dev_err(&pdev->dev, "bootCounter maxboot is 0\n");
        return -EINVAL;
    } 

    ret = uio_register_device(&pdev->dev, &data->info);
    if (ret){
        dev_err(&pdev->dev, "Could not register device\n");
        return ret;
    }

    // remember driver data for later access in getter/ setter functions)
    dev_set_drvdata(&pdev->dev, data);

   // Create the sysfs file entries
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

    ret = device_create_file(&pdev->dev, &dev_attr_zero);
    if (ret) {
        dev_err(&pdev->dev,"Failed to create sysfs file for attribute zero\n");
        device_remove_file(&pdev->dev, &dev_attr_value);
        device_remove_file(&pdev->dev, &dev_attr_reset);
        uio_unregister_device(&data->info);
        return ret;
    } 

    ret = device_create_file(&pdev->dev, &dev_attr_max);
    if (ret) {
        dev_err(&pdev->dev,"Failed to create sysfs file for attribute max\n");
        device_remove_file(&pdev->dev, &dev_attr_value);
        device_remove_file(&pdev->dev, &dev_attr_reset);
        device_remove_file(&pdev->dev, &dev_attr_zero);
        uio_unregister_device(&data->info);
        return ret;
    }

    return 0;
}

static int pg_bootcounter_remove(struct platform_device *pdev)
{
    struct uio_info *info = platform_get_drvdata(pdev);

    // Remove the sysfs files
    device_remove_file(&pdev->dev, &dev_attr_value);
    device_remove_file(&pdev->dev, &dev_attr_reset);
    device_remove_file(&pdev->dev, &dev_attr_zero);
    device_remove_file(&pdev->dev, &dev_attr_max);

    uio_unregister_device(info);

    return 0;
}

static const struct of_device_id pg_bootcounter_of_match[] = {
    { .compatible = "hitachi,pg-bootcounter" },
    {},
};
MODULE_DEVICE_TABLE(of, pg_bootcounter_of_match);

static struct platform_driver pg_bootcounter_driver = {
    .probe = pg_bootcounter_probe,
    .remove = pg_bootcounter_remove,
    .driver = {
        .name = DRIVER_NAME,
        .owner = THIS_MODULE,
        .of_match_table = of_match_ptr(pg_bootcounter_of_match),
    },
};

module_platform_driver(pg_bootcounter_driver);

MODULE_AUTHOR("Christian Leeb");
MODULE_DESCRIPTION("Driver for accessing Hitachi pg boot counter");
MODULE_LICENSE("GPL v2");
