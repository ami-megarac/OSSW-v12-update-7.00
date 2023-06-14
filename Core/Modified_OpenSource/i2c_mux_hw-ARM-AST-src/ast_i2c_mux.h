#ifndef _I2C_PILOT_H_
#define _I2C_PILOT_H_

#ifdef LINUX_KERNEL_BUILD

extern void *pilot_addChip(u8 bus, u8 addr, u8 mask, u8 rstGpio, int type, u8 *chanNumbering, u8 maxChanIndex, u16 *modifiers, u8 numModifiers );
extern void pilot_removeChip(void *pPlatformDevice);
extern void pilot_resetChip(void *pPlatformDevice);
extern u32 pilot_getType(char *name);
extern void pilot_setMaskChip(void *pPlatformDevice, u8 mask);
extern int pilot_getStatus (void *pPlatformDevice, char *buf);
extern void pilot_notifyDeselectChan(void *pPlatformDevice);

#endif

#endif //_I2C_PILOT_H_
