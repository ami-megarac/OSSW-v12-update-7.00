 /*
 * File name: ast_mctppcie.c
 */
#include <linux/poll.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/io.h>
#include <asm/uaccess.h>

#include "driver_hal.h"
#include "mctppcie.h"
#include "mctppcieifc.h"
#include "ast_mctppcie.h"
#include <mach/platform.h>
#include <helper.h>

#define MCTP_DBUG(fmt, args...) printk(KERN_DEBUG "%s() " fmt,__FUNCTION__, ## args)
#define AST_MCTPPCIE_DRIVER_NAME	"ast_mctppcie"

struct ast_mctp_info {
	void __iomem *reg_base;
	void __iomem *pci_bdf_regs;
	struct miscdevice misc_dev;
	bool is_open;
	int irq;
	int pcie_irq;
	int mctp_version;
	struct reset_control *reset;
	u32 dram_base;

	/* mctp tx info */
	struct completion tx_complete;

	int tx_fifo_num;
	int tx_idx;
	int tx_payload;

	void *tx_pool;
	dma_addr_t tx_pool_dma;

	struct ast_mctp_cmd_desc *tx_cmd_desc;
	dma_addr_t tx_cmd_desc_dma;

	/* mctp rx info */
	void *rx_pool;
	dma_addr_t rx_pool_dma;

	int rx_dma_pool_size;
	int rx_fifo_size;
	int rx_fifo_num;

	void *rx_cmd_desc;
	dma_addr_t rx_cmd_desc_dma;

	int rx_idx;
	int rx_hw_idx;
	int rx_full;
	int rx_payload;

        //AMI
        int hal_id;
        unsigned char wait_tx_complete;
        unsigned char reset_occur;
        int asigned_EID;
};

extern int mctppcie_core_loaded;
static struct ast_mctp_info *ast_mctp=NULL;
static pcie_core_funcs_t *pmctppcie_core_funcs;
static hw_hal_t ast_mctppcie_hw_hal;
void ast_mctppcie_exit(void);

static inline u32 ast_mctp_read(struct ast_mctp_info *ast_mctp, u32 reg)
{
	u32 val;
	val = ioread32( ast_mctp->reg_base + reg );
	return val;
}

static inline void ast_mctp_write(struct ast_mctp_info *ast_mctp, u32 val, u32 reg)
{
	iowrite32 ( val, ast_mctp->reg_base + reg);
}

static void ast_mctp_ctrl_init(struct ast_mctp_info *ast_mctp)
{
	int i = 0;
        struct ast_mctp_cmd_desc *rx_cmd_desc=NULL;
	MCTP_DBUG("dram base %x \n", ast_mctp->dram_base);
	ast_mctp_write(ast_mctp, (ast_mctp_read(ast_mctp, ASPEED_MCTP_EID) & 0xff) |
			  ast_mctp->dram_base, ASPEED_MCTP_EID);

	ast_mctp->tx_idx = 0;
	ast_mctp_write(ast_mctp, ast_mctp->tx_cmd_desc_dma, ASPEED_MCTP_TX_CMD);

	ast_mctp->rx_idx = 0;
	ast_mctp->rx_hw_idx = 0;

	//rx fifo datarx_cmd_desc_dma
	//ast2400/ast2500 : each 128 bytes align, and only 64 bytes can receive
	rx_cmd_desc = ast_mctp->rx_cmd_desc;
	for (i = 0; i < ast_mctp->rx_fifo_num; i++) {
            rx_cmd_desc[i].desc0 = 0;
            rx_cmd_desc[i].desc1 = RX_DATA_ADDR(ast_mctp->rx_pool_dma + (ast_mctp->rx_fifo_size * i));
            if (i == (ast_mctp->rx_fifo_num - 1))
                rx_cmd_desc[i].desc1 |= LAST_CMD;
            
            MCTP_DBUG("Rx [%d]: desc0: %x , desc1: %x \n", i, rx_cmd_desc[i].desc0, rx_cmd_desc[i].desc1);
	}
        
        ast_mctp_write(ast_mctp, ast_mctp->rx_cmd_desc_dma, ASPEED_MCTP_RX_CMD);
	ast_mctp_write(ast_mctp, MCTP_RX_COMPLETE | MCTP_RX_NO_CMD, ASPEED_MCTP_IER);
        ast_mctp_write(ast_mctp, ast_mctp_read(ast_mctp, ASPEED_MCTP_CTRL) | MCTP_RX_CMD_RDY, ASPEED_MCTP_CTRL);

}

