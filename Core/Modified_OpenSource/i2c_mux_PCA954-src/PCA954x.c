#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/version.h>
#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,17))
#include <linux/poll.h>
#endif

typedef enum {
	pca_9543 = 0,
	pca_9546,
	pca_9548,
	pca_9544,
	pca_unsuported = 0xFF
}ePcaType;

#define PCA954X_MAX_CHAN    8
#define PCA9548_MAX_CHAN	8
#define PCA9546_MAX_CHAN	4
#define PCA9543_MAX_CHAN	2
#define PCA9544_MAX_CHAN	4

typedef struct 
{
	int status;
	ePcaType type;
	u8 bus;
	u8 addr;
	u8 mask;
	u8 rstGpio;
	u8 avaiableChannels;
	u8 chanUsed;
	u8 noDeviceProbing;
	u8 dummy;
	char test[32];
	u8 vBusNum[PCA954X_MAX_CHAN];
	struct i2c_adapter *virtAdaps[PCA954X_MAX_CHAN];
	wait_queue_head_t chanDeselectWait;
	int doChanDeselect;
	int deselectTo[PCA954X_MAX_CHAN];
	int chanDisable[PCA954X_MAX_CHAN];
	#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,17))
    void *muxc;
	struct i2c_client *client;
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


static const struct i2c_device_id pca954x_id[] = {
	{ "pca9543", pca_9543 },
	{ "pca9546", pca_9546 },
	{ "pca9548", pca_9548 },
	{ "pca9544", pca_9544 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, pca954x_id);

/* reset function */
#define GPIO_GPDO_OFFSET			0x08
static void pca954x_setGPIOLow(u8 pinNum)
{
	u8 value=0;
	int	port,tpin;
	port = pinNum / 8;
	tpin = pinNum % 8;
	#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,17))
	value = *(((u8*)SE_GPIO_VA_BASE) + GPIO_GPDO_OFFSET + port * 0x10);
	#endif
	value = value & ~ ( 0x01 << tpin );
	#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,17))
	*(((u8*)SE_GPIO_VA_BASE) + GPIO_GPDO_OFFSET + port * 0x10) = value;
	#endif
}
static void pca954x_setGPIOHigh(u8 pinNum)
{
	u8 value=0;
	int	port,tpin;
	port = pinNum / 8;
	tpin = pinNum % 8;
	#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,17))
	value = *(((u8*)SE_GPIO_VA_BASE) + GPIO_GPDO_OFFSET + port * 0x10);
	#endif
	value = value | ( 0x01 << tpin );
	#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,17))
	*(((u8*)SE_GPIO_VA_BASE) + GPIO_GPDO_OFFSET + port * 0x10) = value;
	#endif
}
static void pca954x_reset(u8 pin) {
	if ((pin == 0x00) || (pin == 0xFF))
		return;
	pca954x_setGPIOLow(pin);
	pca954x_setGPIOHigh(pin);
}
/* end of reset functions */

static u8 getChanDisableMask(tCtlMuxData *pCtlMuxData)
{
	u8 disableMask = 0;
	int i;

	if (pCtlMuxData != NULL)
	{
		for (i = 0; i < pCtlMuxData->avaiableChannels; i++)
		{
			if (pCtlMuxData->chanDisable[i]) {
				disableMask |= (u8)(1 << i);
			}
		}
	}
	return disableMask;
}

static int pca954x_reg_write(struct i2c_adapter *adap,
			     struct i2c_client *client, u8 val)
{
	int ret = -ENODEV;

	if (adap->algo->master_xfer) {
		struct i2c_msg msg;
		char buf[1];

		msg.addr = client->addr;
		msg.flags = 0;
		msg.len = 1;
		buf[0] = val;
		msg.buf = buf;
		ret = adap->algo->master_xfer(adap, &msg, 1);
	} else {
		union i2c_smbus_data data;
		ret = adap->algo->smbus_xfer(adap, client->addr,
					     client->flags,
					     I2C_SMBUS_WRITE,
					     val, I2C_SMBUS_BYTE, &data);
	}

	return ret;
}

static int isI2cSlaveActive(int bus)
{
#define I2C_DBG_STS     0x944
#define I2C_DBG_STS_1   0x978

	unsigned int i2cStatus = 0;

	if ((bus >= 0) && (bus <= 7)) {
		#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,17))
		i2cStatus = (*(volatile unsigned int *)(SE_SYS_CLK_VA_BASE + I2C_DBG_STS)) >> (bus * 4);
		#endif
	} else if ((bus == 8) || (bus == 9)) {
		#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,17))
		i2cStatus = (*(volatile unsigned int *)(SE_SYS_CLK_VA_BASE + I2C_DBG_STS_1)) >> ((bus - 8) * 4);
		#endif
	}

	return (i2cStatus & 1);
}

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,17))
static int pca954x_select_chan(struct i2c_adapter *adap, void *client, u32 chan)
{
	u8 regval = 0;
	int ret = 0;
	tCtlMuxData *pCtlMuxData = NULL;
	struct i2c_client *i2cClient = client;
	
	if(i2cClient != NULL) {
		pCtlMuxData = i2cClient->dev.platform_data;
		if(pCtlMuxData != NULL) {
			/* If channel is being forcibly disabled do not select it but return immediately  */
			if (pCtlMuxData->chanDisable[chan]) {
				return 0;
			}
			//printk("pca954x_select_chan: %s,%x,%x\n",pCtlMuxData->test,pCtlMuxData->mask,pCtlMuxData->chanUsed);
			regval = (u8)(1 << chan);
			//some interface are open forever after configuration (except where forcibly disabled)
			regval |= pCtlMuxData->mask & ~getChanDisableMask(pCtlMuxData);
			//mask out unused bits
			regval &= pCtlMuxData->chanUsed;
	
			ret = pca954x_reg_write(adap, client, regval);
			pCtlMuxData->doChanDeselect = 0;
			return ret;
		}
	}
	return -1;
}
#else
static int pca954xk5_select_chan(struct i2c_mux_core *muxc, u32 chan)
{
	int ret=0;
        u8 regval = 0;
        tCtlMuxData *pCtlMuxData = NULL;
        if(muxc!=NULL)
        {
          pCtlMuxData=(tCtlMuxData*)muxc->priv;
          if(pCtlMuxData!=NULL)
          {
              /* If channel is being forcibly disabled do not select it but return immediately  */
              if (pCtlMuxData->chanDisable[chan]) {
              	return 0;
              }
	      //printk("pca954x_select_chan: %s,%x,%x\n",pCtlMuxData->test,pCtlMuxData->mask,pCtlMuxData->chanUsed);
              regval = (u8)(1 << chan);
              //some interface are open forever after configuration (except where forcibly disabled)
              regval |= pCtlMuxData->mask & ~getChanDisableMask(pCtlMuxData);
              //mask out unused bits
              regval &= pCtlMuxData->chanUsed;
 
	      ret = pca954x_reg_write(muxc->parent, pCtlMuxData->client, regval);
              pCtlMuxData->doChanDeselect = 0;
	      return ret;
          }

        }

	return -1;
}

