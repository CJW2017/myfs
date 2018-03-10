#include <linux/quotaops.h>
#include <linux/sched.h>
#include <linux/backing-dev.h>
#include <linux/buffer_head.h>
#include <linux/random.h>
#include "myfs.h"



/*
 *本来是通过inode位图找一个新的inode
 *现在想法是线性分配不会删除（第一个节点放在1号……） 
 *找superblock的已分配的inode节点数 在下一个建立这个节点
 */
struct inode *myfs_new_inode(struct inode *dir, umode_t mode,
			     const struct qstr *qstr)
{
	struct super_block *sb;
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *bh2;
	int group, i;
	ino_t ino = 0;
	struct inode * inode;
	struct myfs_group_desc *gdp;
	struct myfs_super_block *es;
	struct myfs_inode_info *ei;
	struct myfs_sb_info *sbi;
	int err;

	sb = dir->i_sb;
	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);

	ei = EXT2_I(inode);
	sbi = EXT2_SB(sb);
	es = sbi->s_es;
	if (S_ISDIR(mode)) {
		if (test_opt(sb, OLDALLOC))
			group = find_group_dir(sb, dir);
		else
			group = find_group_orlov(sb, dir);
	} else 
		group = find_group_other(sb, dir);

	if (group == -1) {
		err = -ENOSPC;
		goto fail;
	}

	for (i = 0; i < sbi->s_groups_count; i++) {
		gdp = myfs_get_group_desc(sb, group, &bh2);
		brelse(bitmap_bh);
		bitmap_bh = read_inode_bitmap(sb, group);
		if (!bitmap_bh) {
			err = -EIO;
			goto fail;
		}
		ino = 0;

repeat_in_this_group:
		ino = myfs_find_next_zero_bit((unsigned long *)bitmap_bh->b_data,
					      EXT2_INODES_PER_GROUP(sb), ino);
		if (ino >= EXT2_INODES_PER_GROUP(sb)) {
			/*
			 * Rare race: find_group_xx() decided that there were
			 * free inodes in this group, but by the time we tried
			 * to allocate one, they're all gone.  This can also
			 * occur because the counters which find_group_orlov()
			 * uses are approximate.  So just go and search the
			 * next block group.
			 */
			if (++group == sbi->s_groups_count)
				group = 0;
			continue;
		}
		if (myfs_set_bit_atomic(sb_bgl_lock(sbi, group),
						ino, bitmap_bh->b_data)) {
			/* we lost this inode */
			if (++ino >= EXT2_INODES_PER_GROUP(sb)) {
				/* this group is exhausted, try next group */
				if (++group == sbi->s_groups_count)
					group = 0;
				continue;
			}
			/* try to find free inode in the same group */
			goto repeat_in_this_group;
		}
		goto got;
	}

	/*
	 * Scanned all blockgroups.
	 */
	err = -ENOSPC;
	goto fail;
