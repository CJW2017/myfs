#include <linux/fs.h>
#include <linux/blockgroup_lock.h>
#include <linux/percpu_counter.h>
#include <linux/rbtree.h>

#define MYFS_BLOCK_SIZE    	4096
#define MYFS_BLOCK_SIZE_BITS	2
#define MYFS_INODE_SIZE		32
#define MYFS_INODE_NUM		256
#define MYFS_SUPER_MAGIC 	0x1011
#define MYFS_FRAG_SIZE		4096

#define MYFS_STATE_NEW 		0x00000001
#define MYFS_ROOT_INO		2

#define MYFS_NAME_LEN		8
#define MYFS_REC_LEN        16

#define set_opt(o, opt)     o |= opt;


typedef unsigned long myfs_fsblk_t ;

/*内存中的sb信息*/
struct myfs_sb_info {
	unsigned long s_frag_size;	/* Size of a fragment in bytes */
	unsigned long s_frags_per_block;/* Number of fragments per block */
	unsigned long s_inodes_per_block;/* Number of inodes per block */
	unsigned long s_frags_per_group;/* Number of fragments in a group */
	unsigned long s_blocks_per_group;/* Number of blocks in a group */
	unsigned long s_inodes_per_group;/* Number of inodes in a group */
	unsigned long s_itb_per_group;	/* Number of inode table blocks per group */
	unsigned long s_gdb_count;	/* Number of group descriptor blocks */
	unsigned long s_desc_per_block;	/* Number of group descriptors per block */
	unsigned long s_groups_count;	/* Number of groups in the fs */
	unsigned long s_overhead_last;  /* Last calculated overhead */
	unsigned long s_blocks_last;    /* Last seen block count */
	struct buffer_head * s_sbh;	/* Buffer containing the super block */
	struct myfs_super_block * s_es;	/* Pointer to the super block in the buffer */
	struct buffer_head ** s_group_desc;
	unsigned long  s_mount_opt;
	unsigned long s_sb_block;
	kuid_t s_resuid;
	kgid_t s_resgid;
	unsigned short s_mount_state;
	unsigned short s_pad;
	int s_addr_per_block_bits;
	int s_desc_per_block_bits;
	int s_inode_size;
	int s_first_ino;
	spinlock_t s_next_gen_lock;
	u32 s_next_generation;
	unsigned long s_dir_count;
	u8 *s_debts;
//	struct percpu_counter s_freeblocks_counter;
//	struct percpu_counter s_freeinodes_counter;
//	struct percpu_counter s_dirs_counter;
	struct blockgroup_lock *s_blockgroup_lock;
	/* root of the per fs reservation window tree */
	spinlock_t s_rsv_window_lock;
	struct rb_root s_rsv_window_root;
//	struct myfs_reserve_window_node s_rsv_window_head;
	/*
	 * s_lock protects against concurrent modifications of s_mount_state,
	 * s_blocks_last, s_overhead_last and the content of superblock's
	 * buffer pointed to by sbi->s_es.
	 *
	 * Note: It is used in ext2_show_options() to provide a consistent view
	 * of the mount options.
	 */
	spinlock_t s_lock;
};

