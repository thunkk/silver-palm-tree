#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/sched.h>
#include <linux/parser.h>
#include <linux/magic.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/module.h>

#define RAMFS_DEFAULT_MODE 0755
#define TESTFS_MAGIC 123456789

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ss");


const struct file_operations ramfs_file_operations = {
    .read_iter      = generic_file_read_iter,
    .write_iter     = generic_file_write_iter,
    .mmap           = generic_file_mmap,
    .fsync          = noop_fsync,
    .splice_read    = generic_file_splice_read,
    .splice_write   = iter_file_splice_write,
    .llseek         = generic_file_llseek,
};

const struct inode_operations ramfs_file_inode_operations = {
    .setattr        = simple_setattr,
    .getattr        = simple_getattr,
};

static const struct inode_operations ramfs_dir_inode_operations;

int asdf(struct page *page)
{
    pr_debug("ASDF \n");
    return 0;
}

static const struct address_space_operations ramfs_aops = {
    .readpage       = simple_readpage,
    .write_begin    = simple_write_begin,
    .write_end      = simple_write_end,
    .set_page_dirty = asdf,
};

struct inode *ramfs_get_inode(struct super_block *sb, const struct inode *dir, umode_t mode, dev_t dev)
{
    struct inode * inode = new_inode(sb);

    if (inode) {
        inode->i_ino = get_next_ino();
        inode_init_owner(inode, dir, mode);
        inode->i_mapping->a_ops = &ramfs_aops;
        mapping_set_gfp_mask(inode->i_mapping, GFP_HIGHUSER);
        mapping_set_unevictable(inode->i_mapping);
        inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
        switch (mode & S_IFMT) {
            default:
            init_special_inode(inode, mode, dev);
            break;
            case S_IFREG:
            inode->i_op = &ramfs_file_inode_operations;
            inode->i_fop = &ramfs_file_operations;
            break;
            case S_IFDIR:
            inode->i_op = &ramfs_dir_inode_operations;
            inode->i_fop = &simple_dir_operations;

            /* directory inodes start off with i_nlink == 2 (for "." entry) */
            inc_nlink(inode);
            break;
            case S_IFLNK:
            inode->i_op = &page_symlink_inode_operations;
            break;
        }
    }
    return inode;
}

/*
* File creation. Allocate an inode, and we're done..
*/
/* SMP-safe */
static int
ramfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t dev)
{
    struct inode * inode = ramfs_get_inode(dir->i_sb, dir, mode, dev);
    int error = -ENOSPC;

    if (inode) {
        d_instantiate(dentry, inode);
        dget(dentry);   /* Extra count - pin the dentry in core */
        error = 0;
        dir->i_mtime = dir->i_ctime = CURRENT_TIME;
    }
    return error;
}

static int ramfs_mkdir(struct inode * dir, struct dentry * dentry, umode_t mode)
{
    int retval = ramfs_mknod(dir, dentry, mode | S_IFDIR, 0);
    if (!retval)
    inc_nlink(dir);
    return retval;
}

static int ramfs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
    return ramfs_mknod(dir, dentry, mode | S_IFREG, 0);
}

static int ramfs_symlink(struct inode * dir, struct dentry *dentry, const char * symname)
{
    struct inode *inode;
    int error = -ENOSPC;

    inode = ramfs_get_inode(dir->i_sb, dir, S_IFLNK|S_IRWXUGO, 0);
    if (inode) {
        int l = strlen(symname)+1;
        error = page_symlink(inode, symname, l);
        if (!error) {
            d_instantiate(dentry, inode);
            dget(dentry);
            dir->i_mtime = dir->i_ctime = CURRENT_TIME;
        } else
        iput(inode);
    }
    return error;
}

static const struct inode_operations ramfs_dir_inode_operations = {
    .create         = ramfs_create,
    .lookup         = simple_lookup,
    .link           = simple_link,
    .unlink         = simple_unlink,
    .symlink        = ramfs_symlink,
    .mkdir          = ramfs_mkdir,
    .rmdir          = simple_rmdir,
    .mknod          = ramfs_mknod,
    .rename         = simple_rename,
};

static const struct super_operations ramfs_ops = {
    .statfs         = simple_statfs,
    .drop_inode     = generic_delete_inode,
    .show_options   = generic_show_options,
};

struct ramfs_mount_opts {
    umode_t mode;
};

enum {
    Opt_mode,
    Opt_err
};

static const match_table_t tokens = {
    {Opt_mode, "mode=%o"},
    {Opt_err, NULL}
};

struct ramfs_fs_info {
    struct ramfs_mount_opts mount_opts;
};

static int ramfs_parse_options(char *data, struct ramfs_mount_opts *opts)
{
    substring_t args[MAX_OPT_ARGS];
    int option;
    int token;
    char *p;

    opts->mode = RAMFS_DEFAULT_MODE;

    while ((p = strsep(&data, ",")) != NULL) {
        if (!*p)
        continue;

        token = match_token(p, tokens, args);
        switch (token) {
            case Opt_mode:
            if (match_octal(&args[0], &option))
            return -EINVAL;
            opts->mode = option & S_IALLUGO;
            break;
            /*
            * We might like to report bad mount options here;
            * but traditionally ramfs has ignored all mount options,
            * and as it is used as a !CONFIG_SHMEM simple substitute
            * for tmpfs, better continue to ignore other mount options.
            */
        }
    }

    return 0;
}

int ramfs_fill_super(struct super_block *sb, void *data, int silent)
{
    struct ramfs_fs_info *fsi;
    struct inode *inode;
    int err;

    save_mount_options(sb, data);

    fsi = kzalloc(sizeof(struct ramfs_fs_info), GFP_KERNEL);
    sb->s_fs_info = fsi;
    if (!fsi)
    return -ENOMEM;

    err = ramfs_parse_options(data, &fsi->mount_opts);
    if (err)
    return err;

    sb->s_maxbytes          = MAX_LFS_FILESIZE;
    sb->s_blocksize         = PAGE_CACHE_SIZE;
    sb->s_blocksize_bits    = PAGE_CACHE_SHIFT;
    sb->s_magic             = TESTFS_MAGIC;
    sb->s_op                = &ramfs_ops;
    sb->s_time_gran         = 1;

    inode = ramfs_get_inode(sb, NULL, S_IFDIR | fsi->mount_opts.mode, 0);
    sb->s_root = d_make_root(inode);
    if (!sb->s_root)
    return -ENOMEM;

    return 0;
}

struct dentry *ramfs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    return mount_nodev(fs_type, flags, data, ramfs_fill_super);
}

static void ramfs_kill_sb(struct super_block *sb)
{
    kfree(sb->s_fs_info);
    kill_litter_super(sb);
}

static struct file_system_type ramfs_fs_type = {
    .name           = "testfs",
    .mount          = ramfs_mount,
    .kill_sb        = ramfs_kill_sb,
    .fs_flags       = FS_USERNS_MOUNT,
};

int __init testfs_init(void)
{
    static unsigned long once;

    if (test_and_set_bit(0, &once)) {
        return 0;
    }

    pr_debug("testfs module loaded\n");
    return register_filesystem(&ramfs_fs_type);
}
// fs_initcall(init_ramfs_fs);
static void __exit testfs_fini(void)
{
    int err = unregister_filesystem(&ramfs_fs_type);
    if (err) {
      pr_err("Unable to unregister testfs\n");
    }
    pr_debug("testfs module unloaded\n");
}

module_init(testfs_init);
module_exit(testfs_fini);