#endif



#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,17))
static int pca954x_deselect_chan(struct i2c_adapter *adap, void *client, u32 chan)
{
	int ret = 0;
	u8 regval = 0;
	tCtlMuxData *pCtlMuxData = NULL;
	struct i2c_client *i2cClient = client;
	
	if(i2cClient != NULL) {
		pCtlMuxData = i2cClient->dev.platform_data;
		if(pCtlMuxData != NULL) {
			if ((!pCtlMuxData->chanDisable[chan]) && (pCtlMuxData->deselectTo[chan] != 0)) {
				int retry = 2;
				
				do {
					wait_event_timeout(pCtlMuxData->chanDeselectWait, pCtlMuxData->doChanDeselect, (msecs_to_jiffies(pCtlMuxData->deselectTo[chan])));
				} while (isI2cSlaveActive(pCtlMuxData->bus) && (retry-- > 0));
			}
			//printk("pca954x_deselect_chan: %s,%x,%x\n",pCtlMuxData->test,pCtlMuxData->mask,pCtlMuxData->chanUsed);
			//some interface are open forever after configuration (except where forcibly disabled)
			regval =  pCtlMuxData->mask & ~getChanDisableMask(pCtlMuxData);
			//mask out unused bits
			regval &= pCtlMuxData->chanUsed;
	
			ret = pca954x_reg_write(adap, client, regval);
			return ret;
		}
	}
	return -1;
}
static int pca9544_select_chan(struct i2c_adapter *adap, void *client, u32 chan)
{
	int ret = 0;
	u8 regval = 0;

	//set channel value
	regval = (chan & 0x03);
	//set enable bit
	regval |= 0x04;
	ret = pca954x_reg_write(adap, client, regval);
	
	return ret;
}
 
