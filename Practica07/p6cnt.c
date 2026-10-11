#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/atomic.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/uaccess.h>

static atomic_long_t aperturas = ATOMIC_LONG_INIT(0);

static int p6c_open(struct inode *inode, struct file *f)
{
	atomic_long_inc(&aperturas);
	return 0;
}

static ssize_t p6c_read(struct file *f, char __user *dst, size_t n, loff_t *pos)
{
	char tmp[32];
	int len;

	len = scnprintf(tmp, sizeof(tmp), "aperturas: %ld\n",
			atomic_long_read(&aperturas));
	return simple_read_from_buffer(dst, n, pos, tmp, len);
}

static ssize_t p6c_write(struct file *f, const char __user *src, size_t n,
			 loff_t *pos)
{
	char cmd[16];

	if (n >= sizeof(cmd))
		return -EINVAL;
	if (copy_from_user(cmd, src, n))
		return -EFAULT;
	cmd[n] = '\0';
	if (n > 0 && cmd[n - 1] == '\n')
		cmd[n - 1] = '\0';

	if (strcmp(cmd, "reset") != 0) {
		pr_warn("comando no valido, solo se acepta \"reset\"\n");
		return -EINVAL;
	}
	atomic_long_set(&aperturas, 0);
	pr_info("contador reiniciado\n");
	return n;
}

static const struct file_operations p6c_fops = {
	.owner = THIS_MODULE,
	.open = p6c_open,
	.read = p6c_read,
	.write = p6c_write,
	.llseek = default_llseek,
};

static struct miscdevice p6c_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "p6cnt",
	.fops = &p6c_fops,
	.mode = 0666,
};

static int __init p6c_init(void)
{
	int rc = misc_register(&p6c_dev);

	if (rc) {
		pr_err("misc_register fallo: %d\n", rc);
		return rc;
	}
	pr_info("/dev/p6cnt creado, minor %d\n", p6c_dev.minor);
	return 0;
}

static void __exit p6c_exit(void)
{
	misc_deregister(&p6c_dev);
	pr_info("/dev/p6cnt eliminado, total de aperturas: %ld\n",
		atomic_long_read(&aperturas));
}

module_init(p6c_init);
module_exit(p6c_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tu nombre");
MODULE_DESCRIPTION("Practica 6 FSE: contador de aperturas");
