#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <mach/platform.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,17))
#include <linux/poll.h>
#endif

#define I2C_MUX_AST_NAME "i2c_ast_mux"


typedef enum {
	ast_mux = 0,
	ast_mux_unsuported = 0xFF
}eMuxType;

#define I2C_MUX_AST_MAX_CHAN   4  

typedef struct 
{
	int status;
	eMuxType type;
	u8 bus;
	u8 avaiableChannels;
	u8 chanUsed;
	u8 dummy_1;
	char test[32];
	u8 vBusNum[I2C_MUX_AST_MAX_CHAN];	
	struct i2c_adapter *virtAdaps[I2C_MUX_AST_MAX_CHAN];
	wait_queue_head_t chanDeselectWait;
	int doChanDeselect;
	int deselectTo[I2C_MUX_AST_MAX_CHAN];
	#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,17))
        void *muxc;
    #endif

}tCtlMuxData;


typedef struct {
	u8 type;
	u8 maxChn;
	u8 sizeOfName;
	u8 dummy;
	char name[12];
    #if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,17))
	int (*select_chan)   (struct i2c_mux_core *muxc, u32 chan);
	int (*deselect_chan) (struct i2c_mux_core *muxc, u32 chan);
	#else
	int (*select_chan)   (struct i2c_adapter *, void *mux_dev, u32 chan_id);
	int (*deselect_chan) (struct i2c_adapter *, void *mux_dev, u32 chan_id);
	#endif
}tMuxProp;

const static u8 io_offset[10] = {0xE0,0x00,0x00,0x00, 0xF4,0xE4,0xE8,0xEC,0x00, 0xF0};
#define I2C_TOP_LEVEL_CTL_BASE	NULL 
#define I2C_TOP_LEVEL_CTL_BASE_SECOND NULL

static int isI2cSlaveActive(int bus)
{
	return 0;
}

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,17))
static int ast_deselect_chan(struct i2c_adapter *adap, void *client, u32 chan)
{
	u32 regval = 0;
	tCtlMuxData *pCtlMuxData = NULL;
	struct platform_device *pdev = client;
	
	if(pdev != NULL) {
		pCtlMuxData = pdev->dev.platform_data;
		if(pCtlMuxData != NULL) {
			if (pCtlMuxData->deselectTo[chan] != 0) {
				int retry = 2;

				do {
					wait_event_timeout(pCtlMuxData->chanDeselectWait, pCtlMuxData->doChanDeselect, (msecs_to_jiffies(pCtlMuxData->deselectTo[chan])));
				} while (isI2cSlaveActive(pCtlMuxData->bus) && (retry-- > 0));
			}
			//printk(KERN_ERR "ast_deselect_chan: %s\n",pCtlMuxData->test);
			*(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE + io_offset[pCtlMuxData->bus]) = regval;
			*(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE_SECOND + io_offset[pCtlMuxData->bus]) = regval;
			//printk(KERN_ERR "ast_deselect_chan: reg reads 0x%X", *(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE_SECOND + io_offset[pCtlMuxData->bus]));
                        return 0;
		}
	}
	return -1;
}

static int ast_select_chan(struct i2c_adapter *adap, void *client, u32 chan)
{
	u32 regval = 0;
	u8 num = chan;
	tCtlMuxData *pCtlMuxData = NULL;
	struct platform_device *pdev = client;
	
	if(pdev != NULL) {
		pCtlMuxData = pdev->dev.platform_data;
		if(pCtlMuxData != NULL) {
			//printk(KERN_ERR "ast_select_chan: (%d) %s\n",chan, pCtlMuxData->test);
			num &= 3;
			regval = 1<<num;
			regval |= ((num +1) << 4);
			*(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE + io_offset[pCtlMuxData->bus]) = regval;
			*(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE_SECOND + io_offset[pCtlMuxData->bus]) = regval;
			//printk(KERN_ERR "ast_select_chan: reg reads 0x%X", *(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE_SECOND + io_offset[pCtlMuxData->bus]));
            pCtlMuxData->doChanDeselect = 0;
            return 0;
		}
	}
	return -1;
}