static int enable_mctp_pkt(void)
{           
        return 0;
} 

static int mctp_check_reset(mctp_iodata *io)
{
    io->data[0] = ast_mctp->reset_occur;
    io->len = 1;
    io->status = MCTP_STATUS_SUCCESS;
    if(ast_mctp->reset_occur)
        ast_mctp->reset_occur = 0;
    
    return 0;
}

static void ast_mctp_reset(void)
{
    unsigned int sts;
    printk("Reset MCTP Controller\n");
    *(volatile u32 *)(IO_ADDRESS(AST_SCU_BASE)) = PROTECTION_KEY;
    sts =*(volatile u32 *)(IO_ADDRESS(AST_SCU4));
    *(volatile u32 *)(IO_ADDRESS(AST_SCU4)) = sts | SCU_RESET_MCTP;
    sts =*(volatile u32 *)(IO_ADDRESS(AST_SCU4));
    *(volatile u32 *)(IO_ADDRESS(AST_SCU4)) = sts & (~SCU_RESET_MCTP);
    *(volatile u32 *)(IO_ADDRESS(AST_SCU_BASE)) = 0;
    return ;
}

static void ast_scu_init(void)
{
    int status;
    //reference SDK ast-bmc-scu.c
    //*(volatile u32 *)(IO_ADDRESS(AST_SCU18)) = 0x003f0000;
    status = *(volatile u32 *)(IO_ADDRESS(AST_SCU18));
    *(volatile u32 *)(IO_ADDRESS(AST_SCU18)) = status |\
                                               INTR_PCIE_H_L_RESET_EN |\
                                               INTR_PCIE_L_H_RESET_EN;
    
    status = *(volatile u32 *)(IO_ADDRESS(AST_SCU18));
    return ;
}

static irqreturn_t ast_scu_isr (int this_irq, void *dev_id)
{
    unsigned int sts;
    MCTP_DBUG("MCTP PCIe Reset\n");
    sts = *(volatile u32 *)(IO_ADDRESS(AST_SCU18));
    *(volatile u32 *)(IO_ADDRESS(AST_SCU18)) = sts & (~SCU_RESET_MCTP);
    
    if(ast_mctp->wait_tx_complete)
    {
        complete(&ast_mctp->tx_complete);//avoid deadlock
        ast_mctp->wait_tx_complete=0;
    }

    ast_mctp->reset_occur = !(0);
    ast_mctp->asigned_EID = 0;
    ast_mctp_ctrl_init(ast_mctp);
    return IRQ_HANDLED;
}


static irqreturn_t mctppcie_handler(int this_irq, void *dev_id)
{
    struct ast_mctp_cmd_desc *rx_cmd_desc=NULL;
    u32 status = ast_mctp_read(ast_mctp, ASPEED_MCTP_ISR);

    MCTP_DBUG("status: %08x \n", status);

    if (status & MCTP_TX_LAST) {
        //only for ast2400/ast2500, ast2600 is tx fifo
	ast_mctp_write(ast_mctp, MCTP_TX_LAST, ASPEED_MCTP_ISR);
	complete(&ast_mctp->tx_complete);
        ast_mctp->wait_tx_complete=0;
    }

    if (status & MCTP_TX_COMPLETE) {
	ast_mctp_write(ast_mctp, MCTP_TX_COMPLETE, ASPEED_MCTP_ISR);
	//printk("don't care don't use \n");
    }

    if (status & MCTP_RX_COMPLETE) {
        rx_cmd_desc = ast_mctp->rx_cmd_desc;
        
        while (rx_cmd_desc[ast_mctp->rx_hw_idx].desc0 & CMD_UPDATE) {
            ast_mctp->rx_hw_idx++;
            ast_mctp->rx_hw_idx %= ast_mctp->rx_fifo_num;
            if (ast_mctp->rx_hw_idx == ast_mctp->rx_idx)
                break;
        }
        
        ast_mctp_write(ast_mctp, MCTP_RX_COMPLETE, ASPEED_MCTP_ISR);
    }

    if (status & MCTP_RX_NO_CMD) {
        ast_mctp->rx_full = 1;
        ast_mctp_write(ast_mctp, MCTP_RX_NO_CMD, ASPEED_MCTP_ISR);
        printk("MCTP_RX_NO_CMD \n");
    }

    return IRQ_HANDLED;
}

