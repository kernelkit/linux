// SPDX-License-Identifier: GPL-2.0
#include <linux/libfdt.h>
#include <linux/module.h>
#include <linux/nvmem-consumer.h>
#include <linux/nvmem-provider.h>
#include <linux/of.h>
#include <linux/platform_device.h>

struct nvmem_fdt {
	char *name;
	struct nvmem_config cfg;
	struct nvmem_device *upper;
	struct nvmem_device *lower;

	void *fdt;
};

int nvmem_fdt_reg_read(void *priv, unsigned int offset, void *val, size_t bytes)
{
	struct nvmem_fdt *nvfdt = priv;
	const void *data;
	int i;

	for (i = 0; i < nvfdt->cfg.ncells; i++) {
		if ((nvfdt->cfg.cells[i].offset == offset) &&
		    (nvfdt->cfg.cells[i].bytes >= bytes)) {
			data = fdt_getprop_by_offset(nvfdt->fdt, offset, NULL, NULL);
			memcpy(val, data, bytes);
			return 0;
		}
	}

	return -ENOSYS;
}

static int nvmem_fdt_probe_cells(struct platform_device *pdev)

{
	struct nvmem_fdt *nvfdt = platform_get_drvdata(pdev);
	struct nvmem_cell_info *info;
	int offset, len;

	fdt_for_each_property_offset(offset, nvfdt->fdt, 0)
		nvfdt->cfg.ncells++;

	info = devm_kmalloc_array(&pdev->dev, nvfdt->cfg.ncells, sizeof(*info),
				  GFP_KERNEL | __GFP_ZERO);
	if (!info)
		return -ENOMEM;

	nvfdt->cfg.cells = info;

	fdt_for_each_property_offset(offset, nvfdt->fdt, 0) {
		fdt_getprop_by_offset(nvfdt->fdt, offset, &info->name, &len);
		info->offset = offset;
		info->bytes = len;

		if (pdev->dev.of_node)
			info->np = of_find_node_by_name(pdev->dev.of_node, info->name);

		info++;
	}

	return 0;
}

static int nvmem_fdt_probe(struct platform_device *pdev)
{
	struct nvmem_fdt *nvfdt;
	struct fdt_header hdr;
	int bytes, err;

	nvfdt = devm_kzalloc(&pdev->dev, sizeof(*nvfdt), GFP_KERNEL);
	if (!nvfdt)
		return -ENOMEM;

	nvfdt->lower = devm_nvmem_device_get(&pdev->dev, NULL);
	if (IS_ERR(nvfdt->lower))
		return PTR_ERR(nvfdt->lower);

	bytes = nvmem_device_read(nvfdt->lower, 0, sizeof(hdr), &hdr);
	if (bytes < 0)
		return bytes;

	if (fdt_check_header(&hdr))
		return -EINVAL;

	nvfdt->fdt = devm_kmalloc(&pdev->dev, fdt_totalsize(&hdr), GFP_KERNEL);
	if (!nvfdt->fdt)
		return -ENOMEM;

	bytes = nvmem_device_read(nvfdt->lower, 0, fdt_totalsize(&hdr), nvfdt->fdt);
	if (bytes < 0 )
		return bytes;

	/* if (fdt_check_full(nvfdt->fdt, fdt_totalsize(&hdr))) */
	/* 	return -EINVAL; */

	platform_set_drvdata(pdev, nvfdt);

	nvfdt->cfg = (struct nvmem_config){
		.dev = &pdev->dev,
		.id = NVMEM_DEVID_NONE,
		.owner = THIS_MODULE,
		.read_only = true,
		.reg_read = nvmem_fdt_reg_read,
		.priv = nvfdt,
	};

	nvfdt->cfg.name = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%s-fdt",
					 nvmem_dev_name(nvfdt->lower));

	err = nvmem_fdt_probe_cells(pdev);
	if (err)
		return err;

	nvfdt->upper = devm_nvmem_register(&pdev->dev, &nvfdt->cfg);
	if (IS_ERR(nvfdt->upper))
		return PTR_ERR(nvfdt->upper);

	return 0;
}

static int nvmem_fdt_remove(struct platform_device *pdev)
{
	/* All resources are managed by devm */
	return 0;
}

static const struct of_device_id nvmem_fdt_of_match[] = {
	{ .compatible = "nvmem-fdt" },
	{ },
};
MODULE_DEVICE_TABLE(of, nvmem_fdt_of_match);

static struct platform_driver nvmem_fdt_driver = {
	.probe = nvmem_fdt_probe,
	.remove = nvmem_fdt_remove,
	.driver = {
		.name = "nvmem-fdt",
		.of_match_table = nvmem_fdt_of_match,
	},
};
module_platform_driver(nvmem_fdt_driver);

MODULE_ALIAS("platform:nvmem-fdt");
MODULE_AUTHOR("Tobias Waldekranz");
MODULE_LICENSE("GPL v2");
