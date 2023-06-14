/****************************************************************
 ****************************************************************
 **                                                            **
 **    (C)Copyright 2009-2015, American Megatrends Inc.        **
 **                                                            **
 **            All Rights Reserved.                            **
 **                                                            **
 **        5555 Oakbrook Pkwy Suite 200, Norcross              **
 **                                                            **
 **        Georgia - 30093, USA. Phone-(770)-246-8600.         **
 **                                                            **
 ****************************************************************
 ****************************************************************/

/****************************************************************
 *
 * watchdog_core.c
 * common module for watchdog timer driver
 *
 ****************************************************************/

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>
#include "watchdog_core.h"
#include "helper.h"
#include "driver_hal.h"

#define WATCHDOG_DRIVER_NAME "watchdog"
#define WATCHDOG_DEFAULT_TIMEOUT 900 /* in seconds */
#define MAX_INSTANCES            3

#ifdef HAVE_UNLOCKED_IOCTL  
  #if HAVE_UNLOCKED_IOCTL  
	#define USE_UNLOCKED_IOCTL  
  #endif  
#endif 

//for old misic device of watchdog
#define EDEV_TYPE_WATCHDOG_NEW 0x100

static int watchdog_timeout = WATCHDOG_DEFAULT_TIMEOUT;
static int watchdog_timeout_new[MAX_INSTANCES] = {WATCHDOG_DEFAULT_TIMEOUT, 
                                              WATCHDOG_DEFAULT_TIMEOUT, 
                                              WATCHDOG_DEFAULT_TIMEOUT};

#ifdef CONFIG_WATCHDOG_NOWAYOUT
static int nowayout = 1;
#else
static int nowayout = 0;
#endif

static unsigned long watchdog_is_open;
static unsigned long watchdog_is_open_new[MAX_INSTANCES];
static struct watchdog_info watchdog_info;
static char watchdog_expect_close;
static char watchdog_expect_close_new[MAX_INSTANCES];

static struct watchdog_core_funcs_t watchdog_core_funcs;
static struct watchdog_hal_t *watchdog_hal;
static struct watchdog_hal_new_t *watchdog_hal_new;
static struct proc_dir_entry *wd_dir = NULL;
static struct cdev *wdt_cdev;
static dev_t wdt_devt;
static struct class *wdt_class;

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,4,11))
static struct proc_info  *wdt_proc = NULL;
static struct proc_info  *wdt_proc_new[MAX_INSTANCES] = {NULL, NULL, NULL};
#endif

static char banner[] __initdata = KERN_INFO "Watchdog Common Driver, (c) 2010 American Megatrends Inc.\n";	

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,4,11))
int watchdog_timer_read(struct file *file,  char __user *buf, size_t count, loff_t *offset)
#else
int watchdog_timer_read( char *buf, char **start, off_t offset, int count, int *eof, void *data )
#endif
{
	
	 int len=0;
#ifdef CONFIG_SPX_FEATURE_HW_FAILSAFE_BOOT 
	int len_status=0; 
	char watchdog_status[256] = {0}; 
#endif 

/* We don't support seeked reads. read should start from 0 */	
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,4,11))
	if (*offset != 0)
		return 0;
#else
	*eof=1;
	if (offset != 0)
		return 0;
#endif
	
#if (LINUX_VERSION_CODE <  KERNEL_VERSION(3,4,11))
	/* This is a requirement. So don't change it */
	*start = buf;
#endif

	 len = snprintf(buf, 12, "%d\n", watchdog_timeout);
#ifdef CONFIG_SPX_FEATURE_HW_FAILSAFE_BOOT 
	if (watchdog_hal->hal_ops->get_status) 
	{	 
		len_status = snprintf(watchdog_status, sizeof(watchdog_status), "%d\n", watchdog_hal->hal_ops->get_status()); 
		len += snprintf(&buf[len], (len + len_status), "%s", watchdog_status); 
	} 
#endif 



#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,4,11))
	 *offset +=len;
#endif
	 
	 return len;
}

#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,4,11))
int watchdog_timer_read_new(struct file *file,  char __user *buf, size_t count, loff_t *offset)
#else
int watchdog_timer_read_new( char *buf, char **start, off_t offset, int count, int *eof, void *data )
#endif
{
	
	int len=0;
#ifdef CONFIG_SPX_FEATURE_HW_FAILSAFE_BOOT 
	int len_status=0; 
	char watchdog_status[256] = {0}; 
#endif 
    int cnt = 0;

/* We don't support seeked reads. read should start from 0 */	
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,4,11))
	if (*offset != 0)
		return 0;
