#ifndef _I2C_PCA954X_H_
#define _I2C_PCA954X_H_

#ifdef LINUX_KERNEL_BUILD

extern void *pca954x_addChip(u8 bus, u8 addr, u8 mask, u8 rstGpio, int type, u8 *chanNumbering, u8 maxChanIndex, u16 *modifiers, u8 numModifiers );
extern void pca954x_resetChip(void *PlatformDrv);
extern void pca954x_removeChip(void *PlatformDrv);
extern u32 pca954x_getType(char *name);
extern void pca954x_setMaskChip(void *PlatformDrv, u8 mask);
extern int pca954x_getStatus (void *PlatformDrv, char *buf);
extern void pca954x_notifyDeselectChan(void *PlatformDrv);

#endif

#endif //_I2C_PCA954X_H_
