/*inode.c
 *分配 读 写 销毁  inode
 */
#include <linux/time.h>
#include <linux/highuid.h>
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/writeback.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/fiemap.h>
#include <linux/namei.h>
#include <linux/aio.h>
#include "myfs.h"




//static struct inode *myfs_alloc_inode(struct super_block *sb)

//static void myfs_destroy_inode(struct inode *inode)

//static void myfs_write_inode(struct inode *inode)

//static void myfs_read_inode(struct super_block *sb), 

static struct myfs_inode *myfs_get_inode(struct super_block *sb, ino_t ino,
					struct buffer_head **p)
{
	struct buffer_head * bh;
    int block;
	unsigned long offset;

	*p = NULL;
	offset = (ino - 1) * MYFS_INODE_SIZE;  //inode_size= 32B
	if (ino<128)           //前128个在2号块
		block = 1;
	else
		block = 2;
		
	bh = sb_bread(sb, block);   //读取第1个或者第2个块

	*p = bh;
	
	return (struct myfs_inode *) (bh->b_data + offset);
}



/*根据inode号获取inode，并且填充inode_info、vfs_inode*/
struct inode *myfs_iget (struct super_block *sb, unsigned long ino )
{
	struct myfs_inode_info *ei;
	struct buffer_head * bh;
	struct myfs_inode *raw_inode;
	struct inode *inode;
	
	long ret = -EIO;
	int n;
	
	uid_t i_uid;
	gid_t i_gid;
	
	inode = iget_locked(sb,ino);//  /fs/inode.c   先读inode
	if (!inode) 				//没读到
		return ERR_PTR(-ENOMEM);
	if (!(inode->i_state & I_NEW))		//读到的是最新。。。
		return inode;
	//如果有更新  重新读
	ei = MYFS_I(inode);
	ei->i_block_alloc_info = NULL;
	
	/*获取硬盘上的inode*/
	raw_inode = myfs_get_inode(inode->i_sb, ino ,&bh);
	/*填充vfs_inode信息*/
	inode->i_mode = le16_to_cpu(raw_inode->i_mode);
	i_uid = (uid_t)le16_to_cpu(raw_inode->i_uid);
	i_gid = (gid_t)le16_to_cpu(raw_inode->i_gid);
	i_uid_write(inode, i_uid);
	i_gid_write(inode, i_gid);	
	
	set_nlink(inode, 0);
	inode->i_size = le32_to_cpu(raw_inode->i_size_low);
	if (S_ISREG(inode->i_mode))
		inode->i_size |= ((__u64)le32_to_cpu(raw_inode->i_size_high)) << 32;
	inode->i_atime.tv_sec = (signed)le32_to_cpu(raw_inode->i_atime);
	inode->i_ctime.tv_sec = (signed)le32_to_cpu(raw_inode->i_ctime);
	inode->i_mtime.tv_sec = (signed)le32_to_cpu(raw_inode->i_atime);
	inode->i_atime.tv_nsec = inode->i_mtime.tv_nsec = inode->i_ctime.tv_nsec = 0;

	inode->i_generation = 0;//le32_to_cpu(raw_inode->i_generation);//应该不重要 找一个默认值 暂时是0
		
	inode->i_blocks = le32_to_cpu(raw_inode->i_blocks);
	
	/*填充myfs_inode_info 结构体 信息*/
	ei->i_flags = 0;/* 文件标志  需要改一个默认值*/
	ei->i_faddr = 0;/**/
	ei->i_frag_no = 0;/**/
	ei->i_frag_size = 0;/**/
	ei->i_file_acl = 0;//le32_to_cpu(raw_inode->i_file_acl);//这个可能需要
	ei->i_dir_acl = 0;
	ei->i_dtime = 0;
	
	ei->i_state = 0;
	ei->i_block_group = 0;//改
	ei->i_dir_start_lookup = 0;

	ei->i_start_blk = raw_inode->i_start_blk;
	ei->i_end_blk = raw_inode->i_end_blk;
	ei->i_blocks = raw_inode->i_blocks;
	
	if (S_ISREG(inode->i_mode)) {
		inode->i_op 			= &myfs_file_inode_operations;
		inode->i_mapping->a_ops = &myfs_aops;
		inode->i_fop 			= &myfs_file_operations;
		
	} else if (S_ISDIR(inode->i_mode)) {
		inode->i_op 			= &myfs_dir_inode_operations;
		inode->i_fop 			= &myfs_dir_operations;
		inode->i_mapping->a_ops = &myfs_aops;
	} 
	
	brelse (bh);
	
	
	return inode;
	
	
}

/**/
/*
void myfs_truncate(struct inode *inode){
	if(!(S_ISREG(inode->i_mode) || S_ISDIR(inode->i_mode)))
		return;
	struct myfs_inode_info *ei = MYFS_I(inode);
	block_truncate_page(inode->i_mapping,inode->i_size,myfs_get_block);


}
*/

/**/
int myfs_get_block (struct inode *inode, sector_t iblock, 
                    struct buffer_head *bh, int create)
{
		int err = -EIO;

		struct myfs_inode_info *ei = MYFS_I(inode);

		if (iblock>(ei->i_blocks  )){
			printk("MYFS : myfs_get_block error");
			return err;
		}