#else
	*eof=1;
	if (offset != 0)
		return 0;
#endif
	
#if (LINUX_VERSION_CODE <  KERNEL_VERSION(3,4,11))
	/* This is a requirement. So don't change it */
	*start = buf;
#endif

        for (cnt = 0; cnt < MAX_INSTANCES; cnt++) {
            unsigned char entry[16] = {0};
            
            snprintf(entry, sizeof(entry), "TimerValue%d", cnt);
            if (strcmp(file->f_path.dentry->d_iname, entry) == 0) {
                len = snprintf(buf, 12, "%d\n", watchdog_timeout_new[cnt]);
#ifdef CONFIG_SPX_FEATURE_HW_FAILSAFE_BOOT
                if (watchdog_hal_new->hal_ops->get_status) {
                    len_status = snprintf(watchdog_status, sizeof(watchdog_status), "%d\n", watchdog_hal_new->hal_ops->get_status(cnt));
                    len += snprintf(&buf[len], (len + len_status), "%s", watchdog_status);
                }
#endif
                break;
            }
        }


#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,4,11))
	 *offset +=len;
#endif
	 
	 return len;
}

int register_watchdog_hal_module(unsigned char num_instances, void *hal_ops, void **hw_data)
{
	if (!hal_ops)
		return -ENOSYS;

	watchdog_hal = (struct watchdog_hal_t *) kmalloc(sizeof(struct watchdog_hal_t), GFP_KERNEL);
	if (!watchdog_hal) {
		return -ENOMEM;
	}

	watchdog_hal->hal_ops = (struct watchdog_hal_ops_t *) hal_ops;
	*hw_data = (void *) watchdog_hal;
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,4,11))
	wdt_proc = 
#endif
	AddProcEntry (wd_dir, "TimerValue", watchdog_timer_read, NULL, NULL);

	return 0;
}
int register_watchdog_hal_module_new(unsigned char num_instances, void *hal_ops, void **hw_data)
{
    int cnt = 0;

	if (!hal_ops)
		return -ENOSYS;

	watchdog_hal_new = (struct watchdog_hal_new_t *) kmalloc(sizeof(struct watchdog_hal_new_t), GFP_KERNEL);
	if (!watchdog_hal_new) {
		return -ENOMEM;
	}

	watchdog_hal_new->hal_ops = (struct watchdog_hal_ops_new_t *) hal_ops;
	*hw_data = (void *) watchdog_hal_new;

    for (cnt = 0; cnt < num_instances; cnt++) {
        unsigned char entry[16] = {0};

        snprintf(entry, sizeof(entry), "TimerValue%d", cnt);
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,4,11))
        wdt_proc_new[cnt] =
#endif
        AddProcEntry (wd_dir, entry, watchdog_timer_read_new, NULL, NULL);
    }

	return 0;
}

int unregister_watchdog_hal_module(void *hw_data)
{
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,4,11))
	RemoveProcEntry(wdt_proc);
#else
	RemoveProcEntry(wd_dir, "TimerValue");
#endif

	if (watchdog_hal != NULL)
		kfree(watchdog_hal);
    return 0;
}
int unregister_watchdog_hal_module_new(void *hw_data)
{
    int cnt = 0 ;

    for (cnt = 0; cnt < MAX_INSTANCES; cnt++) {
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,4,11))
        if (wdt_proc_new[cnt])
        	RemoveProcEntry(wdt_proc_new[cnt]);
#else
        unsigned char entry[16] = {0};

        snprintf(entry, sizeof(entry), "TimerValue%d", cnt);
        RemoveProcEntry(wd_dir, entry);
#endif
    }

	if (watchdog_hal_new != NULL)
		kfree(watchdog_hal_new);

    return 0;
}

