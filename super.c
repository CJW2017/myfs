#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>
#include <linux/vfs.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include <linux/log2.h>
#include <linux/quotaops.h>
#include <asm/uaccess.h>
#include "myfs.h"

//extern struct inode *myfs_iget(struct super_block *sb, unsigned long ino);
struct kmem_cache * myfs_inode_cachep;

/*myfs_statfs函数先将fs的状态存放到内核态的kstatfs指针中
 *
 *
 *
 *
 */

/*myfs_alloc_inode*/
static struct inode *myfs_alloc_inode(struct super_block *sb)
{
    struct myfs_inode_info *ei;
    ei =(struct myfs_inode_info *) kmem_cache_alloc(myfs_inode_cachep, GFP_KERNEL);
    if(!ei)
        return NULL;
    ei->i_block_alloc_info = NULL;
    ei->vfs_inode.i_version = 1;
    
    return &ei->vfs_inode;        
}




				
/*
 *把fs的状态 存到kstatfs指针里 有什么用？？
 */
static int myfs_statfs (struct dentry * dentry, struct kstatfs * buf)
{
	struct super_block *sb =dentry->d_sb;
	struct myfs_sb_info *sbi = MYFS_SB(sb);
	struct myfs_super_block *es = sbi->s_es;
	
	buf->f_type		= sb->s_magic;
	buf->f_bsize	= sb->s_blocksize;
	buf->f_blocks	= es->s_blocks_count; 
	buf->f_bfree	= 0;//myfs_count_free_blocks(sb);
	buf->f_bavail	= buf->f_bfree - le32_to_cpu(es->s_r_blocks_count);
	buf->f_files 	= le32_to_cpu(es->s_inodes_count);		//
	buf->f_ffree	= 0;//myfs_count_free_inodes(sb);
	buf->f_namelen	= MYFS_NAME_LEN;
//	buf->f_fsid		= 		//	
//	buf->f_frsize	= 		//
//	buf->f_flags	=		//
//	buf->f_spare	=		//
	
	return 0;
	
}
/*
static struct dentry *myfs_mount (struct file_system_type *fs_type,
        int flags, const char *dev_name, void *data)
{
    return mount_bdev(fs_type, flags, dev_name, data, myfs_fill_super);        
}
*/




/*
 *同步超级块
 *更新空闲块和i节点数      空闲快数量和空闲i节点数量都是通过读位图出来的（我们没有位图 所以暂时让这两等于0？）
 *同时更新dirty标志
 */
static void myfs_sync_super(struct super_block *sb,
			    struct myfs_super_block *es, int wait)
{
	/**/
	es->s_free_blocks_count = 1;//cpu_to_le32是大小端转换
	es->s_free_inodes_count = 1;//就暂时让他为0
	es->s_wtime = cpu_to_le32(get_seconds());   //writetime
	mark_buffer_dirty(MYFS_SB(sb)->s_sbh);
	if(wait)
		sync_dirty_buffer(MYFS_SB(sb)->s_sbh);
		
}

/*同步文件系统（实际就是同步超级块）*/
static int myfs_sync_fs(struct super_block *sb, int wait)
{
	struct myfs_sb_info *sbi = MYFS_SB(sb);
	struct myfs_super_block *es = MYFS_SB(sb)->s_es;
	
	myfs_sync_super(sb, es, wait);
	return 0;
}

/**/
/*
unsigned long long simple_strtoul(const char *cp, char **endp unsigned int base)
{
        return simple_strtoul(cp, endp ,base);
        }
*/

/*get the number of superblock */
static unsigned long get_sb_block(void **data)
{
        unsigned long sb_block;
        char    *options = (char *) *data;

        if(!options ||strncmp(options , "sb=", 3)!= 0)
                return 1;
        options += 3;
        sb_block = simple_strtoul(options, &options, 0);
        if(*options && *options !=','){
             printk("MYFS :invalid %s\n",(char *)*data);
             return 1;
        }
        if(*options == ',')
                options++;
        *data = (void *)options;
        return sb_block;
        }


/*
 * 写超级块(实际上是同步文件系统、超级块)
 * 发生在xxx之后
 */
static void myfs_write_super(struct super_block *sb){
	if (!(sb->s_flags & MS_RDONLY))
		myfs_sync_fs(sb, 1);
	
}