static int ast_select_chan2(struct i2c_adapter *adap, void *client, u32 chan)
{
	u32 regval = 0;
	tCtlMuxData *pCtlMuxData = NULL;
	struct platform_device *pdev = client;
	
	if(pdev != NULL) {
		pCtlMuxData = pdev->dev.platform_data;
		if(pCtlMuxData != NULL) {
			//printk(KERN_ERR "ast_select_chan2: (%d) %s\n",chan, pCtlMuxData->test);
			if (chan > 0) {
				u8 num = (chan - 1) & 3;
				regval = 1 << num;
				regval |= ((num +1) << 4);
			}
			*(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE + io_offset[pCtlMuxData->bus]) = regval;
			*(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE_SECOND + io_offset[pCtlMuxData->bus]) = regval;
			//printk(KERN_ERR "ast_select_chan2: reg reads 0x%X", *(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE_SECOND + io_offset[pCtlMuxData->bus]));
			pCtlMuxData->doChanDeselect = 0;
			return 0;
		}
	}
	return -1;
}


#else
static int astk5_deselect_chan(struct i2c_mux_core *muxc, u32 chan)
{
	u32 regval = 0;
	tCtlMuxData *pCtlMuxData = NULL;

	if(muxc!=NULL)
	{	
          pCtlMuxData=(tCtlMuxData*)muxc->priv;
	  if(pCtlMuxData!=NULL)
	  {
		if (pCtlMuxData->deselectTo[chan] != 0) {
			int retry = 2;

			do {
				wait_event_timeout(pCtlMuxData->chanDeselectWait, pCtlMuxData->doChanDeselect, (msecs_to_jiffies(pCtlMuxData->deselectTo[chan])));
			} while (isI2cSlaveActive(pCtlMuxData->bus) && (retry-- > 0));
		}
		
		//printk(KERN_ERR "astk5_deselect_chan: %s\n",pCtlMuxData->test);
		*(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE + io_offset[pCtlMuxData->bus]) = regval;
		*(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE_SECOND + io_offset[pCtlMuxData->bus]) = regval;
		//printk(KERN_ERR "astk5_deselect_chan: reg reads 0x%X", *(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE_SECOND + io_offset[pCtlMuxData->bus]));
		return 0;
	  }
	
        }

	return -1;
}

static int astk5_select_chan(struct i2c_mux_core *muxc, u32 chan)
{
	u32 regval = 0;
        u8 num = chan;
	tCtlMuxData *pCtlMuxData = NULL;
	muxc=muxc;
	chan=chan;
	if(muxc!=NULL)
	{	
          pCtlMuxData=(tCtlMuxData*)muxc->priv;
	  if(pCtlMuxData!=NULL)
	  {
              //printk(KERN_ERR "astk5_select_chan: (%d) %s\n",chan, pCtlMuxData->test);
              num &= 3;
              regval = 1<<num;
              regval |= ((num +1) << 4);
              *(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE + io_offset[pCtlMuxData->bus]) = regval;
              *(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE_SECOND + io_offset[pCtlMuxData->bus]) = regval;
              //printk(KERN_ERR "ast_select_chan: reg reads 0x%X", *(unsigned int *)(I2C_TOP_LEVEL_CTL_BASE_SECOND + io_offset[pCtlMuxData->bus]));
              pCtlMuxData->doChanDeselect = 0;
	      return 0;
	  }
	
        }

	return -1;
}

#endif

static tMuxProp muxProp[] =
{
	#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,17))
	{ ast_mux, I2C_MUX_AST_MAX_CHAN-1, (u8)(sizeof("ast_mux")),0, "ast_mux", astk5_select_chan, astk5_deselect_chan},
	#else
	{ ast_mux, I2C_MUX_AST_MAX_CHAN-1, (u8)(sizeof("ast_mux")),0, "ast_mux", ast_select_chan, ast_deselect_chan},
	#endif
};