static core_hal_t watchdog_core_hal = {
	.owner = THIS_MODULE,
	.name = "WATCHDOG CORE",
	.dev_type = EDEV_TYPE_WATCHDOG,
	.register_hal_module = register_watchdog_hal_module,
	.unregister_hal_module = unregister_watchdog_hal_module,
	.pcore_funcs = (void *) (&watchdog_core_funcs)
};
static core_hal_t watchdog_core_hal_new = {
	.owner = THIS_MODULE,
	.name = "WATCHDOG CORE",
	.dev_type = EDEV_TYPE_WATCHDOG_NEW,
	.register_hal_module = register_watchdog_hal_module_new,
	.unregister_hal_module = unregister_watchdog_hal_module_new,
	.pcore_funcs = (void *) (&watchdog_core_funcs)
};

static struct watchdog_info watchdog_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
	.firmware_version = 0,
	.identity = WATCHDOG_DRIVER_NAME,
};

static ssize_t watchdog_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	size_t i;
	char c;

	if (!watchdog_hal)
		return -ENOSYS;

	if (count) {
		watchdog_expect_close = 0;
		for (i = 0; i < count; i ++) {
			if (get_user(c, buf + i))
				return -EFAULT;
			if (c == 'V') /* special character, userspace daemon would like to disable watchdog */
				watchdog_expect_close = 1;
		}
		watchdog_hal->hal_ops->count(); /* pat the watchdog */
	}
	return count;
}
static ssize_t watchdog_write_new(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	size_t i;
	char c;
    int dev_instance = iminor(file->f_path.dentry->d_inode);

	if (!watchdog_hal_new) {
		return -ENOSYS;
    }

    if ((dev_instance < 0) || (dev_instance >= MAX_INSTANCES)) {
        return -EFAULT;
    }

	if (count) {
		watchdog_expect_close_new[dev_instance] = 0;
		for (i = 0; i < count; i ++) {
			if (get_user(c, buf + i))
				return -EFAULT;
			if (c == 'V') /* special character, userspace daemon would like to disable watchdog */
				watchdog_expect_close_new[dev_instance] = 1;
		}
		watchdog_hal_new->hal_ops->count(dev_instance); /* pat the watchdog */
	}
	return count;
}

static int watchdog_open(struct inode *index_node, struct file *file)
{
	if (!watchdog_hal)
		return -ENOSYS;

	if (test_and_set_bit(0, &watchdog_is_open)) /* we have been opened */
		return -EBUSY;

	watchdog_hal->hal_ops->enable();
	watchdog_hal->hal_ops->set_value(watchdog_timeout);
	watchdog_hal->hal_ops->count();
	return 0;
}
static int watchdog_open_new(struct inode *index_node, struct file *file)
{
    int dev_instance = iminor(file->f_path.dentry->d_inode);
    
	if (!watchdog_hal_new) {
		return -ENOSYS;
    }

    if ((dev_instance < 0) || (dev_instance >= MAX_INSTANCES)) {
        return -EFAULT;
    }

	if (test_and_set_bit(0, &watchdog_is_open_new[dev_instance]) == 1) { /* we have been opened */
		return -EBUSY;
    }

	watchdog_hal_new->hal_ops->enable(dev_instance);
	watchdog_hal_new->hal_ops->set_value(watchdog_timeout_new[dev_instance], dev_instance);
	watchdog_hal_new->hal_ops->count(dev_instance);
	return 0;
}

static int watchdog_close(struct inode *inode, struct file *file)
{
	if (!watchdog_hal)
		return -ENOSYS;

	if (!nowayout && (watchdog_expect_close == 1)) {
		watchdog_hal->hal_ops->disable();
	} else {
		printk(KERN_CRIT WATCHDOG_DRIVER_NAME ": Unexpected close, not stopping watchdog!\n");
	}

	clear_bit(0, &watchdog_is_open);
	watchdog_expect_close = 0;
	return 0;
}
static int watchdog_close_new(struct inode *inode, struct file *file)
{
    int dev_instance = iminor(file->f_path.dentry->d_inode);

	if (!watchdog_hal_new) {
		return -ENOSYS;
    }

    if ((dev_instance < 0) || (dev_instance >= MAX_INSTANCES)) {
        return -EFAULT;
    }

	if (!nowayout && (watchdog_expect_close_new[dev_instance] == 1)) {
		watchdog_hal_new->hal_ops->disable(dev_instance);
	} else {
		printk(KERN_CRIT WATCHDOG_DRIVER_NAME ": Unexpected close, not stopping watchdog!\n");
	}

	clear_bit(0, &watchdog_is_open_new[dev_instance]);
	watchdog_expect_close_new[dev_instance] = 0;
	return 0;
}

