#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/spinlock.h>
#include <linux/version.h>

#define DRV_NAME   "SdeC_signals"
#define CLASS_NAME "SdeC"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sistemas de Computacion - TP5");
MODULE_DESCRIPTION("CDD que sensa dos senales simuladas (senoidal y cuadrada) cada 1 s");
MODULE_VERSION("1.0");


static int sq_period = 10;          
module_param(sq_period, int, 0644);
MODULE_PARM_DESC(sq_period, "Periodo de la senal cuadrada en segundos (>=2)");


#define SINE_LEN 24
static const int sine_table[SINE_LEN] = {
	    0,  259,  500,  707,  866,  966, 1000,  966,  866,  707,  500,  259,
	    0, -259, -500, -707, -866, -966, -1000, -966, -866, -707, -500, -259
};

static dev_t           dev_num;     
static struct cdev     my_cdev;
static struct class   *my_class;
static struct device  *my_device;

static struct timer_list sample_timer;
static DEFINE_SPINLOCK(state_lock); 

static u64 t_seconds;               
static int signal1_raw;             
static int signal2_raw;             
static int selected = 1;            

static void sample_cb(struct timer_list *t)
{
	unsigned long flags;
	int period = (sq_period < 2) ? 2 : sq_period;

	spin_lock_irqsave(&state_lock, flags);

	signal1_raw = sine_table[t_seconds % SINE_LEN];

	signal2_raw = ((t_seconds % period) < (period / 2)) ? 1000 : 0;

	t_seconds++;
	spin_unlock_irqrestore(&state_lock, flags);

	mod_timer(&sample_timer, jiffies + HZ);
}

static int my_open(struct inode *inode, struct file *file)
{
	pr_info("%s: open()\n", DRV_NAME);
	return 0;
}

static int my_release(struct inode *inode, struct file *file)
{
	pr_info("%s: release()\n", DRV_NAME);
	return 0;
}

static ssize_t my_read(struct file *file, char __user *ubuf,
		       size_t count, loff_t *ppos)
{
	char tmp[64];
	int  len, sig, s1, s2, val;
	u64  t;
	unsigned long flags;

	spin_lock_irqsave(&state_lock, flags);
	sig = selected;
	s1  = signal1_raw;
	s2  = signal2_raw;
	t   = t_seconds;
	spin_unlock_irqrestore(&state_lock, flags);

	val = (sig == 2) ? s2 : s1;

	len = snprintf(tmp, sizeof(tmp), "%d %llu %d\n",
		       sig, (unsigned long long)t, val);

	return simple_read_from_buffer(ubuf, count, ppos, tmp, len);
}

static ssize_t my_write(struct file *file, const char __user *ubuf,
			size_t count, loff_t *ppos)
{
	char kbuf[16];
	size_t n = min(count, (size_t)(sizeof(kbuf) - 1));
	unsigned long flags;

	if (copy_from_user(kbuf, ubuf, n))
		return -EFAULT;
	kbuf[n] = '\0';

	if (kbuf[0] == '1' || kbuf[0] == '2') {
		spin_lock_irqsave(&state_lock, flags);
		selected = (kbuf[0] == '2') ? 2 : 1;
		spin_unlock_irqrestore(&state_lock, flags);
		pr_info("%s: senal seleccionada = %c\n", DRV_NAME, kbuf[0]);
	} else {
		pr_warn("%s: comando invalido (use '1' o '2')\n", DRV_NAME);
	}

	return count;
}

static const struct file_operations fops = {
	.owner   = THIS_MODULE,
	.open    = my_open,
	.release = my_release,
	.read    = my_read,
	.write   = my_write,
};

static int __init sdec_init(void)
{
	int ret;

	ret = alloc_chrdev_region(&dev_num, 0, 1, DRV_NAME);
	if (ret < 0) {
		pr_err("%s: alloc_chrdev_region fallo (%d)\n", DRV_NAME, ret);
		return ret;
	}

	cdev_init(&my_cdev, &fops);
	my_cdev.owner = THIS_MODULE;
	ret = cdev_add(&my_cdev, dev_num, 1);
	if (ret < 0) {
		pr_err("%s: cdev_add fallo (%d)\n", DRV_NAME, ret);
		goto err_cdev;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	my_class = class_create(CLASS_NAME);
#else
	my_class = class_create(THIS_MODULE, CLASS_NAME);
#endif
	if (IS_ERR(my_class)) {
		ret = PTR_ERR(my_class);
		goto err_class;
	}

	my_device = device_create(my_class, NULL, dev_num, NULL, DRV_NAME);
	if (IS_ERR(my_device)) {
		ret = PTR_ERR(my_device);
		goto err_device;
	}

	timer_setup(&sample_timer, sample_cb, 0);
	mod_timer(&sample_timer, jiffies + HZ);

	pr_info("%s: cargado. major=%d minor=%d -> /dev/%s\n",
		DRV_NAME, MAJOR(dev_num), MINOR(dev_num), DRV_NAME);
	return 0;

err_device:
	class_destroy(my_class);
err_class:
	cdev_del(&my_cdev);
err_cdev:
	unregister_chrdev_region(dev_num, 1);
	return ret;
}

static void __exit sdec_exit(void)
{
	del_timer_sync(&sample_timer);
	device_destroy(my_class, dev_num);
	class_destroy(my_class);
	cdev_del(&my_cdev);
	unregister_chrdev_region(dev_num, 1);
	pr_info("%s: descargado\n", DRV_NAME);
}

module_init(sdec_init);
module_exit(sdec_exit);