eMuxType ast_getType(char *name)
{
	int loop = 0;

	//printk(KERN_ERR "ast_getType: search for: %s\n",name);
	for (loop = 0; loop < (sizeof(muxProp)/sizeof(tMuxProp)); ++loop) {
		if( 0 == strncmp(name, muxProp[loop].name,muxProp[loop].sizeOfName) ) {
			return muxProp[loop].type;
		}
	}
	return ast_mux_unsuported;
}
static char *ast_pDeviceName(eMuxType type)
{
	if(type >= ast_mux_unsuported)
		return 0;
	else
    {
        if(type<(sizeof(muxProp)/sizeof(muxProp[0])))
		    return muxProp[type].name;
    }

    return NULL;
}
static int ast_getMaxChan(eMuxType type)
{
	if(type >= ast_mux_unsuported)
		return 0;
	else
    {
        if(type<(sizeof(muxProp)/sizeof(muxProp[0])))
            return muxProp[type].maxChn;
    }

    return 0;
}

static ssize_t deselect_timeout_read (struct file *filp, char *buf, size_t count, loff_t *f_pos)
{
	char str[64] = { 0 };
	int *deselectTo = (int *)PDE_DATA(file_inode(filp));
	if (deselectTo != NULL) {
		snprintf(str, sizeof(str), "%d", *deselectTo);
		return simple_read_from_buffer(buf, count, f_pos, str, strlen(str));
	}

	return -EFAULT;
}


static ssize_t deselect_timeout_write (struct file *filp, const char *buf, size_t len, loff_t *offp)
{
	int *deselectTo = (int *)PDE_DATA(file_inode(filp));
	int timeout = 0;
	unsigned char localbuf[128];

	if ((len > sizeof(localbuf)) || (copy_from_user(localbuf, buf, len))) {
		return -EFAULT;
	}

	sscanf (localbuf, "%d", &timeout);
	if (deselectTo) *deselectTo = timeout;

	return len;
}

static struct file_operations deselect_timeout_fops = {
	.owner = THIS_MODULE,
	.read  = deselect_timeout_read,	
	.write = deselect_timeout_write,
};

#define I2C_MUX_IF_PROC_DIR		"i2c_mux"

static int create_proc_entry(void *data)
{
	char chanNumStr[64] = { 0 };
	tCtlMuxData *pCtlMuxData = (tCtlMuxData *)data;
	struct proc_dir_entry *i2c_mux_dir = NULL;
	int i = 0;
	
	if (pCtlMuxData != NULL)
	{
		for (i = 0; i < pCtlMuxData->avaiableChannels; i++)
		{
			if (pCtlMuxData->vBusNum[i] != 0)
			{
				snprintf(chanNumStr, sizeof(chanNumStr), "%s/%d", I2C_MUX_IF_PROC_DIR, pCtlMuxData->vBusNum[i]);
				
				if ((i2c_mux_dir = proc_mkdir(chanNumStr, NULL)) == NULL) {
					continue;
				}

				proc_create_data("deselect_timeout", S_IFREG | S_IRUGO | S_IWUGO, i2c_mux_dir, &deselect_timeout_fops, (void *)&(pCtlMuxData->deselectTo[i]));
			}
		}
	}
	
	return 0;
}

void ast_setMaskChip(struct platform_device *pdev, u8 mask)
{
	printk(KERN_ERR "ast_setMaskChip: uppps nothing to do....\n");
}
void ast_resetChip(struct platform_device *pdev)
{
	printk(KERN_ERR "ast_resetChip: uppps nothing to do....\n");
}

int ast_getStatus (struct platform_device *pdev, char *buf)
{
	int len = 0;
	u8 j = 0;
	
	tCtlMuxData *pCtlMuxData = NULL;
	if(pdev != NULL) {
		pCtlMuxData = (tCtlMuxData *)pdev->dev.platform_data;
		if(pCtlMuxData != NULL) {
			len = sprintf(buf,"st: %08X, bus: %02X, addr: %02X, gpio: %d, type: %s",
						pCtlMuxData->status,pCtlMuxData->bus,0,0,muxProp[pCtlMuxData->type].name);
			for(j=0;j<pCtlMuxData->avaiableChannels;++j) {
					len += sprintf(buf+len,",%s%i", ((pCtlMuxData->vBusNum[j] == 0)?"":"i2c-"),pCtlMuxData->vBusNum[j]);
			}
			len += sprintf(buf+len,"\n");
		}
	}
	return len;
	
}

