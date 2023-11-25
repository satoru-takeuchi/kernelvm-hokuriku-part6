/*
 * Copyright (C) 2023 Satoru Takeuchi <satoru.takeuchi@gmail.com>
 * Copyright (C) 2001-2003 Sistina Software (UK) Limited.
 *
 * This file is released under the GPL.
 *
 * This module modifies the dm-linear target drivers/md/dm-linear.c in linux kernel.
 */

#include "dm.h"
#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>
#include <linux/device-mapper.h>

#define DM_MSG_PREFIX "simple"

/*
 * Linear: maps a linear range of a device.
 */
struct linear_c {
	struct dm_dev *dev;
	sector_t start;
};

/*
 * Construct a linear mapping: <dev_path> <offset>
 */
static int linear_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	struct linear_c *lc;
	unsigned long long tmp;
	char dummy;
	int ret;

	if (argc != 2) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	lc = kmalloc(sizeof(*lc), GFP_KERNEL);
	if (lc == NULL) {
		ti->error = "Cannot allocate linear context";
		return -ENOMEM;
	}

	ret = -EINVAL;
	if (sscanf(argv[1], "%llu%c", &tmp, &dummy) != 1 || tmp != (sector_t)tmp) {
		ti->error = "Invalid device sector";
		goto bad;
	}
	lc->start = tmp;

	ret = dm_get_device(ti, argv[0], dm_table_get_mode(ti->table), &lc->dev);
	if (ret) {
		ti->error = "Device lookup failed";
		goto bad;
	}

	ti->num_flush_bios = 1;
	ti->num_discard_bios = 1;
	ti->num_secure_erase_bios = 1;
	ti->num_write_same_bios = 1;
	ti->num_write_zeroes_bios = 1;
	ti->private = lc;
	return 0;

      bad:
	kfree(lc);
	return ret;
}

static void linear_dtr(struct dm_target *ti)
{
	struct linear_c *lc = (struct linear_c *) ti->private;

	dm_put_device(ti, lc->dev);
	kfree(lc);
}

static sector_t linear_map_sector(struct dm_target *ti, sector_t bi_sector)
{
	struct linear_c *lc = ti->private;

	return lc->start + dm_target_offset(ti, bi_sector);
}

static void linear_map_bio(struct dm_target *ti, struct bio *bio)
{
	struct linear_c *lc = ti->private;

	bio_set_dev(bio, lc->dev->bdev);
	if (bio_sectors(bio) || op_is_zone_mgmt(bio_op(bio)))
		bio->bi_iter.bi_sector =
			linear_map_sector(ti, bio->bi_iter.bi_sector);
}

static int linear_map(struct dm_target *ti, struct bio *bio)
{
	linear_map_bio(ti, bio);

	return DM_MAPIO_REMAPPED;
}

static void linear_status(struct dm_target *ti, status_type_t type,
			  unsigned status_flags, char *result, unsigned maxlen)
{
	struct linear_c *lc = (struct linear_c *) ti->private;
	size_t sz = 0;

	switch (type) {
	case STATUSTYPE_INFO:
		result[0] = '\0';
		break;

	case STATUSTYPE_TABLE:
		DMEMIT("%s %llu", lc->dev->name, (unsigned long long)lc->start);
		break;

	case STATUSTYPE_IMA:
		DMEMIT_TARGET_NAME_VERSION(ti->type);
		DMEMIT(",device_name=%s,start=%llu;", lc->dev->name,
		       (unsigned long long)lc->start);
		break;
	}
}

static int linear_prepare_ioctl(struct dm_target *ti, struct block_device **bdev)
{
	struct linear_c *lc = (struct linear_c *) ti->private;
	struct dm_dev *dev = lc->dev;

	*bdev = dev->bdev;

	/*
	 * Only pass ioctls through if the device sizes match exactly.
	 */
	if (lc->start ||
	    ti->len != i_size_read(dev->bdev->bd_inode) >> SECTOR_SHIFT)
		return 1;
	return 0;
}

static int linear_iterate_devices(struct dm_target *ti,
                                  iterate_devices_callout_fn fn, void *data)
{
        struct linear_c *lc = ti->private;

        return fn(ti, lc->dev, lc->start, ti->len, data);
}

static struct target_type simple_target = {
	.name   = "simple",
	.version = {0, 0, 1},
	.features = DM_TARGET_PASSES_INTEGRITY | DM_TARGET_NOWAIT |
		    DM_TARGET_PASSES_CRYPTO,
	.module = THIS_MODULE,
	.ctr    = linear_ctr,
	.dtr    = linear_dtr,
	.map    = linear_map,
	.status = linear_status,
	.prepare_ioctl = linear_prepare_ioctl,
	.iterate_devices = linear_iterate_devices,
};

int __init dm_simple_init(void)
{
	int r = dm_register_target(&simple_target);

	if (r < 0)
		DMERR("register failed %d", r);

	return r;
}

void dm_simple_exit(void)
{
	dm_unregister_target(&simple_target);
}

module_init(dm_simple_init);
module_exit(dm_simple_exit);

MODULE_DESCRIPTION(DM_NAME "simple dm target");
MODULE_AUTHOR("Satoru Takeuchi <satoru.takeuchi@gmail.com>");
MODULE_LICENSE("GPL");