static int pca9544_deselect_chan(struct i2c_adapter *adap, void *client, u32 chan)
{
	int ret = 0;
	u8 regval = 0;
	regval = 0;
	
	ret = pca954x_reg_write(adap, client, regval);
	return ret;
}
#else

static int pca954xk5_deselect_chan(struct i2c_mux_core *muxc, u32 chan)
{
	int ret=0;
	u8 regval = 0;
	tCtlMuxData *pCtlMuxData = NULL;
       
       	if(muxc!=NULL)
        {
          pCtlMuxData=(tCtlMuxData*)muxc->priv;
          if(pCtlMuxData!=NULL)
          {
               if ((!pCtlMuxData->chanDisable[chan]) && (pCtlMuxData->deselectTo[chan] != 0)) {
                    int retry = 2;
                                
                    do {
                         wait_event_timeout(pCtlMuxData->chanDeselectWait, pCtlMuxData->doChanDeselect, (msecs_to_jiffies(pCtlMuxData->deselectTo[chan])));
                    } while (isI2cSlaveActive(pCtlMuxData->bus) && (retry-- > 0));
               }
              //printk("pca954x_deselect_chan: %s,%x,%x\n",pCtlMuxData->test,pCtlMuxData->mask,pCtlMuxData->chanUsed);
              //some interface are open forever after configuration (except where forcibly disabled)
              regval =  pCtlMuxData->mask & ~getChanDisableMask(pCtlMuxData);
              //mask out unused bits
              regval &= pCtlMuxData->chanUsed;
        
              ret = pca954x_reg_write(muxc->parent, pCtlMuxData->client, regval);
              return ret;
	  }

        }
	
	return -1;	
}

static int pca9544k5_select_chan(struct i2c_mux_core *muxc, u32 chan)
{
	int ret=0;
	u8 regval = 0;
	tCtlMuxData *pCtlMuxData = NULL;
       
       	if(muxc!=NULL)
        {
          pCtlMuxData=(tCtlMuxData*)muxc->priv;
          if(pCtlMuxData!=NULL)
          {
		//set channel value
		regval = (chan & 0x03);
		//set enable bit
		regval |= 0x04;
		ret = pca954x_reg_write(muxc->parent, pCtlMuxData->client, regval);
		return ret;
	  }
        }
	
	return -1;	
}
 
static int pca9544k5_deselect_chan(struct i2c_mux_core *muxc, u32 chan)
{
	int ret=0;
	u8 regval = 0;
	tCtlMuxData *pCtlMuxData = NULL;
       
       	if(muxc!=NULL)
        {
          pCtlMuxData=(tCtlMuxData*)muxc->priv;
          if(pCtlMuxData!=NULL)
          {
		ret = pca954x_reg_write(muxc->parent, pCtlMuxData->client, regval);
		return ret;
	  }
        }
	
	return -1;	
}

#endif

static tMuxProp muxProp[] =
{
	#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,17))
	{ pca_9543, PCA9543_MAX_CHAN, (u8)(sizeof("pca9543")),0, "pca9543", pca954x_select_chan, pca954x_deselect_chan},
	{ pca_9546, PCA9546_MAX_CHAN, (u8)(sizeof("pca9546")),0, "pca9546", pca954x_select_chan, pca954x_deselect_chan},
	{ pca_9548, PCA9548_MAX_CHAN, (u8)(sizeof("pca9548")),0, "pca9548", pca954x_select_chan, pca954x_deselect_chan},
	{ pca_9544, PCA9544_MAX_CHAN, (u8)(sizeof("pca9544")),0, "pca9544", pca9544_select_chan, pca9544_deselect_chan},
	#else
	{ pca_9543, PCA9543_MAX_CHAN, (u8)(sizeof("pca9543")),0, "pca9543", pca954xk5_select_chan, pca954xk5_deselect_chan},
	{ pca_9546, PCA9546_MAX_CHAN, (u8)(sizeof("pca9546")),0, "pca9546", pca954xk5_select_chan, pca954xk5_deselect_chan},
	{ pca_9548, PCA9548_MAX_CHAN, (u8)(sizeof("pca9548")),0, "pca9548", pca954xk5_select_chan, pca954xk5_deselect_chan},
	{ pca_9544, PCA9544_MAX_CHAN, (u8)(sizeof("pca9544")),0, "pca9544", pca9544k5_select_chan, pca9544k5_deselect_chan},
	#endif

};