/*释放超级块*/
static void myfs_put_super(struct super_block *sb)
{
	struct myfs_sb_info *sbi = MYFS_SB(sb);
	
	sb->s_fs_info = NULL;
	brelse(sbi->s_sbh);
	kfree(sbi);
	
}


static const struct super_operations myfs_sops = {
	.alloc_inode	= myfs_alloc_inode, 	//分配inode
//	.destroy_inode	= myfs_destroy_inode,	//销毁inode
	.write_inode	= myfs_write_inode,		//写inode
	//.evict_inode	= myfs_evict_inode,
	.put_super		= myfs_put_super,		//释放超级块
	.write_inode	= myfs_write_inode,		//
	.sync_fs		= myfs_sync_fs,			//同步fs
	.statfs			= myfs_statfs,
};






static int myfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct buffer_head * bh;
	struct myfs_sb_info * sbi; 				//内存中的sb
	struct myfs_super_block * es; 				//物理介质上的sb
	struct inode *root;				
	unsigned long block;
	unsigned long sb_block = get_sb_block(&data);		//超级块号
	unsigned long logic_sb_block;				//逻辑超级块号
	unsigned long offset = 0;
	unsigned long def_mount_opts;
	long ret = -EINVAL;
	int blocksize = BLOCK_SIZE;
	int db_count;						//
	int i, j;
	__le32 features;
	int err;

	err = -ENOMEM;
	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		goto failed;

	sbi->s_blockgroup_lock =
		kzalloc(sizeof(struct blockgroup_lock), GFP_KERNEL);
	if (!sbi->s_blockgroup_lock) {
		kfree(sbi);
		goto failed;
	}
	sb->s_fs_info = sbi;
	sbi->s_sb_block = sb_block;   		//找到超级块号

	spin_lock_init(&sbi->s_lock);		//自旋锁 可以去掉吧

	blocksize = sb_min_blocksize(sb, BLOCK_SIZE);

	if (!blocksize) {
		myfs_msg(sb, KERN_ERR, "error: unable to set blocksize");
		goto failed_sbi;
	}

	if(blocksize != BLOCK_SIZE){
		logic_sb_block = (sb_block*BLOCK_SIZE) / blocksize;
        offset = (sb_block*BLOCK_SIZE) % blocksize;
    } 
	else {
      	logic_sb_block = sb_block;
    }
	
	if (!(bh = sb_bread(sb, logic_sb_block))) {		//读取超级块
		myfs_msg(sb, KERN_ERR, "error: unable to read superblock");
		goto failed_sbi;
	}
	/*
	 * Note: s_es must be initialized as soon as possible because
	 *       some ext2 macro-instructions depend on its value
	 */
	//把bread函数读到的bh中的data（超级块的实际信息）传给es
	es = (struct myfs_super_block *) (((char *)bh->b_data) + offset);
	sbi->s_sbh = bh;
	sbi->s_es = es;
	sb->s_fs_info = sbi;

	
	//魔数 是否是myfs
	sb->s_magic = le16_to_cpu(es->s_magic);
	if (sb->s_magic != MYFS_SUPER_MAGIC)
		goto cantfind_myfs;

	sb->s_maxbytes = 1 << 44;
	sb->s_max_links = 1024;

//	sb->s_flags = 

	//暂时不知道有什么用
	/* Set defaults before we parse the mount options */
	def_mount_opts = le32_to_cpu(es->s_default_mount_opts);
	sbi->s_mount_opt = def_mount_opts;

	//uid gid
	sbi->s_resuid = make_kuid(&init_user_ns, le16_to_cpu(es->s_def_resuid));
	sbi->s_resgid = make_kgid(&init_user_ns, le16_to_cpu(es->s_def_resgid));
	


	blocksize = BLOCK_SIZE << le32_to_cpu(sbi->s_es->s_log_block_size);
   	if (sb->s_blocksize != blocksize) {
        brelse(bh);

        if (!sb_set_blocksize(sb, blocksize)) {
            myfs_msg(sb, KERN_ERR, "error: blocksize is too small");
            goto failed_sbi;
        }

        logic_sb_block = (sb_block*BLOCK_SIZE) / blocksize;
        offset = (sb_block*BLOCK_SIZE) % blocksize;
        bh = sb_bread(sb, logic_sb_block);
        if(!bh) {
            myfs_msg(sb, KERN_ERR, "error: couldn't read"
                "superblock on 2nd try");
            goto failed_sbi;
        }
        es = (struct myfs_super_block *) (((char *)bh->b_data) + offset);
        sbi->s_es = es;
        if (es->s_magic != cpu_to_le16(MYFS_SUPER_MAGIC)) {
            myfs_msg(sb, KERN_ERR, "error: magic mismatch");
            goto failed_mount;
        }
    }	


	/* If the blocksize doesn't match, re-read the thing.. */


	