static int mctppcie_rx_recv(mctp_iodata *io)
{
    struct ast_mctp_cmd_desc *rx_cmd_desc = ast_mctp->rx_cmd_desc;
    u32 desc0 = 0;
    struct ast_mctp_xfer mctp_xfer;
    unsigned int pci_bdf;
    int recv_length=0;
    int recv_mulit_length=0; 

    io->len = 0;
    desc0 = rx_cmd_desc[ast_mctp->rx_idx].desc0;
    
    if ((ast_mctp->rx_idx == ast_mctp->rx_hw_idx) && (!desc0)) {
        if (ast_mctp->rx_full) {
            MCTP_DBUG("re-trigger\n");
            ast_mctp_ctrl_init(ast_mctp);
            ast_mctp->rx_full = 0;
	}
	return 0;
    }

    if (!desc0)
        return 0;

    mctp_xfer.header.length = GET_PKG_LEN(desc0);
    mctp_xfer.xfer_buff=&io->data[0];


    if (mctp_xfer.header.length != 0 && mctp_xfer.header.length < GET_PKG_LEN(desc0))
    {
        io->status = MCTP_STATUS_ERROR;
        printk("No Data\n");
        return -EINVAL;
    }


    MCTP_DBUG("mctp_xfer.header.length %d \n", mctp_xfer.header.length);
    mctp_xfer.header.pad_len = GET_PADDING_LEN(desc0);
    mctp_xfer.header.src_epid = GET_SRC_EPID(desc0);
    mctp_xfer.header.type_routing = GET_ROUTING_TYPE(desc0);
    mctp_xfer.header.pkt_seq = GET_SEQ_NO(desc0);
    mctp_xfer.header.msg_tag = GET_MSG_TAG(desc0);
    mctp_xfer.header.eom = GET_MCTP_EOM(desc0);
    mctp_xfer.header.som = GET_MCTP_SOM(desc0);
    mctp_xfer.header.to  = (desc0); 
    // 0x1e6ed0c4[4:0]: Dev#
    // 0x1e6ed0c4[12:5]: Bus#
    // Fun# always 0
    pci_bdf = readl(ast_mctp->pci_bdf_regs + 0xc4);
    mctp_xfer.header.pcie_target_id = (pci_bdf & 0x1f) << 3 |(pci_bdf >> 5 & 0xff) << 8;

    recv_length = (mctp_xfer.header.length * 4) - mctp_xfer.header.pad_len;
    MCTP_DBUG("TYPE:%x TAG:%d SOM:%d EOM:%d SEQ=%d LENGTH=%d\n",mctp_xfer.header.type_routing,mctp_xfer.header.msg_tag,mctp_xfer.header.som,mctp_xfer.header.eom,mctp_xfer.header.pkt_seq,recv_length);

    io->data[0] = mctp_xfer.header.type_routing;
    io->data[1] = mctp_xfer.header.src_epid;
    io->data[2] = mctp_xfer.header.msg_tag;

    if(mctp_xfer.header.som && mctp_xfer.header.eom)
    {
        //Handle Signgle packet
        recv_length = (mctp_xfer.header.length * 4)- mctp_xfer.header.pad_len;
        //MCTP_DBUG("SP TYPE:%x TAG:%d SOM:%d EOM:%d SEQ=%d LENGTH=%d\n",mctp_xfer.header.type_routing,mctp_xfer.header.msg_tag,mctp_xfer.header.som,mctp_xfer.header.eom,mctp_xfer.header.pkt_seq,recv_length);
        memcpy(io->data + 3, ast_mctp->rx_pool + (ast_mctp->rx_fifo_size * ast_mctp->rx_idx), recv_length);
        io->len = recv_length + 3;
        io->status = MCTP_STATUS_SUCCESS;
    }
    else
    {
        //Handle multiple packets
        mctp_xfer.xfer_buff=&io->data[3];
        memcpy(mctp_xfer.xfer_buff,ast_mctp->rx_pool + (ast_mctp->rx_fifo_size * ast_mctp->rx_idx), recv_length);
        recv_mulit_length = multiple_mctp_packets_handler(&mctp_xfer.header, mctp_xfer.xfer_buff, recv_length);
        io->status = (recv_mulit_length > 0 ? MCTP_STATUS_SUCCESS : MCTP_STATUS_ERROR);
        io->len = (io->status==MCTP_STATUS_SUCCESS ? (recv_mulit_length+3) : 0 );
        //printk("io->status=%d , io->len=%d\n",io->status,io->len);
    }
   
    rx_cmd_desc[ast_mctp->rx_idx].desc0 = 0;
    ast_mctp->rx_idx++;
    ast_mctp->rx_idx %= ast_mctp->rx_fifo_num;
    return 0;
}

