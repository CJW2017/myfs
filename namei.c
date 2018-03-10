/*done*/
/* 
 *namei.c *定义了inode_operations中的部分函数，实现了与VFS层的连接
 *主要包括 create lookup link mknod函数 应该够用
 */
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include "myfs.h"



/*
 *在dir目录（本质是一个inode）里查找dentry文件
 */
static struct dentry *myfs_lookup(struct inode * dir, struct dentry *dentry, unsigned int flags)
{
	struct inode * inode;
	ino_t ino;
	
	ino = myfs_inode_by_name(dir, &dentry->d_name);  //inode_by_name函数需要写
	inode = NULL;
	if (ino) {
		inode = myfs_iget(dir->i_sb, ino);
	}
	return d_splice_alias(inode, dentry);
}

/*
 *创建文件 利用已经创建好的dentry结构体 创建inode 
 */
/*
static int myfs_create (struct inode * dir, struct dentry * dentry, umode_t mode, bool excl)
{
	struct inode *inode;

	dquot_initialize(dir);

	inode = myfs_new_inode(dir, mode, &dentry->d_name);//myfs_new_inode没写
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode->i_op = &myfs_file_inode_operations;

	inode->i_mapping->a_ops = &myfs_aops;
	inode->i_fop = &myfs_file_operations;

	mark_inode_dirty(inode);
	return myfs_add_nondir(dentry, inode);
}
*/

/*
 *根据文件模式 设备号创建一个新的inode
 */
/*
static int myfs_mknod (struct inode * dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct inode * inode;
	int err;

	if (!new_valid_dev(rdev))
		return -EINVAL;

	dquot_initialize(dir);

	inode = myfs_new_inode (dir, mode, &dentry->d_name);
	err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		init_special_inode(inode, inode->i_mode, rdev);  //linux/fs/inode.c
		
		mark_inode_dirty(inode);
		err = myfs_add_nondir(dentry, inode);
	}
	return err;
}
*/

static int myfs_add_link(struct dentry *dentry, struct inode *inode)
{
	return 1;
}


/* 
 * 建立目录inode和dentry之间的硬链接
 */

static int myfs_link (struct dentry * old_dentry, struct inode * dir,
	struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;
	int err;

	dquot_initialize(dir);

	inode->i_ctime = CURRENT_TIME_SEC;
	inode_inc_link_count(inode);
	ihold(inode);

	err = myfs_add_link(dentry, inode);
	if (!err) {
		d_instantiate(dentry, inode);
		return 0;
	}
	inode_dec_link_count(inode);
	iput(inode);
	return err;
}

const struct inode_operations myfs_dir_inode_operations = {
//	.create		= myfs_create,
	.lookup		= myfs_lookup,
	.link		= myfs_link,
//	.unlink		= myfs_unlink,
//	.symlink	= myfs_symlink,
//	.mkdir		= myfs_mkdir,
//	.rmdir		= myfs_rmdir,
//	.mknod		= myfs_mknod,
//	.rename		= myfs_rename,
//	.setattr	= myfs_setattr,
//	.get_acl	= myfs_get_acl,
//	.set_acl	= myfs_set_acl,
//	.tmpfile	= myfs_tmpfile,
};