/*物理介质上的super_block*/
struct myfs_super_block{
	__le32	s_inodes_count;		/* inode数量 */
	__le32	s_blocks_count;		/* 块的数量 */
	__le32	s_r_blocks_count;	/* 保留的块数Reserved blocks count */
	__le32	s_free_blocks_count;	/* 空闲的块数 Free blocks count */
	__le32	s_free_inodes_count;	/* 空闲的inode数 Free inodes count */
	__le32	s_first_data_block;	/* 0号块组起始块号 First Data Block */
	__le32	s_log_block_size;	/* 块大小Block size */
	__le32	s_log_frag_size;	/* Fragment size */
	__le32	s_blocks_per_group;	/* 每个块组包含的快数量# Blocks per group */
	__le32	s_frags_per_group;	/* # Fragments per group */
	__le32	s_inodes_per_group;	/* 每个块组包含的inode数量# Inodes per group */
	__le32	s_mtime;		/* 挂载时间Mount time */
	__le32	s_wtime;		/* 文件系统写入时间Write time */
	__le16	s_mnt_count;		/* 挂载数Mount count */
	__le16	s_max_mnt_count;	/* 最大挂载数Maximal mount count */
	__le16	s_magic;		/* 签名标志Magic signature */
	__le16	s_state;		/* 文件系统状态File system state */
	__le16	s_errors;		/* 错误处理方式Behaviour when detecting errors */
	__le16	s_minor_rev_level; 	/* 辅版本号minor revision level */
	__le32	s_lastcheck;		/* 最后一次一致性检查时间time of last check */
	__le32	s_checkinterval;	/* 检查时间间隔max. time between checks */
	__le32	s_creator_os;		/* 创建文件系统的 操作系统OS */
	__le32	s_rev_level;		/* 主版本号Revision level */
	__le16	s_def_resuid;		/* 保留uidDefault uid for reserved blocks */
	__le16	s_def_resgid;		/* 保留gidDefault gid for reserved blocks */
	/*
	 * These fields are for EXT2_DYNAMIC_REV superblocks only.
	 *
	 * Note: the difference between the compatible feature set and
	 * the incompatible feature set is that if there is a bit set
	 * in the incompatible feature set that the kernel doesn't
	 * know about, it should refuse to mount the filesystem.
	 * 
	 * e2fsck's requirements are more strict; if it doesn't know
	 * about a feature in either the compatible or incompatible
	 * feature set, it must abort and not try to meddle with
	 * things it doesn't understand...
	 */
	__le32	s_first_ino; 		/* First non-reserved inode */
	__le16   s_inode_size; 		/* size of inode structure */
	__le16	s_block_group_nr; 	/* block group # of this superblock */
	__le32	s_feature_compat; 	/* compatible feature set */
	__le32	s_feature_incompat; 	/* incompatible feature set */
	__le32	s_feature_ro_compat; 	/* readonly-compatible feature set */
	__u8	s_uuid[16];		/* 128-bit uuid for volume */
	char	s_volume_name[16]; 	/* volume name */
	char	s_last_mounted[64]; 	/* directory where last mounted */
	__le32	s_algorithm_usage_bitmap; /* For compression */
	/*
	 * Performance hints.  Directory preallocation should only
	 * happen if the EXT2_COMPAT_PREALLOC flag is on.
	 */
	__u8	s_prealloc_blocks;	/* Nr of blocks to try to preallocate*/
	__u8	s_prealloc_dir_blocks;	/* Nr to preallocate for dirs */
	__u16	s_padding1;
	/*
	 * Journaling support valid if EXT3_FEATURE_COMPAT_HAS_JOURNAL set.
	 */
	__u8	s_journal_uuid[16];	/* uuid of journal superblock */
	__u32	s_journal_inum;		/* inode number of journal file */
	__u32	s_journal_dev;		/* device number of journal file */
	__u32	s_last_orphan;		/* start of list of inodes to delete */
	__u32	s_hash_seed[4];		/* HTREE hash seed */
	__u8	s_def_hash_version;	/* Default hash version to use */
	__u8	s_reserved_char_pad;
	__u16	s_reserved_word_pad;
	__le32	s_default_mount_opts;
 	__le32	s_first_meta_bg; 	/* First metablock block group */
	__u32	s_reserved[190];	/* Padding to the end of the block */
	
	
	
};

/*磁盘中的inode *raw_inode 
 *每个inode节点一共32个Bytes (256bit) 即每一次创建需要写一个256位的列表
 *分别是文件模式（2字节）、
 *文件大小（高低工8字节）、
 *创建时间（4字节） 、访问时间（4字节）、 
 *文件分配的块数（2字节）（不够用 但是end-start可以得到）
 *起始块指针（4字节） 结束块指针（4字节）
 *UID（2字节） GID（2字节）
 */