static int ast_mctp_tx_xfer(struct ast_mctp_info *ast_mctp, mctp_iodata *mctp_xfer)
{
    uint32_t reg;
    u32 xfer_len = (mctp_xfer->len /4);
    u32 padding_len;
    if((mctp_xfer->len % 4))
    {
        xfer_len++;
        padding_len = 4 - ((mctp_xfer->len) % 4);
    }
    else
        padding_len = 0;


        // TODO: Check TX Command
        /* Data Structure
         *      
         * byte 0 : Routing Type
         * byte 1 : Destination EID
         * byte 2 : Source EID
         * byte 3 : Bus Number
         * byte 4 : Device Number
         * byte 5 : Function Number
         * byte 6 : Tag Owner
         * byte 7 - N : payload Data
         */     
         MCTP_DBUG("mctp_xfer->data[0]=%x\n",mctp_xfer->data[0]);
         MCTP_DBUG("mctp_xfer->data[1]=%x\n",mctp_xfer->data[1]);
         MCTP_DBUG("mctp_xfer->data[2]=%x\n",mctp_xfer->data[2]);
         MCTP_DBUG("mctp_xfer->data[3]=%x\n",mctp_xfer->data[3]);
         MCTP_DBUG("mctp_xfer->data[4]=%x\n",mctp_xfer->data[4]);
         MCTP_DBUG("mctp_xfer->data[5]=%x\n",mctp_xfer->data[5]);
         MCTP_DBUG("mctp_xfer->data[6]=%x\n",mctp_xfer->data[6]);
         MCTP_DBUG("xfer_len =%d\n",xfer_len);

	ast_mctp->tx_cmd_desc->desc0 = INT_ENABLE | G5_ROUTING_TYPE_L(mctp_xfer->data[0]) | G5_ROUTING_TYPE_H(mctp_xfer->data[0]) |
						  G5_PKG_SIZE(xfer_len) | BUS_NO(mctp_xfer->data[3]) | DEV_NO(mctp_xfer->data[4]) | FUN_NO(mctp_xfer->data[5]) |
						  PADDING_LEN(padding_len) | TAG_OWN(mctp_xfer->data[6]);
	ast_mctp->tx_cmd_desc->desc1 = LAST_CMD | DEST_EP_ID(mctp_xfer->data[1]) | G5_TX_DATA_ADDR(ast_mctp->tx_pool_dma);
	

        memcpy(ast_mctp->tx_pool, mctp_xfer->data + 7, mctp_xfer->len);

        if(!ast_mctp->asigned_EID)
        {
                if(mctp_xfer->data[2] != 0)
                {
                        iowrite32 (ioread32( ast_mctp->reg_base + AST_MCTP_CTRL ) | MCTP_RX_RDY | MCTP_MSG_MASK | MCTP_MATCH_EID, ast_mctp->reg_base + AST_MCTP_CTRL);
                        reg = ioread32( ast_mctp->reg_base + AST_MCTP_EID );
                        iowrite32 (reg | MCTP_EP_ID(mctp_xfer->data[2]), ast_mctp->reg_base + AST_MCTP_EID);
                        ast_mctp->asigned_EID = 1;
                }
        }

	//old ast2400/ast2500 only one tx fifo and wait for tx complete
	init_completion(&ast_mctp->tx_complete);

        MCTP_DBUG("ast_mctp->tx_cmd_desc->desc0 =%08x\n",ast_mctp->tx_cmd_desc->desc0);
        MCTP_DBUG("ast_mctp->tx_cmd_desc->desc1 =%08x\n",ast_mctp->tx_cmd_desc->desc1);

        ast_mctp_write(ast_mctp, ast_mctp_read(ast_mctp, ASPEED_MCTP_IER) | MCTP_TX_LAST, ASPEED_MCTP_IER);

	//trigger tx
	ast_mctp_write(ast_mctp, ast_mctp_read(ast_mctp, ASPEED_MCTP_CTRL) | MCTP_TX_TRIGGER, ASPEED_MCTP_CTRL);

        ast_mctp->wait_tx_complete=!(0);
	wait_for_completion(&ast_mctp->tx_complete);

        return 0;
}