/*sb_info填充*/
	sbi->s_inode_size = le16_to_cpu(es->s_inode_size);
	sbi->s_first_ino = le32_to_cpu(es->s_first_ino);
	sbi->s_frag_size = MYFS_FRAG_SIZE;
	if (sbi->s_frag_size == 0)
		goto cantfind_myfs;
	sbi->s_frags_per_block = sb->s_blocksize / sbi->s_frag_size;

	sbi->s_blocks_per_group = le32_to_cpu(es->s_blocks_per_group);
	sbi->s_frags_per_group = le32_to_cpu(es->s_frags_per_group);
	sbi->s_inodes_per_group = le32_to_cpu(es->s_inodes_per_group);

	sbi->s_inodes_per_block = sb->s_blocksize / MYFS_INODE_SIZE;
	if (sbi->s_inodes_per_block == 0 || sbi->s_inodes_per_group == 0)
		goto cantfind_myfs;
//	sbi->s_itb_per_group = sbi->s_inodes_per_group /
//					sbi->s_inodes_per_block;
//	sbi->s_desc_per_block = sb->s_blocksize /                
//					sizeof (struct ext2_group_desc);
	sbi->s_sbh = bh;
	sbi->s_mount_state = le16_to_cpu(es->s_state);
//	sbi->s_addr_per_block_bits =						
//		ilog2 (EXT2_ADDR_PER_BLOCK(sb));
//	sbi->s_desc_per_block_bits =
//		ilog2 (EXT2_DESC_PER_BLOCK(sb));

/*错误 */
	if (sb->s_magic != MYFS_SUPER_MAGIC)
		goto cantfind_myfs;

	if (sb->s_blocksize != bh->b_size) {
		if (!silent)
			myfs_msg(sb, KERN_ERR, "error: unsupported blocksize");
		goto failed_mount;
	}

	if (sb->s_blocksize != sbi->s_frag_size) {
		myfs_msg(sb, KERN_ERR,
			"error: fragsize %lu != blocksize %lu"
			"(not supported yet)",
			sbi->s_frag_size, sb->s_blocksize);
		goto failed_mount;
	}


	
//	sbi->s_gdb_count = db_count;
	get_random_bytes(&sbi->s_next_generation, sizeof(u32));
	spin_lock_init(&sbi->s_next_gen_lock);


	sb->s_op = &myfs_sops;

	root = myfs_iget(sb, MYFS_ROOT_INO); //找到根节点
	
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		printk(KERN_ERR "myfs : can't find root inode");
		goto failed_mount;
	}
	if (!S_ISDIR(root->i_mode) || !root->i_blocks || !root->i_size) {
		iput(root);
		myfs_msg(sb, KERN_ERR, "error: corrupt root inode, run e2fsck");
		goto failed_mount;
	}

	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		iput(root);
		myfs_msg(sb, KERN_ERR, "error: get root inode failed");
		ret = -ENOMEM;
		goto failed_mount;
	}

	
	myfs_write_super(sb);
	return 0;

cantfind_myfs:
	if (!silent)
		myfs_msg(sb, KERN_ERR,
			"error: can't find an myfs filesystem on dev %s.",
			sb->s_id);
	goto failed_mount;
failed_mount:
	brelse(bh);
failed_sbi:
	sb->s_fs_info = NULL;
	kfree(sbi->s_blockgroup_lock);
	kfree(sbi);
failed:
	return ret;
}


/*Mount 函数*/
struct dentry *myfs_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, myfs_fill_super);
}


//static int ext2_freeze(struct super_block *sb);
//static int ext2_unfreeze(struct super_block *sb);
//static int ext2_remount (struct super_block * sb, int * flags, char * data);

