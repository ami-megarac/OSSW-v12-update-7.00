/****************************************************************
 **                                                            **
 **    (C)Copyright 2009-2015, American Megatrends Inc.        **
 **                                                            **
 **            All Rights Reserved.                            **
 **                                                            **
 **        5555 Oakbrook Pkwy Suite 200, Norcross              **
 **                                                            **
 **        Georgia - 30093, USA. Phone-(770)-246-8600.         **
 **                                                            **
****************************************************************/

#ifndef _AST_MCTP_PCIE_H_
#define _AST_MCTP_PCIE_H_

#define AST_MCTP_REG_BASE	0x1E6E8000
#define AST_MCTP_DRAM_BASE	0x80000000
#define MCTP_PCIE_HW_MAX_INST 1
#define AST_SCU_BASE            0x1E6E2000
#define AST_SCU18               (AST_SCU_BASE + 0x18)
#define AST_SCU4                (AST_SCU_BASE + 0x04)
#define PROTECTION_KEY          (0x1688A8A8)

#define INTR_PCIE_L_H_RESET_EN                  (0x1 << 2) 
#define INTR_PCIE_H_L_RESET_EN                  (0x1 << 3)
#define SCU_RESET_MCTP                          (0x1 << 24)
#define INTR_PCIE_L_H_RESET                     (0x1 << 18)


// register
#define	AST_MCTP_CTRL		0x00
#define	AST_MCTP_TX_CMD		0x04
#define	AST_MCTP_RX_CMD		0x08
#define	AST_MCTP_ISR		0x0c
#define	AST_MCTP_IER		0x10
#define	AST_MCTP_EID		0x14
#define AST_MCTP_OBFF		0x18


// AST_MCTP_CTRL
#define MCTP_RX_RDY			(1 << 4)
#define MCTP_TX				1
#define MCTP_MSG_MASK       (1 << 8)

// AST_MCTP_EID
#define MCTP_EP_ID(x)       (x & 0xff)


//AMI
#define BUS_NO(x)			((x & 0xff) << 24)
#define DEV_NO(x)			((x & 0x1f) << 19)
#define FUN_NO(x)			((x & 0x7) << 16)

/*************************************************************************************/
#define MCTP_DESC_SIZE                  4096 * 2        //for tx/rx descript
#define MCTP_TX_BUFF_SIZE               4096
#define MCTP_RX_BUFF_POOL_SIZE          16384
#define MCTP_G6_RX_BUFF_POOL_SIZE       MCTP_RX_BUFF_POOL_SIZE * 4
#define MCTP_TX_FIFO_NUM                1
#define MCTP_G6_TX_FIFO_NUM             8
#define MCTP_G6_RX_DEFAULT_PAYLOAD      64
#define MCTP_G6_TX_DEFAULT_PAYLOAD      64
 
#define MCTP_RX_DESC_BUFF_NUM           8
#define G5_DRAM_BASE_ADDR               0x80000000
/*************************************************************************************/
#define  ASPEED_MCTP_CTRL                0x00
#define  ASPEED_MCTP_CTRL1 		0x1C
#define  MCTP_GET_CUR_CMD_CNT(x)        ((x >> 24) & 0x3f)
#define  MCTP_RX_PCIE_IDLE              BIT(21)
#define  MCTP_RX_DMA_IDLE               BIT(20)
#define  MCTP_TX_PCIE_IDLE              BIT(17)
#define  MCTP_TX_DMA_IDLE               BIT(16)
#define  MCTP_CPL2_ENABLE               BIT(15)
#define  MCTP_MATCH_EID                 BIT(9)
#define  MCTP_RX_CMD_RDY                BIT(4)
#define  MCTP_TX_TRIGGER                BIT(0)
#define  ASPEED_MCTP_TX_CMD              0x04
#define  ASPEED_MCTP_RX_CMD              0x08
#define  ASPEED_MCTP_ISR                 0x0c
#define  ASPEED_MCTP_IER                 0x10
#define  MCTP_MSG_OBFF_STS_CHG          BIT(27)
#define  MCTP_MSG_OBFF_ACTIVE           BIT(26)
#define  MCTP_MSG_OBFF_IDLE             BIT(25)
#define  MCTP_MSG_OBFF_STATE            BIT(24)
#define  MCTP_WAKE_OBFF_ACTIVE          BIT(19)
#define  MCTP_WAKE_POST_OBFF            BIT(18)
#define  MCTP_WAKE_OBFF_STATE           BIT(17)
#define  MCTP_WAKE_OBFF_IDLE            BIT(16)
#define  MCTP_RX_NO_CMD                 BIT(9)
#define  MCTP_RX_COMPLETE               BIT(8)
#define  MCTP_TX_CMD_WRONG              BIT(2)  //ast-g6 mctp
#define  MCTP_TX_LAST                   BIT(1)
#define  MCTP_TX_COMPLETE               BIT(0)
#define ASPEED_MCTP_EID                 0x14
#define ASPEED_MCTP_OBFF_CTRL           0x18