static int mctppcie_tx_xfer(mctp_iodata *mctp_xfer)
{

        if ((ioread32(ast_mctp->pci_bdf_regs + 0xc4) & 0x1fff) == 0) {
            printk("PCIE not ready \n");
            return -EFAULT;
        }

    return ast_mctp_tx_xfer(ast_mctp, mctp_xfer);
}



static mctppcie_hal_operations_t ast_mctppcie_hw_ops = {
	downstream_mctp_pkt	:	mctppcie_rx_recv,
	upstream_mctp_pkt	:	mctppcie_tx_xfer,
	enable_mctp_pkt		:	enable_mctp_pkt,
	mctp_check_reset	:	mctp_check_reset,
};	

static hw_hal_t ast_mctppcie_hw_hal = {
	.dev_type		= EDEV_TYPE_MCTP_PCIE,
	.owner			= THIS_MODULE,
	.devname		= AST_MCTPPCIE_DRIVER_NAME,
	.num_instances	        = MCTP_PCIE_HW_MAX_INST,
	.phal_ops		= (void *) &ast_mctppcie_hw_ops
};

int ast_mctppcie_init(void)
{
    int ret = 0;
    int hal_id=0;

    if (!mctppcie_core_loaded)
            return -1;

    hal_id = register_hw_hal_module(&ast_mctppcie_hw_hal, (void **) &pmctppcie_core_funcs);
    if (hal_id < 0) 
    {
        printk(KERN_WARNING "%s: register HAL HW module failed\n", AST_MCTPPCIE_DRIVER_NAME);
        return hal_id;
    }

    if (!(ast_mctp = kzalloc(sizeof(struct ast_mctp_info), GFP_KERNEL))) {
        unregister_hw_hal_module(EDEV_TYPE_MCTP_PCIE, hal_id);
        return -ENOMEM;
    }

    ast_mctp->hal_id = hal_id;
    ast_mctp->mctp_version = 5; //AST2500 only
    
    ast_mctp->reg_base = ioremap(AST_MCTP_REG_BASE, 0x1000);
    if (!ast_mctp->reg_base) {
        printk(KERN_WARNING "%s: ioremap failed\n", AST_MCTPPCIE_DRIVER_NAME);
        goto out;
    }

    // only for ast2500
    ast_mctp->tx_fifo_num = MCTP_TX_FIFO_NUM;
    ast_mctp->rx_fifo_size = 128;
    ast_mctp->rx_fifo_num = MCTP_RX_BUFF_POOL_SIZE / ast_mctp->rx_fifo_size;
    ast_mctp->dram_base = G5_DRAM_BASE_ADDR;

    ast_mctp->pci_bdf_regs = ioremap(0x1e6ed000, 0x100);
    if (!ast_mctp->pci_bdf_regs) {
        printk(KERN_WARNING "%s: ioremap failed\n", AST_MCTPPCIE_DRIVER_NAME);
        goto out;
    }

    ast_scu_init();

    ast_mctp_reset();

    ast_mctp->pcie_irq = GetIrqFromDT("ami_scu", IRQ_SCU);
    ret = request_irq(ast_mctp->pcie_irq, ast_scu_isr, IRQF_SHARED, "ast-scu", (void*)AST_SCU_BASE);
    if (ret !=0 ) {
        printk("AST SCU Unable request IRQ \n");
        goto out;
    }

    ast_mctp->irq = GetIrqFromDT("ami_mctp", IRQ_MCTP);
    ret  = request_irq(ast_mctp->irq, mctppcie_handler, IRQF_SHARED, AST_MCTPPCIE_DRIVER_NAME, (void *)AST_MCTP_REG_BASE);
    if( ret  != 0 )
    {
        printk( KERN_ERR "%s: Failed request irq %d, return %d\n", AST_MCTPPCIE_DRIVER_NAME, ast_mctp->irq, ret);
        goto out;
    }

    ast_mctp->tx_idx = 0;

    //tx desc allocate --> tx desc : 0~4096, rx desc : 4096 ~ 8192
    ast_mctp->tx_cmd_desc = dma_alloc_coherent(NULL,MCTP_DESC_SIZE,&ast_mctp->tx_cmd_desc_dma, GFP_KERNEL);

    //tx buff pool init
    //ast2400/ast2500 : 128 bytes aligned,
    ast_mctp->tx_pool = dma_alloc_coherent(NULL,MCTP_TX_BUFF_SIZE * ast_mctp->tx_fifo_num,&ast_mctp->tx_pool_dma, GFP_KERNEL);

    //rx desc allocate : 4096 ~ 8192
    ast_mctp->rx_cmd_desc = (void *)ast_mctp->tx_cmd_desc + 4096;
    ast_mctp->rx_cmd_desc_dma = ast_mctp->tx_cmd_desc_dma + 4096;

    //rx buff pool init :
    //ast2400/ast2500, data address [29:7]: 0x00 , 0x80 , 0x100, 0x180,
    ast_mctp->rx_pool = dma_alloc_coherent(NULL,MCTP_RX_BUFF_POOL_SIZE,&ast_mctp->rx_pool_dma, GFP_KERNEL);

    ast_mctp_ctrl_init(ast_mctp);
   
    if(create_mctp_packets_queue()!=0)
        goto out;

    printk(KERN_INFO "%s: driver successfully loaded.\n",AST_MCTPPCIE_DRIVER_NAME);

    return 0;

    out:
        ast_mctppcie_exit();
        printk(KERN_WARNING "%s: driver init failed !\n",AST_MCTPPCIE_DRIVER_NAME);

    return (-ENOENT);
}