		myfs_fsblk_t block = ei->i_start_blk + iblock;
		if (block <= ei->i_end_blk){
			map_bh(bh, inode->i_sb, le32_to_cpu(block));
			return 0;
		}
		else if(!create){
			brelse(bh);
			return err;
		}
	return err;
}


/*aops*/
static int myfs_writepage(struct page *page, struct writeback_control *wbc)
{
        return block_write_full_page(page, myfs_get_block, wbc);
}

static int myfs_readpage(struct file *file, struct page *page)
{
        return mpage_readpage(page, myfs_get_block);
}

static int myfs_readpages(struct file *file, struct address_space *mapping,
                struct list_head *pages, unsigned nr_pages)
{
        return mpage_readpages(mapping, pages, nr_pages, myfs_get_block);
}

static int myfs_writepages(struct address_space *mapping ,struct writeback_control *wbc)
{
        return mpage_writepages(mapping, wbc, myfs_get_block);
}

/**/
static int __myfs_write_inode(struct inode *inode, int do_sync)
{
	struct myfs_inode_info *ei = MYFS_I(inode);
	struct super_block *sb = inode->i_sb;
	ino_t ino = inode->i_ino;
	uid_t uid = i_uid_read(inode);
	gid_t gid = i_gid_read(inode);
	struct buffer_head * bh;
	struct myfs_inode * raw_inode = myfs_get_inode(sb, ino, &bh);
	int n;
	int err = 0;

	if (IS_ERR(raw_inode))
 		return -EIO;

	/* For fields not not tracking in the in-memory inode,
	 * initialise them to zero for new inodes. */
	if (ei->i_state & MYFS_STATE_NEW)
		memset(raw_inode, 0, MYFS_SB(sb)->s_inode_size);

//	myfs_get_inode_flags(ei);  //没写

/*填充raw_inode */
	raw_inode->i_mode = cpu_to_le16(inode->i_mode);

	raw_inode->i_uid = 0;
	raw_inode->i_gid = 0;
	
//	raw_inode->i_links_count = cpu_to_le16(inode->i_nlink);
//	raw_inode->i_size = cpu_to_le32(inode->i_size);
	raw_inode->i_atime = cpu_to_le32(inode->i_atime.tv_sec);
	raw_inode->i_ctime = cpu_to_le32(inode->i_ctime.tv_sec);
//	raw_inode->i_mtime = cpu_to_le32(inode->i_mtime.tv_sec);
//	raw_inode->i_blocks = cpu_to_le32(inode->i_blocks);
//	raw_inode->i_dtime = cpu_to_le32(ei->i_dtime);
//	raw_inode->i_flags = cpu_to_le32(ei->i_flags);
//	raw_inode->i_faddr = cpu_to_le32(ei->i_faddr);
//	raw_inode->i_frag = ei->i_frag_no;
//	raw_inode->i_fsize = ei->i_frag_size;
//	raw_inode->i_file_acl = cpu_to_le32(ei->i_file_acl);
	
	/**/
	if (S_ISREG(inode->i_mode))
	{
		//raw_inode->i_size_high = cpu_to_le32(inode->i_size >> 32);
		if (inode->i_size > 0x7fffffffULL) {
				raw_inode->i_size_low = inode->i_size & (0xFFFFFFFF);
				raw_inode->i_size_high = inode->i_size >> 32;
		}
	}
	
	raw_inode->i_start_blk = ei->i_start_blk;
	raw_inode->i_end_blk = ei->i_end_blk;
	
	mark_buffer_dirty(bh);

	if (do_sync) {
		sync_dirty_buffer(bh);
		if (buffer_req(bh) && !buffer_uptodate(bh)) {
			printk ("IO error syncing myfs inode [%s:%08lx]\n",
				sb->s_id, (unsigned long) ino);
			err = -EIO;
		}
	}
	
	ei->i_state &= ~MYFS_STATE_NEW;
	
	brelse (bh);
	return err;
}

/*写inode函数*/
int myfs_write_inode(struct inode *inode, struct writeback_control *wbc)
{
	return __myfs_write_inode(inode, wbc->sync_mode == WB_SYNC_ALL);
}




/*inode销毁函数*/
static void myfs_destroy_inode(struct inode *inode){
	
	
	;
}

/*

int __myfs_write_begin(struct file *file,struct address_space *mapping,loff_t pos,unsigned len,unsigned flags,struct page *pagep,void **fsdata){
	return block_write_begin(file,mapping,pos,len,flags,pagep,fsdata,myfs_get_block);

}

static int myfs_write_begin(struct file *file,struct address_space *mapping,
loff_t pos,unsigned len,unsigned flags,struct page **pagep,void **fsdata){
	*pagep=NULL;
	return __myfs_write_begin(file,mapping,pos,len,flags,pagep,fsdata);
}
*/
static sector_t myfs_bmap(struct address_space *mapping,sector_t block){
	return generic_block_bmap(mapping,block,myfs_get_block);
}




const struct address_space_operations myfs_aops = {
	.readpage		= myfs_readpage,
	.readpages		= myfs_readpages,
	.writepage		= myfs_writepage,
    .writepages     = myfs_writepages,
//	.write_begin	= myfs_write_begin,
	.write_end		= generic_write_end,
	.bmap			= myfs_bmap,
//	.direct_IO		= ext2_direct_IO,
	.migratepage		= buffer_migrate_page,
	.is_partially_uptodate	= block_is_partially_uptodate,
	.error_remove_page	= generic_error_remove_page,
};





