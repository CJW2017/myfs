/*dir.c*/

#include <linux/buffer_head.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include "myfs.h"

/*how many pages does A FILE have*/
/*
static inline unsigned long dir_pages(struct inode *inode)
{
	return (inode->i_size+PAGE_SIZE-1)>>PAGE_SHIFT;
}
*/

/*release page alloced*/
static inline void myfs_put_page(struct page *page)
{
	kunmap(page);
	put_page(page);
}


static struct page * myfs_get_page(struct inode *dir, unsigned long n,
				   int quiet)
{
	struct address_space *mapping = dir->i_mapping;   //i_mapping 
	struct page *page = read_mapping_page(mapping, n, NULL);
	if (!IS_ERR(page)) {
		kmap(page);
		if (PageError(page))
			goto fail;
	}
	return page;

fail:
	myfs_put_page(page);
	return ERR_PTR(-EIO);

}


/*blocksize of filesystem */
static inline unsigned myfs_chunk_size(struct inode *inode)
{
	return inode->i_sb->s_blocksize;
}

/*name match or not ,match renturn 1, not return 0 */
static inline int myfs_match (int len, const char * const name,
					struct myfs_dir_entry * de)
{
	if (len != de->name_len)
		return 0;
	if (!de->inode)
		return 0;
	return !memcmp(name, de->name, len);
}

/*a pointer point to next dir */
static inline struct myfs_dir_entry *myfs_next_entry(struct myfs_dir_entry *p)

{
	return (struct myfs_dir_entry *)((char *)p +
			le16_to_cpu(p->rec_len));
}

/*. is the first , so .. is the next  */
struct myfs_dir_entry * myfs_dotdot(struct inode *dir, struct page **p)
{
        struct page *page =myfs_get_page(dir, 0, 0);
        struct myfs_dir_entry *de = NULL;

        if(!(IS_ERR(page))){
                de = myfs_next_entry((struct myfs_dir_entry *) page_address(page));
                *p = page;
                }
        return de;
        }

/**/
static unsigned myfs_last_byte(struct inode *inode, unsigned long page_nr)
{
        unsigned last_byte = inode->i_size;
        last_byte -= page_nr << PAGE_SHIFT;

        if(last_byte > PAGE_SIZE)
                last_byte = PAGE_SIZE;
        
        return last_byte;
        
        }
static int myfs_commit_chunk(struct page *page, loff_t pos, unsigned len)
{
    struct address_space *mapping = page->mapping;
    struct inode *dir = mapping->host;
    int err = 0;
    
    dir->i_version++;
    block_write_end(NULL, mapping, pos, len, len, page, NULL);
    if (pos+len > dir->i_size) {
        i_size_write(dir, pos+len);
        mark_inode_dirty(dir);
    }
    if (IS_DIRSYNC(dir)) {
    err = write_one_page(page, 1);
     
    if (!err)
    err = sync_inode_metadata(dir, 1);
    }
    else {
          unlock_page(page);
    }
    return err;
}
static unsigned char myfs_filetype_table[MYFS_FT_MAX] = {
	[MYFS_FT_UNKNOWN]	= DT_UNKNOWN,
	[MYFS_FT_REG_FILE]	= DT_REG,
	[MYFS_FT_DIR]		= DT_DIR,
	[MYFS_FT_CHRDEV] 	= DT_CHR,
	[MYFS_FT_BLKDEV]	= DT_BLK,
	[MYFS_FT_FIFO]		= DT_FIFO,
	[MYFS_FT_SOCK]		= DT_SOCK,
	[MYFS_FT_SYMLINK]	= DT_LNK,
};



/**/
#define S_SHIFT 12
static unsigned char myfs_type_by_mode[S_IFMT >> S_SHIFT] = {
    [S_IFREG >> S_SHIFT]    = MYFS_FT_REG_FILE,        
    [S_IFDIR >> S_SHIFT]    = MYFS_FT_DIR,        
    [S_IFCHR >> S_SHIFT]    = MYFS_FT_CHRDEV,        
    [S_IFBLK >> S_SHIFT]    = MYFS_FT_BLKDEV,        
    [S_IFIFO >> S_SHIFT]    = MYFS_FT_FIFO,        
    [S_IFSOCK >> S_SHIFT]   = MYFS_FT_SOCK,        
    [S_IFLNK >> S_SHIFT]   = MYFS_FT_SYMLINK,        
};




