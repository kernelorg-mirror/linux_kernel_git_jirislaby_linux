#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/raid/pq.h>

#include "ctree.h"
#include "disk-io.h"
#include "volumes.h"
#include "btrfs_inode.h"

#include "../../common.c"

struct backing_dev_info default_backing_dev_info;
const struct file_operations simple_dir_operations;
struct raid6_calls raid6_call;
const struct file_operations pipefifo_fops;
const struct file_operations def_chr_fops;
const struct file_operations def_blk_fops;
const struct file_operations bad_sock_fops;

struct backing_dev_info noop_backing_dev_info = {
	.name           = "noop",
	.capabilities   = BDI_CAP_NO_ACCT_AND_WRITEBACK,
};
int sysctl_vfs_cache_pressure = 100;

void (*raid6_2data_recov)(int, size_t, int, int, void **);
void (*raid6_datap_recov)(int, size_t, int, void **);

struct buffer_head *
__bread_gfp(struct block_device *bdev, sector_t block,
		                   unsigned size, gfp_t gfp)
{
	static struct buffer_head *blocks[100];
	struct buffer_head *bh;

	if (blocks[block]) {
		klee_warning("block returned");
		return blocks[block];
	}
	klee_warning("block alloc");

	bh = malloc(sizeof(*bh));
	if (bh) {
		char buf[30];
		bh->b_blocknr = block;
		bh->b_size = size;
		bh->b_bdev = bdev;
		bh->b_data = malloc(size);
		sprintf(buf, "bdata%lu", block);
		klee_make_symbolic(bh->b_data, size, buf);
		blocks[block] = bh;
	}

	return bh;
}

void __brelse(struct buffer_head * buf)
{
	if (atomic_read(&buf->b_count)) {
		put_bh(buf);
		return;
	}
}

void get_filesystem(struct file_system_type *fs)
{
}

int register_shrinker(struct shrinker *shrinker)
{
	return 0;
}

/**************************************/

int init_srcu_struct(struct srcu_struct *sp)
{
	return 0;
}

void call_rcu_sched(struct rcu_head *head, void (*func)(struct rcu_head *rcu))
{
}

void wait_rcu_gp(call_rcu_func_t crf)
{
}

int bdi_setup_and_register(struct backing_dev_info *bdi, char *name)
{
	return 0;
}

void invalidate_bdev(struct block_device *bdev)
{
}

void inode_wait_for_writeback(struct inode *inode)
{
}

void truncate_inode_pages_final(struct address_space *mapping)
{
}

void mark_page_accessed(struct page *page)
{
}

struct bio_set *bioset_create(unsigned int pool_size, unsigned int front_pad)
{
	return malloc(1);
}

struct workqueue_struct *__alloc_workqueue_key(const char *fmt,
					       unsigned int flags,
					       int max_active,
					       struct lock_class_key *key,
					       const char *lock_name, ...)
{
	return malloc(1);
}

void destroy_workqueue(struct workqueue_struct *wq)
{
	free(wq);
}

struct page *pagecache_get_page(struct address_space *mapping, pgoff_t offset,
	int fgp_flags, gfp_t gfp_mask)
{
	struct page *pg = malloc(sizeof(*pg));
	void *pg_data = malloc(4096);
	memset(pg, 0, sizeof(*pg));
	pg->virtual = pg_data;
	return pg;
}

void unlock_page(struct page *page)
{
	clear_bit_unlock(PG_locked, &page->flags);
}

struct block_device *blkdev_get_by_path(const char *path, fmode_t mode,
					void *holder)
{
	static struct inode bd_inode;
	static struct request_queue queue;
	static struct gendisk bd_disk = {
		.queue = &queue,
	};
	static struct block_device bdev = {
		.bd_inode = &bd_inode,
		.bd_disk = &bd_disk,
	};
	klee_make_symbolic(&bd_inode, sizeof(bd_inode), "bd_inode");
	bd_inode.i_bdev = &bdev;
	bd_inode.i_mapping = (struct address_space *)&bd_inode;

	return &bdev;
}

void blkdev_put(struct block_device *bdev, fmode_t mode)
{
}

int bdev_read_only(struct block_device *bdev) 
{
	return klee_int(__func__);
}

const char *bdevname(struct block_device *bdev, char *buf)
{
	strcpy(buf, "sdb");
	return buf;
}


struct page *read_cache_page_gfp(struct address_space *mapping,
				pgoff_t index,
				gfp_t gfp)
{
	struct page *page = malloc(sizeof(*page));
	struct inode *inode = (struct inode *)mapping;

	page->virtual = __bread_gfp(inode->i_bdev, index, 4096, 0)->b_data;

	return page;
}