got:
	mark_buffer_dirty(bitmap_bh);
	if (sb->s_flags & MS_SYNCHRONOUS)
		sync_dirty_buffer(bitmap_bh);
	brelse(bitmap_bh);

	ino += group * EXT2_INODES_PER_GROUP(sb) + 1;
	if (ino < EXT2_FIRST_INO(sb) || ino > le32_to_cpu(es->s_inodes_count)) {
		myfs_error (sb, "myfs_new_inode",
			    "reserved inode or inode > inodes count - "
			    "block_group = %d,inode=%lu", group,
			    (unsigned long) ino);
		err = -EIO;
		goto fail;
	}

	percpu_counter_add(&sbi->s_freeinodes_counter, -1);
	if (S_ISDIR(mode))
		percpu_counter_inc(&sbi->s_dirs_counter);

	spin_lock(sb_bgl_lock(sbi, group));
	le16_add_cpu(&gdp->bg_free_inodes_count, -1);
	if (S_ISDIR(mode)) {
		if (sbi->s_debts[group] < 255)
			sbi->s_debts[group]++;
		le16_add_cpu(&gdp->bg_used_dirs_count, 1);
	} else {
		if (sbi->s_debts[group])
			sbi->s_debts[group]--;
	}
	spin_unlock(sb_bgl_lock(sbi, group));

	mark_buffer_dirty(bh2);
	if (test_opt(sb, GRPID)) {
		inode->i_mode = mode;
		inode->i_uid = current_fsuid();
		inode->i_gid = dir->i_gid;
	} else
		inode_init_owner(inode, dir, mode);

	inode->i_ino = ino;
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME_SEC;
	memset(ei->i_data, 0, sizeof(ei->i_data));
	ei->i_flags =
		myfs_mask_flags(mode, EXT2_I(dir)->i_flags & EXT2_FL_INHERITED);
	ei->i_faddr = 0;
	ei->i_frag_no = 0;
	ei->i_frag_size = 0;
	ei->i_file_acl = 0;
	ei->i_dir_acl = 0;
	ei->i_dtime = 0;
	ei->i_block_alloc_info = NULL;
	ei->i_block_group = group;
	ei->i_dir_start_lookup = 0;
	ei->i_state = EXT2_STATE_NEW;
	myfs_set_inode_flags(inode);
	spin_lock(&sbi->s_next_gen_lock);
	inode->i_generation = sbi->s_next_generation++;
	spin_unlock(&sbi->s_next_gen_lock);
	if (insert_inode_locked(inode) < 0) {
		myfs_error(sb, "myfs_new_inode",
			   "inode number already in use - inode=%lu",
			   (unsigned long) ino);
		err = -EIO;
		goto fail;
	}

	dquot_initialize(inode);
	err = dquot_alloc_inode(inode);
	if (err)
		goto fail_drop;

	err = myfs_init_acl(inode, dir);
	if (err)
		goto fail_free_drop;

	err = myfs_init_security(inode, dir, qstr);
	if (err)
		goto fail_free_drop;

	mark_inode_dirty(inode);
	myfs_debug("allocating inode %lu\n", inode->i_ino);
	myfs_preread_inode(inode);
	return inode;

fail_free_drop:
	dquot_free_inode(inode);

fail_drop:
	dquot_drop(inode);
	inode->i_flags |= S_NOQUOTA;
	clear_nlink(inode);
	unlock_new_inode(inode);
	iput(inode);
	return ERR_PTR(err);

fail:
	make_bad_inode(inode);
	iput(inode);
	return ERR_PTR(err);
}



/*空闲inode数量*/
/*
unsigned long myfs_count_free_inodes (struct super_block * sb)
{
//	struct ext2_group_desc *desc;
	unsigned long desc_count = 0;
	int i;	

#ifdef EXT2FS_DEBUG
	struct ext2_super_block *es;
	unsigned long bitmap_count = 0;
	struct buffer_head *bitmap_bh = NULL;

	es = EXT2_SB(sb)->s_es;
	for (i = 0; i < EXT2_SB(sb)->s_groups_count; i++) {
		unsigned x;

		desc = ext2_get_group_desc (sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_inodes_count);
		brelse(bitmap_bh);
		bitmap_bh = read_inode_bitmap(sb, i);
		if (!bitmap_bh)
			continue;

		x = ext2_count_free(bitmap_bh, EXT2_INODES_PER_GROUP(sb) / 8);
		printk("group %d: stored = %d, counted = %u\n",
			i, le16_to_cpu(desc->bg_free_inodes_count), x);
		bitmap_count += x;
	}
	brelse(bitmap_bh);
	printk("ext2_count_free_inodes: stored = %lu, computed = %lu, %lu\n",
		(unsigned long)
		percpu_counter_read(&EXT2_SB(sb)->s_freeinodes_counter),
		desc_count, bitmap_count);
	return desc_count;
#else
	for (i = 0; i < EXT2_SB(sb)->s_groups_count; i++) {
		desc = ext2_get_group_desc (sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_inodes_count);
	}
	return desc_count;
#endif
}
*/