void ast_mctppcie_exit(void)
{
        printk("%s exit\n",AST_MCTPPCIE_DRIVER_NAME);
	unregister_hw_hal_module(EDEV_TYPE_MCTP_PCIE, ast_mctp->hal_id);

        if(ast_mctp->pcie_irq)          free_irq(ast_mctp->pcie_irq,(void*)AST_SCU_BASE);
        if(ast_mctp->irq)               free_irq(ast_mctp->irq, (void *)AST_MCTP_REG_BASE);
        if(ast_mctp->tx_cmd_desc)       dma_free_coherent(NULL, MCTP_DESC_SIZE, ast_mctp->tx_cmd_desc, ast_mctp->tx_cmd_desc_dma);
        if(ast_mctp->tx_pool)           dma_free_coherent(NULL, MCTP_TX_BUFF_SIZE * ast_mctp->tx_fifo_num, ast_mctp->tx_pool, ast_mctp->tx_pool_dma);
        if(ast_mctp->rx_pool)           dma_free_coherent(NULL, MCTP_RX_BUFF_POOL_SIZE, ast_mctp->rx_pool, ast_mctp->rx_pool_dma);
	if(ast_mctp->reg_base)          iounmap(ast_mctp->reg_base);	
        if(ast_mctp->pci_bdf_regs)      iounmap(ast_mctp->pci_bdf_regs);	
	if(ast_mctp)                    kfree(ast_mctp);
        ast_mctp=NULL;
        delete_mctp_packets_queue();
        return;
}

module_init (ast_mctppcie_init);
module_exit (ast_mctppcie_exit);

MODULE_AUTHOR("American Megatrends Inc.");
MODULE_DESCRIPTION("ASPEED AST SoC MCTP Over PCIE Driver");
MODULE_LICENSE ("GPL");
