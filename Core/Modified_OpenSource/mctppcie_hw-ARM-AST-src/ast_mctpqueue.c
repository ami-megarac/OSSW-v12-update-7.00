#include<linux/slab.h>
#include "ast_mctppcie.h"

#define MAX_MCTP_DATABUF_SIZE (8*1024)
#define MAX_QUEUE_COUNT 8

typedef struct
{           
    unsigned char  flag;//0:IDLE 1:Got first packet 2:Got middile packet 3:Got last packet
    unsigned char  expected_seq;
    unsigned short expected_eid;
    unsigned int   write_offset;
    unsigned int   remaining_size;
    unsigned char  *buffer;
}mctp_packets_queue_t;
 
static mctp_packets_queue_t MCTPQueue[MAX_QUEUE_COUNT];
static int alloc_mctp_packets_queue(mctp_packets_queue_t *pMCTPQueue,unsigned int size)
{    
    pMCTPQueue->buffer=kzalloc(size,sizeof(unsigned char));
    if(pMCTPQueue->buffer==NULL)
    {       
        printk("failed to malloc mctp packets queue\n");
        return (-1);                                                                                 
    }       


    pMCTPQueue->remaining_size=size;
    return 0;
}  

int create_mctp_packets_queue(void)
{                        
    int i=0,ret=0;       
    memset(MCTPQueue,0,sizeof(MCTPQueue));
    for(i=0;i< MAX_QUEUE_COUNT;i++) 
    {                    
        ret=alloc_mctp_packets_queue(&MCTPQueue[i],(MAX_MCTP_DATABUF_SIZE));
        if(ret < 0)      
        {                
            return (-1); 
        }                
        //else
        //    printk("Q[%d] successfully\n",i);
    
    }                    
    
    return 0;            
}                        
   
int delete_mctp_packets_queue(void)
{
    int i=0;       
    for(i=0;i< MAX_QUEUE_COUNT;i++) 
    {                    
        if(MCTPQueue[i].buffer!=NULL)
        {
            kfree(MCTPQueue[i].buffer);
            MCTPQueue[i].buffer=NULL;
        }
    }                    

    memset(MCTPQueue,0,sizeof(mctp_packets_queue_t)*MAX_QUEUE_COUNT);
    return 0;
}

static void clear_mctp_queue(mctp_packets_queue_t *pMCTPQueue)
{                        
    pMCTPQueue->write_offset=0;
    pMCTPQueue->flag=0;  
    pMCTPQueue->expected_eid=0;
    pMCTPQueue->remaining_size=(MAX_MCTP_DATABUF_SIZE+1);
}                        

