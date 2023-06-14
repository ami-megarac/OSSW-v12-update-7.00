diff -Naur uboot_org/oem/ami/fwupdate/fwupdate.c uboot/oem/ami/fwupdate/fwupdate.c
--- uboot_org/oem/ami/fwupdate/fwupdate.c	2023-03-28 20:10:00.483301153 +0530
+++ uboot/oem/ami/fwupdate/fwupdate.c	2023-03-28 20:14:31.156430336 +0530
@@ -184,7 +184,7 @@
     FMH     *pFMH = NULL;
     MODULE_INFO *pmod = NULL;
     char        sectionname[10] = {0}, conffound = 0;
-    unsigned char   *TempBuf = (unsigned char *)YAFU_TEMP_SECTOR_BUFFER;
+    unsigned char   *TempBuf = (unsigned char *)YAFU_RAM_IMAGE_TEMP_SECTOR_BUFFER;
     int i,ret = -1;
 
     for(i = 0; i < (CONFIG_SPX_FEATURE_GLOBAL_USED_FLASH_SIZE/CONFIG_SPX_FEATURE_GLOBAL_ERASE_BLOCK_SIZE); i++)
@@ -214,7 +214,7 @@
             }
         }
         offset += CONFIG_SPX_FEATURE_GLOBAL_ERASE_BLOCK_SIZE;
-        
+        memset(TempBuf,0,CONFIG_SPX_FEATURE_GLOBAL_ERASE_BLOCK_SIZE);
     }
 
     return ret;
@@ -224,8 +224,8 @@
 {
     flash_info_t    *pFlashInfo;
     int bank = 0,sector,retries = 0;
-    unsigned char   *TempBuf = (unsigned char*)YAFU_TEMP_SECTOR_BUFFER;
-    unsigned char   *pRamAddr;
+    unsigned char   *TempBuf = (unsigned char*)YAFU_SPI_IMAGE_TEMP_SECTOR_BUFFER;
+    unsigned char   *pRamAddr = (unsigned char*) YAFU_RAM_IMAGE_TEMP_SECTOR_BUFFER;
     unsigned long   cnt,confstartaddr = 0,confsize = 0, FlashAddr= 0;
     char        *argv[4];
 
@@ -337,6 +337,8 @@
         if(retries == 0)
             return -1;
         
+        memset(TempBuf,0,CONFIG_SPX_FEATURE_GLOBAL_ERASE_BLOCK_SIZE);
+        memset(pRamAddr,0,CONFIG_SPX_FEATURE_GLOBAL_ERASE_BLOCK_SIZE);
     }
     
     return 0;
@@ -561,7 +563,7 @@
 	volatile int 	SectorToUpdate;
 	unsigned long 	addr;
 	unsigned long 	cnt,BlkToUpgrade;
-	unsigned char	*TempBuf = (unsigned char*)YAFU_TEMP_SECTOR_BUFFER;
+	unsigned char	*TempBuf = (unsigned char*)YAFU_SPI_IMAGE_TEMP_SECTOR_BUFFER;
 	unsigned char	*src,*pRamAddress=0;
 	flash_info_t 	*pFlashInfo;
 	int bank = 0, prev_sector = 0 ,sector_number = 0;
diff -Naur uboot_org/oem/ami/ipmi/cmdhandler.c uboot/oem/ami/ipmi/cmdhandler.c
--- uboot_org/oem/ami/ipmi/cmdhandler.c	2023-03-28 20:10:00.487301169 +0530
+++ uboot/oem/ami/ipmi/cmdhandler.c	2023-03-28 20:21:36.050430709 +0530
@@ -901,7 +901,7 @@
 if( ActivateFlashStatus == 0x01)
 {
    
-    char *BootVal = (char *)YAFU_TEMP_SECTOR_BUFFER;	
+    char *BootVal = (char *)YAFU_SPI_IMAGE_TEMP_SECTOR_BUFFER;	
 	
     AMIYAFUSetBootConfigRes_T* pAMIYAFUSetBootConfig = (AMIYAFUSetBootConfigRes_T*)pRes;
   
@@ -967,7 +967,7 @@
 {
    // int RetVal;
     int BootVarlen =0;	
-    char *Buffer = (char *)YAFU_TEMP_SECTOR_BUFFER;	
+    char *Buffer = (char *)YAFU_SPI_IMAGE_TEMP_SECTOR_BUFFER;	
 
     AMIYAFUGetBootVarsRes_T* pAMIYAFUGetBootVars = (AMIYAFUGetBootVarsRes_T*)pRes;	 
     pAMIYAFUGetBootVars->VarCount = 0X00;
