diff -Naur uboot_org/drivers/spiflash/gigadevice.c uboot/drivers/spiflash/gigadevice.c
--- uboot_org/drivers/spiflash/gigadevice.c	2022-03-23 14:33:31.160998905 +0800
+++ uboot/drivers/spiflash/gigadevice.c	2022-03-23 14:40:54.274581903 +0800
@@ -30,6 +30,7 @@
 static struct spi_flash_info gigadevice_data [] =
 {
 	{ "GigaDevice GD25Q256D/GD25B256D"   , 0xC8, 0x1940, 0x001F001F, 0x2000000, 50 * 1000000,  1, 0x02, {{ 0, 64  * 1024, 512 },} },
+	{ "GigaDevice GD25B512M"   , 0xC8, 0x1a47, 0x001B001B, 0x4000000, 50 * 1000000,  1, 0x02, {{ 0, 64  * 1024, 1024 },} },
 };
 
 static