struct myfs_inode{
	__le16	i_mode;		//文件模式
	__le16	i_blocks;	//文件分配的块数
	__le32	i_size_low;	//文件大小低位
	__le32  i_size_high;//文件大小高位
	__le32 	i_ctime;	//创建时间
	__le32	i_atime; 	//访问时间
	__le32	i_start_blk;//起始块指针
	__le32	i_end_blk;	//结束块指针
	__le16  i_uid;  	//UID
	__le16  i_gid;		//GID
};

/*内存中的inode *ei */
struct myfs_inode_info{
	__le32 	i_data[15];
	__u32 	i_flags;
	__u32 	i_faddr;
//	__u32	i_faddr;
	__u8	i_frag_no;
	__u8	i_frag_size;
	__u16	i_state;
	__u32	i_file_acl;
	__u32	i_dir_acl;
	__u32	i_dtime;
	__u32	i_block_group;	
	struct myfs_block_alloc_info *i_block_alloc_info;  //????
	__u32	i_dir_start_lookup;
	rwlock_t i_meta_lock;
	struct inode	vfs_inode;  //vfsinode
    __le32  i_start_blk;
    __le32  i_end_blk;
	__le32  i_blocks;
 	struct mutex truncate_mutex;  	
};

/*
 *每一个目录项的结构
 *inode号 目录入口长度 文件名长度 文件名
 *文件名是xxx.data 即8个字节 *这样每个目录项就只有16B很整齐
 *
*/
struct myfs_dir_entry{
	__le32	inode;			/* Inode number */
	__le16	rec_len;		/* Directory entry length */
	__u8	name_len;		/* Name length */
	__u8	file_type;
	char	name[8];			/* File name, up to EXT2_NAME_LEN */
};

enum {
	MYFS_FT_UNKNOWN		= 0,
	MYFS_FT_REG_FILE	= 1,
	MYFS_FT_DIR	    	= 2,
	MYFS_FT_CHRDEV		= 3,
	MYFS_FT_BLKDEV		= 4,
	MYFS_FT_FIFO		= 5,
	MYFS_FT_SOCK		= 6,
	MYFS_FT_SYMLINK		= 7,
	MYFS_FT_MAX
};



/*获取文件内存中sb中的文件系统信息 */
static inline struct myfs_sb_info *MYFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

/*
 *通过vfs_inode 返回 myfs_inode_info
 * myfs_inode_info->vfs_inode
 *
 */
static inline struct myfs_inode_info *MYFS_I(struct inode *inode)
{
	return container_of(inode, struct myfs_inode_info, vfs_inode);
}


/*super.c*/
extern void myfs_msg(struct super_block *sb, const char *perfix,
                const char *fmt, ...);

/*dir.c*/
extern int ext2_add_link (struct dentry *dentry, struct inode *inode);
extern ino_t myfs_inode_by_name(struct inode *dir, struct qstr *child);
extern struct myfs_dir_entry *myfs_find_entry (struct inode *dir ,
        struct qstr *child ,struct page ** res_page);

/*ialloc.c*/
extern struct inode *myfs_new_inode (struct inode *dir , 
        umode_t mode, const struct qstr *qstr );


/*file.c*/



/*namei.c*/



/*inode.c*/
extern struct inode *myfs_iget(struct super_block *sb, 
                    unsigned long ino);
extern int myfs_write_inode(struct inode *inode, 
                    struct writeback_control *wbc);
extern int myfs_get_block(struct inode *inode, sector_t iblock, 
                    struct buffer_head *bh_result, int create);



/*operations*/

/*inode.c*/
extern const struct address_space_operations myfs_aops;

/*file.c*/
extern const struct file_operations myfs_file_operations;
extern const struct inode_operations myfs_file_inode_operations;

/*dir.c*/
extern const struct file_operations myfs_dir_operations;

/*namei.c*/
extern const struct inode_operations myfs_dir_inode_operations;

/*ialloc.c*/

