/*************************************************************************************/
//TX CMD desc0 : ast-g4, ast-g5
#define BUS_NO(x)                       ((x & 0xff) << 24)
#define DEV_NO(x)                       ((x & 0x1f) << 19)
#define FUN_NO(x)                       ((x & 0x7) << 16)
#define INT_ENABLE                      BIT(15)

//ast-g5
/* 0: route to RC, 1: route by ID, 2/3: broadcast from RC */
#define G5_ROUTING_TYPE_L(x)            ((x & 0x1) << 14)
#define G5_ROUTING_TYPE_H(x)            (((x & 0x2) >> 1) << 12)
//ast old version
#define ROUTING_TYPE(x)                 ((x & 0x1) << 14)

#define TAG_OWN(x)                      (x << 13)

//bit 12:2 is packet in 4bytes
//ast2400 bit 12 can be use.
//ast2500 bit 12 can't be used. 0: 1024 * 4 = 4096
#define G5_PKG_SIZE(x)                  ((x & 0x3ff) << 2)
#define PKG_SIZE(x)                     ((x & 0x7ff) << 2)

#define PADDING_LEN(x)                  (x & 0x3)
//TX CMD desc1
#define LAST_CMD                        BIT(31)
//ast-g5
#define G5_TX_DATA_ADDR(x)              (((x >> 7) & 0x7fffff) << 8)
//ast old version
#define TX_DATA_ADDR(x)                 (((x >> 6) & 0x7fffff) << 8)
#define DEST_EP_ID(x)                   (x & 0xff)

//RX CMD desc0                                                                                                                                                                                             
#define GET_PKG_LEN(x)                  (((x) >> 24) & 0x7f)
#define GET_SRC_EPID(x)                 (((x) >> 16) & 0xff)
#define GET_ROUTING_TYPE(x)             ((x >> 14) & 0x3) //((x >> 14) & 0x7)
#define GET_SEQ_NO(x)                   (((x) >> 11) & 0x3)
#define GET_MSG_TAG(x)                  (((x) >> 8) & 0x7)//(((x) >> 8) & 0x3)
#define MCTP_SOM                        BIT(7)
#define GET_MCTP_SOM(x)                 (((x) >> 7) & 0x1)
#define MCTP_EOM                        BIT(6)
#define GET_MCTP_EOM(x)                 (((x) >> 6) & 0x1)
#define GET_PADDING_LEN(x)              (((x) >> 4) & 0x3)
#define CMD_UPDATE                      BIT(0)
//RX CMD desc1
#define LAST_CMD                        BIT(31)
#define RX_DATA_ADDR(x)                 ((x) & 0x3fffff80)
 
struct ast_mctp_cmd_desc {
        unsigned int desc0;
        unsigned int desc1;
};

struct pcie_vdm_header {
        u32           length: 10,
                      revd0: 2,
                      attr: 2,
                      ep: 1,
                      td: 1,
                      revd1: 4,
                      tc: 3,
                      revd2: 1,
                      type_routing: 5,
                      fmt: 2,
                      revd3: 1;
        u8            message_code;
        u8            vdm_code: 4,
                      pad_len: 2,
                      tag_revd: 2;
        u16           pcie_req_id;
        u16           vender_id;
        u16           pcie_target_id;
        u8            msg_tag: 3,
                      to: 1,
                      pkt_seq: 2,
                      eom: 1,
                      som: 1;
        u8            src_epid;
        u8            dest_epid;
        u8            header_ver: 4,
                      rsvd: 4;
};

struct ast_mctp_xfer {
        unsigned char *xfer_buff;
        struct pcie_vdm_header header;
};

int create_mctp_packets_queue(void);
int delete_mctp_packets_queue(void);
int multiple_mctp_packets_handler(struct pcie_vdm_header *header, u8 *xfer_buff, u32 payload_len);

#endif /* _AST_MCTP_PCIE_H_ */