static long watchdog_ioctlUnlocked(struct file *file, unsigned int cmd, unsigned long arg)
{
	int new_timeout;
	int options;
	int retval = -EINVAL;

	if (!watchdog_hal)
		return -ENOSYS;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user((struct watchdog_info *)arg, &watchdog_info, sizeof(watchdog_info)))
			return -EFAULT;
		return 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, (int *) arg);

	case WDIOC_KEEPALIVE:
		watchdog_hal->hal_ops->count(); /* pat the watchdog */
		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_timeout, (int *) arg))
			return -EFAULT;
		if (new_timeout < 3 || new_timeout > 3600) /* arbitrary upper limit */
			return -EINVAL;

		watchdog_timeout = new_timeout;
		watchdog_hal->hal_ops->set_value(watchdog_timeout);
		watchdog_hal->hal_ops->count();

		return put_user(watchdog_timeout, (int *) arg);

	case WDIOC_GETTIMEOUT:
		return put_user(watchdog_timeout, (int *) arg);

	case WDIOC_SETOPTIONS:
		if (get_user(options, (int *)arg))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD) {
			watchdog_hal->hal_ops->disable();
			retval = 0;
		}

		if (options & WDIOS_ENABLECARD) {
			watchdog_hal->hal_ops->enable();
			retval = 0;
		}
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return retval;
}
static long watchdog_ioctlUnlocked_new(struct file *file, unsigned int cmd, unsigned long arg)
{
	int new_timeout;
	int options;
	int retval = -EINVAL;
    int dev_instance = iminor(file->f_path.dentry->d_inode);

	if (!watchdog_hal_new) {
		return -ENOSYS;
    }

    if ((dev_instance < 0) || (dev_instance >= MAX_INSTANCES)) {
        return -EFAULT;
    }

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		if (copy_to_user((struct watchdog_info *)arg, &watchdog_info, sizeof(watchdog_info)))
			return -EFAULT;
		return 0;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		return put_user(0, (int *) arg);

	case WDIOC_KEEPALIVE:
		watchdog_hal_new->hal_ops->count(dev_instance); /* pat the watchdog */
		return 0;

	case WDIOC_SETTIMEOUT:
		if (get_user(new_timeout, (int *) arg))
			return -EFAULT;
		if (new_timeout < 3 || new_timeout > 3600) /* arbitrary upper limit */
			return -EINVAL;

		watchdog_timeout_new[dev_instance] = new_timeout;
		watchdog_hal_new->hal_ops->set_value(watchdog_timeout_new[dev_instance], dev_instance);
		watchdog_hal_new->hal_ops->count(dev_instance);

		return put_user(watchdog_timeout_new[dev_instance], (int *) arg);

	case WDIOC_GETTIMEOUT:
		return put_user(watchdog_timeout_new[dev_instance], (int *) arg);

	case WDIOC_SETOPTIONS:
		if (get_user(options, (int *)arg))
			return -EFAULT;

		if (options & WDIOS_DISABLECARD) {
			watchdog_hal_new->hal_ops->disable(dev_instance);
			retval = 0;
		}

		if (options & WDIOS_ENABLECARD) {
			watchdog_hal_new->hal_ops->enable(dev_instance);
			retval = 0;
		}
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return retval;
}
#ifndef USE_UNLOCKED_IOCTL   
static int       
watchdog_ioctl(struct inode * inode, struct file * file, unsigned int cmd,unsigned long arg)  
        return watchdog_ioctlUnlocked(file,cmd,arg);  
} 
static int       
watchdog_ioctl_new(struct inode * inode, struct file * file, unsigned int cmd,unsigned long arg)  
        return watchdog_ioctlUnlocked_new(file,cmd,arg);  
}  
#endif  

static struct file_operations watchdog_fops = {
	.owner = THIS_MODULE,
	.write = watchdog_write,
#ifdef USE_UNLOCKED_IOCTL 
	.unlocked_ioctl = watchdog_ioctlUnlocked, 
#else 
	.ioctl = watchdog_ioctl,
#endif
	.open = watchdog_open,
	.release = watchdog_close,
};
static struct file_operations watchdog_fops_new = {
	.owner = THIS_MODULE,
	.write = watchdog_write_new,
#ifdef USE_UNLOCKED_IOCTL 
	.unlocked_ioctl = watchdog_ioctlUnlocked_new, 
#else 
	.ioctl = watchdog_ioctl_new,
#endif
	.open = watchdog_open_new,
	.release = watchdog_close_new,
};

