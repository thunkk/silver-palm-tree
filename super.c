
#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>

#define GIFFS_MAGIC_NUMBER 123456789

MODULE_LICENSE("GPL");
MODULE_AUTHOR("ss");

static void giffs_put_super(struct super_block *sb)
{
    pr_debug("giffs super block destroyed\n");
}

static struct super_operations const giffs_super_ops = {
    .put_super = giffs_put_super,
};

static int giffs_fill_sb(struct super_block *sb, void *data, int silent)
{
    struct inode *root = NULL;

    sb->s_magic = GIFFS_MAGIC_NUMBER;
    sb->s_op = &giffs_super_ops;

    root = new_inode(sb);
    if (!root)
    {
         pr_err("inode allocation failed\n");
         return -ENOMEM;
    }

    root->i_ino = 0;
    root->i_sb = sb;
    root->i_atime = root->i_mtime = root->i_ctime = CURRENT_TIME;
    inode_init_owner(root, NULL, S_IFDIR);

    sb->s_root = d_make_root(root);
    if (!sb->s_root)
    {
        pr_err("root creation failed\n");
        return -ENOMEM;
    }

   return 0;
}

static struct dentry *giffs_mount(struct file_system_type *type, int flags,
                                  char const *dev, void *data)
{
    struct dentry *const entry = mount_nodev(type, flags,
                                             data, giffs_fill_sb);
    if (IS_ERR(entry))
        pr_err("giffs mounting failed\n");
    else
        pr_debug("giffs mounted\n");
    return entry;
}

static void giffs_kill_sb(struct super_block *sb) {
    pr_debug("killing giffs super block");
    kill_litter_super(sb);
    pr_debug("killed giffs super block");
}

static struct file_system_type giffs_type = {
    .owner = THIS_MODULE,
    .name = "giffs",
    .mount = giffs_mount,
    .kill_sb = giffs_kill_sb,
    .fs_flags = 0,
};

static int __init giffs_init(void)
{
    int err = register_filesystem(&giffs_type);
    if (err) {
      pr_err("Unable to register giffs\n");
      return err;
    }
    pr_debug("giffs module loaded\n");
    return 0;
}

static void __exit giffs_fini(void)
{
    int err = unregister_filesystem(&giffs_type);
    if (err) {
      pr_err("Unable to unregister giffs\n");
    }
    pr_debug("giffs module unloaded\n");
}

module_init(giffs_init);
module_exit(giffs_fini);