static void platform_device_test_release(struct device *pDev)
{   
   pDev=pDev;
   printk(KERN_ERR "platform_device_test_release: uppps nothing to do....\n");
}

static int deviceCount = 0;

void *ast_addChip(u8 bus, u8 addr, u8 mask, u8 rstGpio, int type, u8 *chanNumbering, u8 maxChanIndex, u16 *modifiers, u8 numModifiers )
{
	int ret = 0;
	tCtlMuxData *pCtlMuxData = NULL;
	struct platform_device *pDevice = NULL;
	u8 k = 0;
	
	if((pDevice = kmalloc(sizeof(struct platform_device), GFP_KERNEL)) == NULL) {
	    return NULL;
	}
	if((pCtlMuxData = kmalloc(sizeof(tCtlMuxData), GFP_KERNEL)) == NULL) {
		kfree(pDevice);
	    return NULL;
	}

	memset(pCtlMuxData, 0, sizeof(tCtlMuxData));

	pCtlMuxData->status = (-1);
	pCtlMuxData->type = type;
	pCtlMuxData->avaiableChannels = maxChanIndex;
	pCtlMuxData->bus = bus;
	init_waitqueue_head(&(pCtlMuxData->chanDeselectWait)); 
	pCtlMuxData->doChanDeselect = 0;

	if( pCtlMuxData->avaiableChannels > ast_getMaxChan(type) )
		pCtlMuxData->avaiableChannels = ast_getMaxChan(type);
	
	for(k = 0; k < pCtlMuxData->avaiableChannels; ++k)
		pCtlMuxData->vBusNum[k] =  chanNumbering[k];
	
	memset(pDevice,0x00,sizeof(struct platform_device));
	pDevice->name = I2C_MUX_AST_NAME;
    pDevice->id = bus;
    pDevice->dev.platform_data = pCtlMuxData;
    pDevice->dev.release=platform_device_test_release;
		
	snprintf(pCtlMuxData->test,sizeof(pCtlMuxData->test),"%i:%i,%i %s",deviceCount,bus,addr,ast_pDeviceName(type));
	
	ret = platform_device_register(pDevice);
	
	if(ret != 0) {
		kfree(pDevice);
		kfree(pCtlMuxData);
		return NULL;
	}
	else {
		create_proc_entry(pCtlMuxData);
		return pDevice;
	}
}
void ast_removeChip(struct platform_device *pdev)
{
	tCtlMuxData *pCtlMuxData = NULL;
	if(pdev != NULL) {
		pCtlMuxData = pdev->dev.platform_data;
		platform_device_unregister(pdev);
		kfree(pdev);
		kfree(pCtlMuxData);
		--deviceCount;
	}
}

void ast_notifyDeselectChan(struct platform_device *pdev)
{
	tCtlMuxData *pCtlMuxData = NULL;

	if (pdev != NULL)
	{
		pCtlMuxData = pdev->dev.platform_data;
		pCtlMuxData->doChanDeselect = 1;
		wake_up(&(pCtlMuxData->chanDeselectWait));
	}
}