ePcaType pca954x_getType(char *name)
{
	int loop = 0;
	for (loop = 0; loop < (sizeof(muxProp)/sizeof(tMuxProp)); ++loop) {
		//printk(KERN_ERR "loop: %i\n",loop);
		if( 0 == strncmp(name, muxProp[loop].name,muxProp[loop].sizeOfName) ) {
			return muxProp[loop].type;
		}
	}
	return pca_unsuported;
}
static char *pca954x_pDeviceName(ePcaType type)
{
	if(type >= pca_unsuported)
		return 0;
	else
		return muxProp[type].name;
}
static int pca954x_getMaxChan(ePcaType type)
{
	if(type >= pca_unsuported)
		return 0;
	else
		return muxProp[type].maxChn;
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
				proc_create_data("disable_port", S_IFREG | S_IRUGO | S_IWUGO, i2c_mux_dir, &deselect_timeout_fops, (void *)&(pCtlMuxData->chanDisable[i]));
			}
		}
	}
	
	return 0;
}

void pca954x_setMaskChip(struct i2c_client *pI2client, u8 mask)
{
	tCtlMuxData *pCtlMuxData = NULL;
	if(pI2client != NULL) {
		pCtlMuxData = pI2client->dev.platform_data;
		if((pCtlMuxData != NULL) && (pCtlMuxData->mask != mask)) {
			printk(KERN_ERR "maskChip: %s -> %02X\n",pCtlMuxData->test,mask);
			pCtlMuxData->mask = mask;
		}
	}
}
void pca954x_resetChip(struct i2c_client *pI2client)
{
	tCtlMuxData *pCtlMuxData = NULL;
	if(pI2client != NULL) {
		pCtlMuxData = pI2client->dev.platform_data;
		if(pCtlMuxData != NULL) {
			printk(KERN_ERR "resetChip: %s\n",pCtlMuxData->test);
			pca954x_reset(pCtlMuxData->rstGpio);
		}
		else
			printk(KERN_ERR "resetChip: uppps no pCtlMuxData\n");
	}
	else
		printk(KERN_ERR "resetChip: uppps no client\n");
}
int pca954x_getStatus (struct i2c_client *pI2client, char *buf)
{
	int len = 0;
	u8 j = 0;
	
	tCtlMuxData *pCtlMuxData = NULL;
	if(pI2client != NULL) {
		pCtlMuxData = (tCtlMuxData *)pI2client->dev.platform_data;
		if(pCtlMuxData != NULL) {
			len = sprintf(buf,"st: %08X, bus: %02X, addr: %02X, gpio: %d, type: %s",
						pCtlMuxData->status,pCtlMuxData->bus,pCtlMuxData->addr,pCtlMuxData->rstGpio,muxProp[pCtlMuxData->type].name);
			for(j=0;j<pCtlMuxData->avaiableChannels;++j) {
					len += sprintf(buf+len,",%s%i", ((pCtlMuxData->vBusNum[j] == 0)?"":"i2c-"),pCtlMuxData->vBusNum[j]);
			}
			len += sprintf(buf+len,"\n");
		}
	}
	return len;
	
}
static int deviceCount = 0;

struct i2c_client *pca954x_addChip(u8 bus, u8 addr, u8 mask, u8 rstGpio, int type, u8 *chanNumbering, u8 maxChanIndex, u16 *modifiers, u8 numModifiers )
{
	struct i2c_adapter *adap = NULL;
	struct i2c_client *regI2Client = NULL;
	struct i2c_board_info board_info;
	tCtlMuxData *pCtlMuxData = NULL;
	u8 k = 0;
	