/**/
static inline void myfs_set_de_type(struct myfs_dir_entry *de, struct inode *inode )
{
        umode_t mode = inode->i_mode;
        de->file_type = myfs_type_by_mode[(mode & S_IFMT)>>S_SHIFT];
        
        }


/**/

int myfs_add_link (struct dentry *dentry, struct inode *inode)
{
	struct inode *dir = dentry->d_parent->d_inode;
	const char *name = dentry->d_name.name;
	int namelen = dentry->d_name.len;
	unsigned chunk_size = myfs_chunk_size(dir);
	unsigned reclen = MYFS_REC_LEN; //EXT2_DIR_REC_LEN(namelen);
	unsigned short rec_len, name_len;
	struct page *page = NULL;
	struct  myfs_dir_entry * de;
	unsigned long npages = dir_pages(dir);
	unsigned long n;
	char *kaddr;
	loff_t pos;
	int err;

	/*
	 * We take care of directory expansion in the same loop.
	 * This code plays outside i_size, so it locks the page
	 * to protect that region.
	 */
	for (n = 0; n <= npages; n++) {
		char *dir_end;

		page = myfs_get_page(dir, n, 0);
		err = PTR_ERR(page);
		if (IS_ERR(page))
			goto out;
		lock_page(page);
		kaddr = page_address(page);
		dir_end = kaddr + myfs_last_byte(dir, n); //
		de = (struct myfs_dir_entry *)kaddr;
		kaddr += PAGE_SIZE - reclen;
		while ((char *)de <= kaddr) {
			if ((char *)de == dir_end) {
				/* We hit i_size */
				name_len = 0;
				rec_len = chunk_size;
				de->rec_len = MYFS_REC_LEN;//
				de->inode = 0;
				goto got_it;
			}
			if (de->rec_len == 0) {
				err = -EIO;
				goto out_unlock;
			}
			err = -EEXIST;
			if (myfs_match (namelen, name, de))
				goto out_unlock;
			name_len = MYFS_NAME_LEN;
			rec_len = MYFS_REC_LEN;  //
			if (!de->inode && rec_len >= reclen)
				goto got_it;
			if (rec_len >= name_len + reclen)
				goto got_it;
			de = (struct myfs_dir_entry *) ((char *) de + rec_len);
		}
		unlock_page(page);
		myfs_put_page(page);
	}
	BUG();
	return -EINVAL;

got_it:
	pos = page_offset(page) +
		(char*)de - (char*)page_address(page);
	err = __block_write_begin(page, pos, rec_len, myfs_get_block);
	if (err)
		goto out_unlock;
	if (de->inode) {
		struct myfs_dir_entry *de1 = (struct myfs_dir_entry *) ((char *) de + name_len);
		de1->rec_len = MYFS_REC_LEN;
		de->rec_len = MYFS_REC_LEN;
		de = de1;
	}
	de->name_len = namelen;
	memcpy(de->name, name, namelen);
	de->inode = cpu_to_le32(inode->i_ino);
    myfs_set_de_type (de, inode);
	err = myfs_commit_chunk(page, pos, rec_len);
	dir->i_mtime = dir->i_ctime = CURRENT_TIME_SEC;
	MYFS_I(dir)->i_flags &= ~FS_BTREE_FL;
	mark_inode_dirty(dir);
	/* OFFSET_CACHE */
out_put:
	myfs_put_page(page);
out:
	return err;
out_unlock:
	unlock_page(page);
	goto out_put;
}

static inline unsigned myfs_validate_entry(char *base, unsigned offset, unsigned mask)
{
	struct myfs_dir_entry *de = (struct myfs_dir_entry*)(base + offset);
	struct myfs_dir_entry *p = (struct myfs_dir_entry*)(base + (offset&mask));
	while((char*)de > (char*)p ){
		if(p->rec_len == 0)
			break;
		p = myfs_next_entry(p);
	}
	return (char*)p - base;
}