void myfs_msg(struct super_block *sb, const char *prefix,
		const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk("%sEXT2-fs (%s): %pV\n", prefix, sb->s_id, &vaf);

	va_end(args);
}




/*
 *初始化 跳转到inode初始化
 */
static void init_once(void *foo)
{
	struct myfs_inode_info *ei = (struct myfs_inode_info *) foo;
	inode_init_once(&ei->vfs_inode); //<linux/fs/inode.c>
	
}

/*初始化inodecache*/
static int __init init_inodecache(void)
{
	myfs_inode_cachep = kmem_cache_create("myfs_inode_cache",
					     sizeof(struct myfs_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once);
	if (myfs_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

/*销毁inodecache*/
static void destroy_inodecache(void)
{
	kmem_cache_destroy(myfs_inode_cachep);
}


static int myfs_setup_super (struct super_block * sb,
			      struct myfs_super_block * es,
			      int read_only)
{
	int res = 0;
	struct myfs_sb_info *sbi = MYFS_SB(sb);

	if (le32_to_cpu(es->s_rev_level) > 1) {     //  EXT2_MAX_SUPP_
		myfs_msg(sb, KERN_ERR,
			"error: revision level too high, "
			"forcing read-only mode");
		res = MS_RDONLY;
	}
	if (read_only)
		return res;
/*	if (!(sbi->s_mount_state & EXT2_VALID_FS))
		myfs_msg(sb, KERN_WARNING,
			"warning: mounting unchecked fs, "
			"running e2fsck is recommended");
	else if ((sbi->s_mount_state & EXT2_ERROR_FS))
		myfs_msg(sb, KERN_WARNING,
			"warning: mounting fs with errors, "
			"running e2fsck is recommended");
	else if ((__s16) le16_to_cpu(es->s_max_mnt_count) >= 0 &&
		 le16_to_cpu(es->s_mnt_count) >=
		 (unsigned short) (__s16) le16_to_cpu(es->s_max_mnt_count))
		myfs_msg(sb, KERN_WARNING,
			"warning: maximal mount count reached, "
			"running e2fsck is recommended");
	else if (le32_to_cpu(es->s_checkinterval) &&
		(le32_to_cpu(es->s_lastcheck) +
			le32_to_cpu(es->s_checkinterval) <= get_seconds()))
		myfs_msg(sb, KERN_WARNING,
			"warning: checktime reached, "
			"running e2fsck is recommended");
	if (!le16_to_cpu(es->s_max_mnt_count))
		es->s_max_mnt_count = cpu_to_le16(EXT2_DFL_MAX_MNT_COUNT);
	le16_add_cpu(&es->s_mnt_count, 1);
	if (test_opt (sb, DEBUG))
		myfs_msg(sb, KERN_INFO, "%s, %s, bs=%lu, fs=%lu, gc=%lu, "
			"bpg=%lu, ipg=%lu, mo=%04lx]",
			EXT2FS_VERSION, EXT2FS_DATE, sb->s_blocksize,
			sbi->s_frag_size,
			sbi->s_groups_count,
			EXT2_BLOCKS_PER_GROUP(sb),
			EXT2_INODES_PER_GROUP(sb),
			sbi->s_mount_opt);
*/
	return res;
}


static struct file_system_type myfs_fs_type = {  
    .owner      = THIS_MODULE,  
    .name       = "myfs",  
    .mount		= myfs_mount,
//  .get_sb     = myfs_get_sb,  //?
    .kill_sb    = kill_block_super,  
    .fs_flags   = FS_REQUIRES_DEV,  
};  
/*myfs的模块初始化*/  
static int __init init_myfs_fs(void)  
{  
        int err;
	err = init_inodecache();
	if(err)
		return err;
        err = register_filesystem(&myfs_fs_type);   	//???
	if(err)
		goto out;
	return 0;
out:
	destroy_inodecache();	
	return err;
}  
/*myfs的模块清理*/  
static void __exit exit_myfs_fs(void)  
{  
    /*注销myfs*/  
    unregister_filesystem(&myfs_fs_type);  				//??
    destroy_inodecache();  
 
}  

MODULE_DESCRIPTION("LINER FILESYSTEM");
MODULE_AUTHOR("JIKUNSHANG");
MODULE_LICENSE("GPL");
  
module_init(init_myfs_fs)  
module_exit(exit_myfs_fs) 