	if((pCtlMuxData = kmalloc(sizeof(tCtlMuxData), GFP_KERNEL)) == NULL) {
	    return NULL;
	}

	memset(pCtlMuxData, 0, sizeof(tCtlMuxData));

	pCtlMuxData->status = (-1);
	pCtlMuxData->mask = mask;
	pCtlMuxData->rstGpio = rstGpio;
	pCtlMuxData->type = type;
	pCtlMuxData->avaiableChannels = maxChanIndex;
	pCtlMuxData->bus = bus;
	pCtlMuxData->addr = addr;
	init_waitqueue_head(&(pCtlMuxData->chanDeselectWait));
	pCtlMuxData->doChanDeselect = 0;
	
	if( pCtlMuxData->avaiableChannels > pca954x_getMaxChan(type) )
		pCtlMuxData->avaiableChannels = pca954x_getMaxChan(type);
	
	for(k = 0; k < pCtlMuxData->avaiableChannels; ++k)
		pCtlMuxData->vBusNum[k] =  chanNumbering[k];

	pCtlMuxData->noDeviceProbing = (numModifiers == 0) ? 0 : modifiers[0];

	/* If the disable channel bit mask modifier is configured apply it here */
	if ((numModifiers > 1) && (modifiers[1] != 0))
	{
		for (k = 0; k < pCtlMuxData->avaiableChannels; ++k) {
			pCtlMuxData->chanDisable[k] = (modifiers[1] & (1 << k)) ? 1 : 0;
		}
	}

	memset(&board_info,0x00,sizeof(struct i2c_board_info));
	strncpy(board_info.type,pca954x_pDeviceName(type),8);
	board_info.addr = (u16)(addr >> 1);
	board_info.platform_data = pCtlMuxData;
	
	snprintf(pCtlMuxData->test,sizeof(pCtlMuxData->test),"%i:%i,%X %s",deviceCount,bus,addr,pca954x_pDeviceName(type));
		
	adap = i2c_get_adapter(bus);
    if( adap != NULL )
    {
		regI2Client = i2c_new_device(adap, &board_info);
		printk(KERN_ERR "addChip: %p\n",regI2Client);
   		if(pCtlMuxData->status == 0) {
   			++deviceCount;
   			printk(KERN_ERR "addChip: %s\n",pCtlMuxData->test);
   			create_proc_entry(pCtlMuxData);
   			return regI2Client;
   		}
   		else { 
   			if (regI2Client != NULL)
   				i2c_unregister_device(regI2Client);
   			return NULL;
   		}
    }
    else {
            return NULL;
    }
	
}
void pca954x_removeChip(struct i2c_client *regI2Client)
{
	tCtlMuxData *pCtlMuxData = NULL;
	if(regI2Client != NULL) {
		pCtlMuxData = (tCtlMuxData *)regI2Client->dev.platform_data;
		i2c_unregister_device(regI2Client);
		
		if(pCtlMuxData != NULL) {
			printk(KERN_ERR "removeChip: %s\n",pCtlMuxData->test);
			kfree(pCtlMuxData);
		}
		
		--deviceCount;
	}
}

void pca954x_notifyDeselectChan(struct i2c_client *pI2client)
{
	tCtlMuxData *pCtlMuxData = NULL;

	if (pI2client != NULL)
	{
		pCtlMuxData = pI2client->dev.platform_data;
		pCtlMuxData->doChanDeselect = 1;
		wake_up(&(pCtlMuxData->chanDeselectWait));
	}
}

/*
 * I2C init/probing/exit functions
 */