int multiple_mctp_packets_handler(struct pcie_vdm_header *header, u8 *xfer_buff, u32 payload_len)
{
    int     retval=0;   
     
    if(header==NULL || xfer_buff==NULL)
    {   
        printk("NULL parameter\n");
        return (-1);    
    }
    
    //printk("MP TYPE:%x TAG:%d SOM:%d EOM:%d SEQ=%d SRC EID:%x LENGTH=%d\n", header->type_routing,header->msg_tag, header->som, header->eom, header->pkt_seq, header->src_epid,payload_len);
        
    if(!(header->som) && !(header->eom))
    {   
        //middle packet 
        if(MCTPQueue[header->msg_tag].remaining_size < (payload_len))
        {               
            printk("MCTP WARNING: The size of data received from PCIE is over Queue[%d] ",header->msg_tag);
            goto DUMP_MCTP;
        }       
        
        if(MCTPQueue[header->msg_tag].expected_seq!=header->pkt_seq)
        {               
            printk("MCTP WARNING: MCTP Packet expected seq not matched, drop it");
            goto DUMP_MCTP;
        }       
                        
        if(MCTPQueue[header->msg_tag].expected_eid!=header->src_epid)
        {               
            printk("MCTP WARNING: MCTP Packet SRC EID not matched, drop it");
            goto DUMP_MCTP;
        }

        MCTPQueue[header->msg_tag].flag=2;
        MCTPQueue[header->msg_tag].expected_seq=((header->pkt_seq+1) > 3) ? 0 : (header->pkt_seq+1);
        memcpy(&MCTPQueue[header->msg_tag].buffer[MCTPQueue[header->msg_tag].write_offset],xfer_buff,(payload_len));
        MCTPQueue[header->msg_tag].write_offset+=payload_len;
        MCTPQueue[header->msg_tag].remaining_size-=payload_len;
        goto END;
    }
    else if(!(header->som) && (header->eom))
    {
        //last packet
        if(MCTPQueue[header->msg_tag].remaining_size < (payload_len))
        {
            printk("MCTP WARNING: The size of data received from PCIE is over Queue[%d] buffer", header->msg_tag);
            goto DUMP_MCTP;
        }

        if(MCTPQueue[header->msg_tag].expected_seq!=header->pkt_seq)
        {
            printk("MCTP WARNING: MCTP Packet expected seq not matched, drop it");
            goto DUMP_MCTP;
        }

        if(MCTPQueue[header->msg_tag].expected_eid!=header->src_epid)
        {
            printk("MCTP WARNING: MCTP Packet SRC EID not matched, drop it");
            goto DUMP_MCTP;
        }

        MCTPQueue[header->msg_tag].flag=3;
        MCTPQueue[header->msg_tag].expected_seq=((header->pkt_seq+1) > 3) ? 0 : (header->pkt_seq+1);
        memcpy(&MCTPQueue[header->msg_tag].buffer[MCTPQueue[header->msg_tag].write_offset],xfer_buff,(payload_len));
        MCTPQueue[header->msg_tag].write_offset+=payload_len;
        MCTPQueue[header->msg_tag].remaining_size-=payload_len;
        //pad_len = header->pad_len;//p_pcie_vdm_header->mctp_pcie_hdr.Tag & 0x30;
    }
    else if((header->som) && !(header->eom))
    {                    
        //first packet   
        if(MCTPQueue[header->msg_tag].write_offset!=0||MCTPQueue[header->msg_tag].flag!=0)
        {                
            printk("MCTP WARNING: Overwrite Queue[%d], there are multiple MCTP Packets with the same message tag from EID:%x ",header->msg_tag, header->src_epid);
        }                
                         
        clear_mctp_queue(&MCTPQueue[header->msg_tag]);
                         
        MCTPQueue[header->msg_tag].flag=1;
                         
        MCTPQueue[header->msg_tag].expected_eid= header->src_epid;
        MCTPQueue[header->msg_tag].expected_seq=((header->pkt_seq+1) > 3) ? 0 : (header->pkt_seq+1);
        memcpy(&MCTPQueue[header->msg_tag].buffer[MCTPQueue[header->msg_tag].write_offset], xfer_buff,(/*PCIE_VDM_HEADER_LENGTH +*/ payload_len));
        MCTPQueue[header->msg_tag].write_offset+=payload_len;
        MCTPQueue[header->msg_tag].remaining_size-=payload_len;//(PCIE_VDM_HEADER_LENGTH + payload_len);
        goto END;        
    }                    
    else                 
    {                    
        printk("Unknow mctp packet. drop it");
        goto DUMP_MCTP;  
    }                    
       
    if(MCTPQueue[header->msg_tag].flag!=3) 
    {     
        printk("Queue[%d].flag =%d has not receive the last packet",header->msg_tag,MCTPQueue[header->msg_tag].flag);
        goto END;
    }     
    
    if(MCTPQueue[header->msg_tag].write_offset > MAX_MCTP_DATABUF_SIZE)
    {     
        printk("MCTP WARNING: MCTP Size bigger than MAX_MCTP_DATABUF_SIZE\n");
        clear_mctp_queue(&MCTPQueue[header->msg_tag]);
        goto DUMP_MCTP;
    }     
    
    memcpy(xfer_buff,MCTPQueue[header->msg_tag].buffer,MCTPQueue[header->msg_tag].write_offset);
    retval = MCTPQueue[header->msg_tag].write_offset;
    //printk("Multi Packets of received length : %d\n", MCTPQueue[header->msg_tag].write_offset);
    clear_mctp_queue(&MCTPQueue[header->msg_tag]);
                 
    return retval;
   
DUMP_MCTP:
    retval=-1;
    printk("MCTP WARNING: MCTP MSG TAG:%d SOM:%x EOM:%x PKTSEQ:%x SRC EID:%x", header->msg_tag, header->som,header->eom, header->pkt_seq, header->src_epid);
    printk("MCTP WARNING: QUEUE[%d]: Expected PKTSEQ:%x, Expected EID:%x Used:0x%xh Free:0x%xh",header->msg_tag,MCTPQueue[header->msg_tag].expected_seq,MCTPQueue[header->msg_tag].expected_eid,MCTPQueue[header->msg_tag].write_offset,MCTPQueue[header->msg_tag].remaining_size);
                         
END:

    return retval; 
}