static int i2c_mux_ast_probe(struct platform_device *pdev)
{
	struct i2c_adapter *adap = NULL;
	tCtlMuxData *pCtlMuxData = pdev->dev.platform_data;
	int num;
	#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,17))
	struct i2c_mux_core *muxc=NULL;
	int ret=0;
	#endif
	
	//printk(KERN_ERR "i2c_mux_ast_probe: %p\n",pdev);
	if(pCtlMuxData == NULL) {
		return -1;
	}
	
	adap = i2c_get_adapter(pCtlMuxData->bus);
	if(adap == NULL) {
		return -1;
	}
		
	pCtlMuxData->status = 0;

	#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,17))
        muxc = i2c_mux_alloc(adap, &pdev->dev, pCtlMuxData->avaiableChannels, 0,0,muxProp[pCtlMuxData->type].select_chan, muxProp[pCtlMuxData->type].deselect_chan);
	if (!muxc)
	{
		goto virt_reg_failed;
	}
	muxc->priv=(void*)pCtlMuxData;
	#endif

	// Now create an adapter for each channel
	pCtlMuxData->chanUsed = 0;
	memset(pCtlMuxData->virtAdaps,0,(sizeof(struct i2c_adapter *)*I2C_MUX_AST_MAX_CHAN));
	for (num = 0; num < pCtlMuxData->avaiableChannels; ++num) {
		if(pCtlMuxData->vBusNum[num] != 0) {
			pCtlMuxData->chanUsed |= 1 << num;
			#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,17))
			//printk(KERN_ERR "pCtlMuxData->vBusNum[num]=%d, num=%d\n",pCtlMuxData->vBusNum[num], num);
			ret=i2c_mux_add_adapter(muxc,pCtlMuxData->vBusNum[num], num,0);
			if(ret)
			{
			   i2c_mux_del_adapters(muxc);
			   printk(KERN_ERR " Created i2c device %d fail\n",pCtlMuxData->vBusNum[num]);	
			   goto virt_reg_failed;
			}

			#else
			pCtlMuxData->virtAdaps[num] = i2c_add_mux_adapter(adap,  &pdev->dev, pdev, pCtlMuxData->vBusNum[num], num, 0, muxProp[pCtlMuxData->type].select_chan, muxProp[pCtlMuxData->type].deselect_chan);
			
			if (pCtlMuxData->virtAdaps[num] == NULL) {
				printk(KERN_ERR "failed to register multiplexed adapter %d as bus %d\n", num, pCtlMuxData->vBusNum[num]);
				goto virt_reg_failed;
			}
			#endif

		}
		else
			pCtlMuxData->virtAdaps[num] = NULL;
		
	}
	return pCtlMuxData->status;

virt_reg_failed:
	for (num--; num >= 0; num--)
		if(pCtlMuxData->virtAdaps[num] != NULL)
		{
			#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,17))
			i2c_del_mux_adapter(pCtlMuxData->virtAdaps[num]);
			#endif
		}
	pCtlMuxData->status = -ENODEV;
	//i2c_put_adapter(adap);
	return pCtlMuxData->status;
}

static int i2c_mux_ast_remove(struct platform_device *pdev)
{
	int num;
	tCtlMuxData *pCtlMuxData = NULL;
	
	//printk(KERN_ERR "i2c_mux_ast_remove: REMOVE %p\n",pdev);
	
	if(pdev != NULL) {
		pCtlMuxData = pdev->dev.platform_data;
		if(pCtlMuxData != NULL) {
			for (num = 0; num < pCtlMuxData->avaiableChannels; ++num) {
					if (pCtlMuxData->virtAdaps[num]) {
						#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,17))
						i2c_del_mux_adapter(pCtlMuxData->virtAdaps[num]);
						#endif
						pCtlMuxData->virtAdaps[num] = NULL;
					}
				}
		}
	}
	return 0;
}

static struct platform_driver i2c_mux_ast_driver = {
	.probe          = i2c_mux_ast_probe,
	.remove         = i2c_mux_ast_remove,
	.driver = {
			.name  = I2C_MUX_AST_NAME,
	},
};

static int __init i2c_mux_ast_init(void)
{
	int ret = 0;
	ret = platform_driver_register(&i2c_mux_ast_driver);
	printk(KERN_ERR "The i2c_mux_hw driver is loaded successfully with return code:%d\n",ret);
	return 0;
}
static void __exit i2c_mux_ast_exit(void)
{
	platform_driver_unregister(&i2c_mux_ast_driver);
	printk(KERN_ERR "The i2c_mux_hw driver is unloaded successfully\n");
}

module_init(i2c_mux_ast_init);
module_exit(i2c_mux_ast_exit);

MODULE_DESCRIPTION("I2C Mux Driver for AST SOC");
MODULE_LICENSE("GPL v2");

EXPORT_SYMBOL_GPL(ast_addChip);
EXPORT_SYMBOL_GPL(ast_resetChip);
EXPORT_SYMBOL_GPL(ast_removeChip);
EXPORT_SYMBOL_GPL(ast_getType);
EXPORT_SYMBOL_GPL(ast_setMaskChip);
EXPORT_SYMBOL_GPL(ast_getStatus);
EXPORT_SYMBOL_GPL(ast_notifyDeselectChan);