static int pca954x_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct i2c_adapter *adap = to_i2c_adapter(client->dev.parent);
	tCtlMuxData *pCtlMuxData = client->dev.platform_data;
	int num;
	#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,17))
        struct i2c_mux_core *muxc=NULL;
        int ret=0;
        #endif

	printk(KERN_ERR "pca954x_probe: %p\n",client);
	if(pCtlMuxData == NULL) {
		return -1;
	}
	//reset chip
	pca954x_reset(pCtlMuxData->rstGpio);


	#if (LINUX_VERSION_CODE > KERNEL_VERSION(3,14,17))
        muxc = i2c_mux_alloc(adap, &client->dev, pCtlMuxData->avaiableChannels, 0,0,muxProp[pCtlMuxData->type].select_chan, muxProp[pCtlMuxData->type].deselect_chan);

        if (!muxc)
        {
                goto virt_reg_failed;
        }

	pCtlMuxData->client=client;
    	muxc->priv=(void*)pCtlMuxData;

	#endif
	pCtlMuxData->status = 0;

	if (!i2c_check_functionality(adap, I2C_FUNC_SMBUS_BYTE)) {
		pCtlMuxData->status = -ENODEV;
		return pCtlMuxData->status;
	}


	// Check if the device is present 
	if (!pCtlMuxData->noDeviceProbing) {
		pCtlMuxData->status = i2c_smbus_write_byte(client, 0);
		if(pCtlMuxData->status < 0) {
			dev_warn(&client->dev, "probe failed\n");
			return pCtlMuxData->status;
		}
	}

	// Now create an adapter for each channel
	pCtlMuxData->chanUsed = 0;
	memset(pCtlMuxData->virtAdaps,0,(sizeof(struct i2c_adapter *)*PCA954X_MAX_CHAN));
	for (num = 0; num < pCtlMuxData->avaiableChannels; ++num) {
		if(pCtlMuxData->vBusNum[num] != 0) {
			
			#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3,14,17))
			pCtlMuxData->virtAdaps[num] = i2c_add_mux_adapter(adap,  &client->dev, client, pCtlMuxData->vBusNum[num], num, 0, muxProp[pCtlMuxData->type].select_chan, muxProp[pCtlMuxData->type].deselect_chan);
			if (pCtlMuxData->virtAdaps[num] == NULL) {
				printk(KERN_ERR "failed to register multiplexed adapter %d as bus %d\n", num, pCtlMuxData->vBusNum[num]);
				goto virt_reg_failed;
			}
			#else
                        ret=i2c_mux_add_adapter(muxc,pCtlMuxData->vBusNum[num], num,0);
                        if(ret)
                        {
                            i2c_mux_del_adapters(muxc);
                            printk(KERN_ERR "failed to register multiplexed adapter %d as bus %d\n", num, pCtlMuxData->vBusNum[num]);
			   goto virt_reg_failed;
                        }
			#endif
			
			pCtlMuxData->chanUsed |= 1 << num;
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

	return pCtlMuxData->status;

}

static int pca954x_remove(struct i2c_client *client)
{
	int num;
	tCtlMuxData *pCtlMuxData = NULL;
	if(client != NULL) {
		pCtlMuxData = client->dev.platform_data;
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

static struct i2c_driver pca954x_driver = {
	.driver		= {
		.name	= "pca954x",
		.owner	= THIS_MODULE,
	},
	.probe		= pca954x_probe,
	.remove		= pca954x_remove,
	.id_table	= pca954x_id,
};



static int __init i2c_mux_PCA954x_init(void)
{
	printk(KERN_ERR "The i2c_mux_PCA954x driver is loaded successfully\n");
	i2c_add_driver(&pca954x_driver);
	return 0;
}
static void __exit i2c_mux_PCA954x_exit(void)
{
	printk(KERN_ERR "The i2c_mux_PCA954x driver is unloaded successfully\n");
	i2c_del_driver(&pca954x_driver);
}

module_init(i2c_mux_PCA954x_init);
module_exit(i2c_mux_PCA954x_exit);

MODULE_DESCRIPTION("I2C Mux Driver for PCA954x");
MODULE_LICENSE("GPL v2");

EXPORT_SYMBOL_GPL(pca954x_addChip);
EXPORT_SYMBOL_GPL(pca954x_resetChip);
EXPORT_SYMBOL_GPL(pca954x_removeChip);
EXPORT_SYMBOL_GPL(pca954x_getType);
EXPORT_SYMBOL_GPL(pca954x_setMaskChip);
EXPORT_SYMBOL_GPL(pca954x_getStatus);
EXPORT_SYMBOL_GPL(pca954x_notifyDeselectChan);