/**/
static int
myfs_readdir(struct file *file, struct dir_context *ctx)
{
	loff_t pos = ctx->pos;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	unsigned int offset = pos & ~PAGE_MASK;
	unsigned long n = pos >> PAGE_SHIFT;
	unsigned long npages = dir_pages(inode);
	unsigned chunk_mask = ~(myfs_chunk_size(inode)-1);//
	unsigned char *types = NULL;
	int need_revalidate = file->f_version != inode->i_version;
	
	types = myfs_filetype_table;

	if (pos > inode->i_size - MYFS_REC_LEN)			//
		return 0;

	for ( ; n < npages; n++, offset = 0) {
		char *kaddr, *limit;
		struct myfs_dir_entry *de;
		struct page *page = myfs_get_page(inode, n, 0);

		if (IS_ERR(page)) {
			myfs_msg(sb, __func__,
				   "bad page in #%lu",
				   inode->i_ino);
			ctx->pos += PAGE_SIZE - offset;
			return PTR_ERR(page);
		}
		kaddr = page_address(page);
		if (unlikely(need_revalidate)) {
			if (offset) {
				offset = myfs_validate_entry(kaddr, offset, chunk_mask); //validate_entry
				ctx->pos = (n<<PAGE_SHIFT) + offset;
			}
			file->f_version = inode->i_version;
			need_revalidate = 0;
		}
		de = (struct myfs_dir_entry *)(kaddr+offset);
		limit = kaddr + myfs_last_byte(inode, n) - MYFS_REC_LEN ; //
		for ( ;(char*)de <= limit; de = myfs_next_entry(de)) {
			if (de->rec_len == 0) {
				myfs_msg(sb, __func__,
					"zero-length directory entry");
				myfs_put_page(page);
				return -EIO;
			}
			if (de->inode) {
				unsigned char d_type = DT_UNKNOWN;

				if (types && de->file_type < MYFS_FT_MAX)
					d_type = types[de->file_type];

				if (!dir_emit(ctx, de->name, de->name_len,
						le32_to_cpu(de->inode),
						d_type)) {
					myfs_put_page(page);
					return 0;
				}
			}
			ctx->pos += le16_to_cpu(de->rec_len) ;//ext2_rec_len_from_disk
		}
		myfs_put_page(page);
	}
	return 0;
}

/* */



/**/
struct myfs_dir_entry *myfs_find_entry (struct inode * dir,
			struct qstr *child, struct page ** res_page)
{
	const char *name = child->name;
	int namelen = child->len;
	unsigned reclen = 16;
	unsigned long start, n;
	unsigned long npages = dir_pages(dir);
	struct page *page = NULL;
	struct myfs_inode_info *ei = MYFS_I(dir);
	struct myfs_dir_entry * de;
	int dir_has_error = 0;

	if (npages == 0)
		goto out;

	/* OFFSET_CACHE */
	*res_page = NULL;

	start = ei->i_dir_start_lookup;
	if (start >= npages)
		start = 0;
	n = start;
	do {
		char *kaddr;
		page = myfs_get_page(dir, n, dir_has_error);
		if (!IS_ERR(page)) {
			//转换成虚拟地址然后转换成myfs目录项指针
			kaddr = page_address(page);
			de = (struct myfs_dir_entry *) kaddr;
			//边界地址
			kaddr += myfs_last_byte(dir, n) - reclen;  //	myfs_last_byte
			while ((char *) de <= kaddr) {
				
				if (myfs_match(namelen, name, de))  //myfs_match
					goto found;
				de = myfs_next_entry(de);  //myfs_next_entry
			}
			myfs_put_page(page);
		} else
			dir_has_error = 1;

		if (++n >= npages)
			n = 0;

	} while (n != start);
out:
	return NULL;

found:
	*res_page = page;
	ei->i_dir_start_lookup = n;
	return de;
}

ino_t myfs_inode_by_name(struct inode *dir, struct qstr *child)
{
	ino_t res = 0;
	struct myfs_dir_entry *de;
	struct page *page;
	
	de = myfs_find_entry(dir, child, &page);
	if (de) {
		res = le32_to_cpu(de->inode);
		myfs_put_page(page);
	}
	return res;
}







const struct file_operations myfs_dir_operations = {
	.llseek		= generic_file_llseek,
	.read		= generic_read_dir,
	.iterate	= myfs_readdir,
//	.unlocked_ioctl = ext2_ioctl,
//	.fsync		= ext2_fsync,

};