void put_page(struct page *page)
{
	free(page);
}

int filemap_write_and_wait(struct address_space *mapping)
{
	/* TODO */
	klee_warning("TODO filemap_write_and_wait");
	return 0;
}

int filemap_fdatawrite_range(struct address_space *mapping, loff_t start,
				loff_t end)
{
	/* TODO */
//	klee_warning("TODO filemap_fdatawrite_range");
	return 0;
}

int filemap_fdatawait_range(struct address_space *mapping, loff_t start_byte,
			    loff_t end_byte)
{
	/* TODO */
//	klee_warning("TODO filemap_fdatawait_range");
	return 0;
}

void wake_up_bit(void *word, int bit)
{
}

int set_blocksize(struct block_device *bdev, int size)
{
	/* Size must be a power of two, and between 512 and PAGE_SIZE */
	if (size > PAGE_SIZE || size < 512 || !is_power_of_2(size))
		return -EINVAL;

	/* Size cannot be smaller than the size supported by the device */
//	if (size < bdev_logical_block_size(bdev))
//		return -EINVAL;

	/* Don't change the size if it is same as current */
	if (bdev->bd_block_size != size) {
//		sync_blockdev(bdev);
		bdev->bd_block_size = size;
		bdev->bd_inode->i_blkbits = blksize_bits(size);
//		kill_bdev(bdev);
	}
	return 0;
}

/**************************************/

extern void btrfs_init_compress(void);
extern int btrfs_init_cachep(void);
extern int extent_io_init(void);
extern int extent_map_init(void);
extern int ordered_data_init(void);
extern int btrfs_delayed_inode_init(void);
extern int btrfs_auto_defrag_init(void);
extern int btrfs_delayed_ref_init(void);
extern int btrfs_prelim_ref_init(void);
extern int btrfs_end_io_wq_init(void);

extern int device_list_add(const char *path,
		struct btrfs_super_block *disk_super, u64 devid,
		struct btrfs_fs_devices **fs_devices_ret);

extern int btrfs_fill_super(struct super_block *sb,
			    struct btrfs_fs_devices *fs_devices,
			    void *data, int silent);


#if 0
static char super_copy[4096];
static char super_for_commit[4096];
static struct btrfs_fs_info bfi = {
	.super_copy = (void *)super_copy,
	.super_for_commit = (void *)super_for_commit,
};
static struct super_block sb = {
	.s_fs_info = &bfi,
};
static struct btrfs_super_block *disk_super;
#endif
extern struct file_system_type btrfs_fs_type;

//static char data[4096];

int main(void)
{
	unsigned int a;
	int flags;
#if 0
	struct btrfs_fs_devices *fs_dev;
	u64 devid;
	u64 total_devices;
	int ret;

	typeof(sb.s_flags) flags;

	klee_make_symbolic(&flags, sizeof(flags), "sb.s_flags");
	sb.s_flags = flags;
	INIT_LIST_HEAD(&sb.s_inodes);
#endif
//	klee_make_symbolic(data, sizeof(data), "data");
	klee_make_symbolic(&flags, sizeof(flags), "flags");

	current_task = malloc(sizeof(struct task_struct));
	memset(current_task, 0, sizeof(struct task_struct));
	current_task->pid = 1; /* let it be init :) */

	for (a = 0; a < SECTIONS_PER_ROOT; a++) {
		mem_section[a] = malloc(sizeof(struct mem_section));
		memset(mem_section[a], 0, sizeof(struct mem_section));
	}

	idr_init_cache();
	radix_tree_init();
	inode_init();

	btrfs_init_compress();
	btrfs_init_cachep();
	extent_io_init();
	extent_map_init();
	ordered_data_init();
	btrfs_delayed_inode_init();
	btrfs_auto_defrag_init();
	btrfs_delayed_ref_init();
	btrfs_prelim_ref_init();
	btrfs_end_io_wq_init();
#if 0
	disk_super = __bread_gfp(&bdev, 16, 4096, 0)->b_data;

	devid = btrfs_stack_device_id(&disk_super->dev_item);
	total_devices = btrfs_super_num_devices(disk_super);

	ret = device_list_add("/dev/sdb", disk_super, devid, &fs_dev);
	if (!ret)
		fs_dev->total_devices = total_devices;

	fs_dev->latest_bdev = &bdev;
	bfi.fs_devices = fs_dev;

	btrfs_fill_super(&sb, fs_dev, NULL, 0);
#endif
	btrfs_fs_type.mount(&btrfs_fs_type, flags, "/dev/sdb", NULL); // data

	return 0;
}
