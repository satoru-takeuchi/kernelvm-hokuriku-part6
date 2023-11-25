/*
 * Copyright (C) 2003 Sistina Software (UK) Limited.
 * Copyright (C) 2004, 2010-2011 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 */

#include <linux/device-mapper.h>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/bio.h>
#include <linux/slab.h>

#define DM_MSG_PREFIX "hello"

struct hello_c {
	struct dm_dev *dev;
	sector_t start;
};

static int hello_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	int r;
	struct hello_c *hc;
	struct dm_arg_set as;
	const char *devname;
	unsigned long long tmp;
	char _dontuse;

	as.argc = argc;
	as.argv = argv;

	if (argc != 2) {
		ti->error = "Invalid argument count";
		return -EINVAL;
	}

	hc = kzalloc(sizeof(*hc), GFP_KERNEL);
	if (!hc) {
		ti->error = "Cannot allocate context";
		return -ENOMEM;
	}

	devname = dm_shift_arg(&as);

	r = -EINVAL;
	if (sscanf(dm_shift_arg(&as), "%llu%c", &tmp, &_dontuse) != 1 || tmp != (sector_t)tmp) {
		ti->error = "Invalid device sector";
		goto err;
	}
	hc->start = tmp;

	r = dm_get_device(ti, devname, dm_table_get_mode(ti->table), &hc->dev);
	if (r) {
		ti->error = "Device lookup failed";
		goto err;
	}

	ti->num_flush_bios = 1;
	ti->num_discard_bios = 1;
	ti->private = hc;
	
	return 0;

err:
	kfree(hc);
	return r;
}

static void hello_dtr(struct dm_target *ti)
{
	struct hello_c *hc = ti->private;

	dm_put_device(ti, hc->dev);
	kfree(hc);
}

static void hello_map_bio(struct dm_target *ti, struct bio *bio)
{
	struct hello_c *hc = ti->private;

	bio_set_dev(bio, hc->dev->bdev);
	if (bio_sectors(bio))
		bio->bi_iter.bi_sector = hc->start + dm_target_offset(ti, bio->bi_iter.bi_sector);
}

static char hello[] = "Hello!\n";

static int hello_map(struct dm_target *ti, struct bio *bio)
{
	struct bvec_iter iter;
	struct bio_vec bvec;

	if (!bio_has_data(bio))
		return DM_MAPIO_REMAPPED;

	bio_for_each_segment(bvec, bio, iter) {
		char *segment;
		struct page *page = bio_iter_page(bio, iter);
		if (unlikely(page == ZERO_PAGE(0)))
			break;
		segment = bvec_kmap_local(&bvec);
		memcpy(segment, hello, sizeof(hello));
		kunmap_local(segment);
		break;
	}
	hello_map_bio(ti, bio);

	return DM_MAPIO_REMAPPED;
}

static int hello_prepare_ioctl(struct dm_target *ti, struct block_device **bdev)
{
	struct hello_c *hc = ti->private;

	*bdev = hc->dev->bdev;

	if (hc->start || ti->len != i_size_read((*bdev)->bd_inode) >> SECTOR_SHIFT)
		return 1;
	return 0;
}

static int hello_iterate_devices(struct dm_target *ti, iterate_devices_callout_fn fn, void *data)
{
	struct hello_c *hc = ti->private;

	return fn(ti, hc->dev, hc->start, ti->len, data);
}

static struct target_type hello_target = {
	.name   = "hello",
	.version = {0, 0, 1},
	.features = DM_TARGET_PASSES_CRYPTO,
	.module = THIS_MODULE,
	.ctr    = hello_ctr,
	.dtr    = hello_dtr,
	.map    = hello_map,
	.prepare_ioctl = hello_prepare_ioctl,
	.iterate_devices = hello_iterate_devices,
};

static int __init dm_hello_init(void)
{
	int r = dm_register_target(&hello_target);

	if (r < 0)
		DMERR("register failed %d", r);

	return r;
}

static void __exit dm_hello_exit(void)
{
	dm_unregister_target(&hello_target);
}

module_init(dm_hello_init);
module_exit(dm_hello_exit);

MODULE_DESCRIPTION(DM_NAME "hello target");
MODULE_AUTHOR("Joe Thornber <dm-devel@redhat.com>");
MODULE_LICENSE("GPL");