static struct miscdevice watchdog_miscdev = {
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &watchdog_fops,
};

static int __init watchdog_init(void)
{
	int ret;
    int cnt = 0;
    struct device *dev;

	printk(banner);

	ret = register_core_hal_module (&watchdog_core_hal);
	if (ret < 0) {
		printk(KERN_ERR "failed to register the common watchdog module(old)\n");
		return -EINVAL;
	}

	ret = register_core_hal_module (&watchdog_core_hal_new);
	if (ret < 0) {
		printk(KERN_ERR "failed to register the common watchdog module\n");
		return -EINVAL;
	}

    //It will remove, and change to use device node, like "/dev/watchdogN", N is number of 0 ~ N
	ret = misc_register(&watchdog_miscdev);
	if (ret) {
		printk(KERN_ERR WATCHDOG_DRIVER_NAME ": Can't register misc device\n");
		return ret;
	}

    ret = alloc_chrdev_region(&wdt_devt, 0, MAX_INSTANCES, WATCHDOG_DRIVER_NAME);
    if (ret < 0) {
        printk (KERN_ERR "failed to allocate char dev region\n");
        return ret;
    }

    wdt_cdev = cdev_alloc ();
    if (!wdt_cdev) {
        unregister_chrdev_region (wdt_devt, MAX_INSTANCES);
        printk (KERN_ERR "%s: failed to allocate wdt cdev structure\n", WATCHDOG_DRIVER_NAME);
        return -1;
    }

    cdev_init (wdt_cdev, &watchdog_fops_new);
    wdt_cdev->owner = THIS_MODULE;

    if ((ret = cdev_add (wdt_cdev, wdt_devt, MAX_INSTANCES)) < 0) {
        printk  (KERN_ERR "failed to add <%s> char device\n", WATCHDOG_DRIVER_NAME);
        ret = -ENODEV;
        goto error;
    }

    wdt_class = class_create(THIS_MODULE, WATCHDOG_DRIVER_NAME);
    if (IS_ERR(wdt_class)) {
        ret = PTR_ERR(wdt_class);
        goto error;
    }

    for (cnt = 0; cnt < MAX_INSTANCES; cnt++) {
        dev = device_create(wdt_class, NULL, MKDEV(MAJOR(wdt_devt), cnt), NULL, "watchdog%d", cnt);
        if (IS_ERR(dev)) {
            ret = PTR_ERR(dev);
            goto error;
        }
    }
    
	wd_dir = proc_mkdir("watchdog", rootdir);
	return ret;

error:
    for (cnt = 0; cnt < MAX_INSTANCES; cnt++) {
        device_destroy(wdt_class, MKDEV(MAJOR(wdt_devt), cnt));
    }

    class_destroy(wdt_class);
    cdev_del (wdt_cdev);
    unregister_chrdev_region (wdt_devt, MAX_INSTANCES);
    
    return ret; 
}

static void __exit watchdog_exit(void)
{
    int cnt = 0;

	unregister_core_hal_module(EDEV_TYPE_WATCHDOG);
	unregister_core_hal_module(EDEV_TYPE_WATCHDOG_NEW);
	remove_proc_entry("watchdog", rootdir);
    
    //It wiil remove
	misc_deregister(&watchdog_miscdev);

    for (cnt = 0; cnt < MAX_INSTANCES; cnt++) {
        device_destroy(wdt_class, MKDEV(MAJOR(wdt_devt), cnt));
    }

    class_destroy(wdt_class);
    cdev_del (wdt_cdev);
    unregister_chrdev_region (wdt_devt, MAX_INSTANCES);

}

module_init(watchdog_init);
module_exit(watchdog_exit);

module_param(watchdog_timeout, int, 0);
MODULE_PARM_DESC(watchdog_timeout, "Watchdog timeout in seconds, default=" __MODULE_STRING(WATCHDOG_DEFAULT_TIMEOUT) ")");

module_param(nowayout, int, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");

MODULE_AUTHOR("American Megatrends Inc.");
MODULE_DESCRIPTION("common module for watchdog timer driver");
MODULE_LICENSE("GPL");

int watchdog_core_loaded =1;
EXPORT_SYMBOL(watchdog_core_loaded);
