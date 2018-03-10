/*done*/
/*
 *file.c 
 *定义file_operations结构体 实现与VFS层的连接
 *定义的全部都是 现有的函数，没有自己写
 *这个文件可以与其他文件合并
 */
#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include <linux/time.h>
#include <linux/fs.h>
#include "myfs.h"
#include <linux/uio.h>
#include <linux/aio.h>

ssize_t myfs_read_iter(struct kiocb *iocb, struct iov_iter *iter)
{
	return generic_file_read_iter(iocb, iter);
}

const struct file_operations myfs_file_operations = {
	.llseek		= generic_file_llseek,
//	.read		= new_sync_read,
//	.write		= new_sync_write,
	.read_iter	= myfs_read_iter,
	.write_iter	= generic_file_write_iter,
//	.unlocked_ioctl = ext2_ioctl,
	.mmap		= generic_file_mmap,
	.open		= dquot_file_open,
//	.release	= ext2_release_file,
//	.fsync		= ext2_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
};


const struct inode_operations myfs_file_inode_operations = {
//	.truncate	= myfs_truncate,
//	.setattr	= ext2_setattr,
//	.get_acl	= ext2_get_acl,
//	.set_acl	= ext2_set_acl,
//	.fiemap		= ext2_fiemap,
};
