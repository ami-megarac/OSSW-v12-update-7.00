--- a/net/bootp.c	2023-04-18 15:31:18.750683998 +0530
+++ b/net/bootp.c	2023-04-18 16:27:24.068327420 +0530
@@ -69,6 +69,7 @@
 {
 	struct Bootp_t *bp = (struct Bootp_t *) pkt;
 	int retval = 0;
+	ulong bp_id = bp->bp_id;
 
 	if (dest != PORT_BOOTPC || src != PORT_BOOTPS)
 		retval = -1;
@@ -84,7 +85,7 @@
 		retval = -4;
 	else if (bp->bp_hlen != HWL_ETHER)
 		retval = -5;
-	else if (NetReadLong((ulong *)&bp->bp_id) != BootpID)
+	else if (NetReadLong((ulong *)&bp_id) != BootpID)
 		retval = -6;
 
 	debug("Filtering pkt = %d\n", retval);
@@ -664,6 +665,7 @@
 	NetWriteIP(&bp->bp_giaddr, 0);
 	memcpy(bp->bp_chaddr, NetOurEther, 6);
 	copy_filename(bp->bp_file, BootFile, sizeof(bp->bp_file));
+	ulong bp_id = bp->bp_id;
 
 	/* Request additional information from the BOOTP/DHCP server */
 #if defined(CONFIG_CMD_DHCP)
@@ -682,7 +684,7 @@
 		| (ulong)NetOurEther[5];
 	BootpID += get_timer(0);
 	BootpID	 = htonl(BootpID);
-	NetCopyLong(&bp->bp_id, &BootpID);
+	NetCopyLong(&bp_id, &BootpID);
 
 	/*
 	 * Calculate proper packet lengths taking into account the
@@ -846,6 +848,8 @@
 	bp->bp_hlen = HWL_ETHER;
 	bp->bp_hops = 0;
 	bp->bp_secs = htons(get_timer(0) / 1000);
+	ulong bp_id = bp->bp_id;
+	ulong bp_offer_bp_id = bp_offer->bp_id;
 	/* Do not set the client IP, your IP, or server IP yet, since it
 	 * hasn't been ACK'ed by the server yet */
 
@@ -861,7 +865,7 @@
 	 * ID is the id of the OFFER packet
 	 */
 
-	NetCopyLong(&bp->bp_id, &bp_offer->bp_id);
+	NetCopyLong(&bp_id, &bp_offer_bp_id);
 
 	/*
 	 * Copy options from OFFER packet if present
