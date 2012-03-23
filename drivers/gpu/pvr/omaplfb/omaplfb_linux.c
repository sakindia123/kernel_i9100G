/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#ifndef AUTOCONF_INCLUDED
#include <linux/config.h>
#endif

#include <linux/version.h>
#include <linux/module.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <asm/io.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32))
#include <plat/vrfb.h>
#include <plat/display.h>
#else
#include <mach/vrfb.h>
#include <mach/display.h>
#endif

#ifdef RELEASE
#include <../drivers/video/omap2/omapfb/omapfb.h>
#undef DEBUG
#else
#undef DEBUG
#include <../drivers/video/omap2/omapfb/omapfb.h>
#endif

#if defined(CONFIG_OUTER_CACHE)  /* Kernel config option */
#include <asm/cacheflush.h>
#define HOST_PAGESIZE			(4096)
#define HOST_PAGEMASK			(~(HOST_PAGESIZE-1))
#define HOST_PAGEALIGN(addr)	(((addr)+HOST_PAGESIZE-1)&HOST_PAGEMASK)
#endif

#if defined(LDM_PLATFORM)
#include <linux/platform_device.h>
#if defined(SGX_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif
#endif

#include <plat/display.h>
#include <plat/dma.h>
#include <linux/wait.h>
#include <mach/tiler.h>

#include "img_defs.h"
#include "servicesext.h"
#include "kerneldisplay.h"
#include "omaplfb.h"
#include "pvrmodule.h"

struct vid_Tiler_dma tiler_dma_tx;
struct oly_mgr_flds ovlDatafld;

extern u32 sec_bootmode;

MODULE_SUPPORTED_DEVICE(DEVNAME);

#if defined(CONFIG_OUTER_CACHE)  /* Kernel config option */
#if defined(__arm__)
static void per_cpu_cache_flush_arm(void *arg)
{
    PVR_UNREFERENCED_PARAMETER(arg);
    flush_cache_all();
}
#endif
#endif

/*
 * Kernel malloc
 * in: ui32ByteSize
 */
void *OMAPLFBAllocKernelMem(unsigned long ui32ByteSize)
{
	void *p;

#if defined(CONFIG_OUTER_CACHE)  /* Kernel config option */
	IMG_VOID *pvPageAlignedCPUPAddr;
	IMG_VOID *pvPageAlignedCPUVAddr;
	IMG_UINT32 ui32PageOffset;
	IMG_UINT32 ui32PageCount;
#endif
	p = kmalloc(ui32ByteSize, GFP_KERNEL);

	if(!p)
		return 0;

#if defined(CONFIG_OUTER_CACHE)  /* Kernel config option */
	ui32PageOffset = (IMG_UINT32) p & (HOST_PAGESIZE - 1);
	ui32PageCount = HOST_PAGEALIGN(ui32ByteSize + ui32PageOffset) / HOST_PAGESIZE;

	pvPageAlignedCPUVAddr = (IMG_VOID *)((IMG_UINT8 *)p - ui32PageOffset);
	pvPageAlignedCPUPAddr = (IMG_VOID*) __pa(pvPageAlignedCPUVAddr);

#if defined(__arm__)
      on_each_cpu(per_cpu_cache_flush_arm, NULL, 1);
#endif
	outer_cache.flush_range((unsigned long) pvPageAlignedCPUPAddr, (unsigned long) ((pvPageAlignedCPUPAddr + HOST_PAGESIZE*ui32PageCount) - 1));
#endif
	return p;
}

/*
 * Kernel free
 * in: pvMem
 */
void OMAPLFBFreeKernelMem(void *pvMem)
{
	kfree(pvMem);
}

/*
 * Here we get the function pointer to get jump table from
 * services using an external function.
 * in: szFunctionName
 * out: ppfnFuncTable
 */
OMAP_ERROR OMAPLFBGetLibFuncAddr (char *szFunctionName,
	PFN_DC_GET_PVRJTABLE *ppfnFuncTable)
{
	if(strcmp("PVRGetDisplayClassJTable", szFunctionName) != 0)
	{
		ERROR_PRINTK("Unable to get function pointer for %s"
			" from services", szFunctionName);
		return OMAP_ERROR_INVALID_PARAMS;
	}
	*ppfnFuncTable = PVRGetDisplayClassJTable;

	return OMAP_OK;
}

#if defined(FLIP_TECHNIQUE_FRAMEBUFFER)
/*
 * Presents the flip in the display with the framebuffer API
 * in: psSwapChain, aPhyAddr
 */
static void OMAPLFBFlipNoLock(OMAPLFB_SWAPCHAIN *psSwapChain,
	unsigned long aPhyAddr)
{
	OMAPLFB_DEVINFO *psDevInfo = (OMAPLFB_DEVINFO *)psSwapChain->pvDevInfo;
	struct fb_info *framebuffer = psDevInfo->psLINFBInfo;

	/* Get the framebuffer physical address base */
	unsigned long fb_base_phyaddr =
		psDevInfo->sSystemBuffer.sSysAddr.uiAddr;

	/* Calculate the virtual Y to move in the framebuffer */
	framebuffer->var.yoffset =
		(aPhyAddr - fb_base_phyaddr) / framebuffer->fix.line_length;
	framebuffer->var.activate = FB_ACTIVATE_FORCE;
	fb_set_var(framebuffer, &framebuffer->var);
}

#elif defined(FLIP_TECHNIQUE_OVERLAY)
/*
 * Presents the flip in the display with the DSS2 overlay API
 * in: psSwapChain, aPhyAddr
 */
static void OMAPLFBFlipNoLock(OMAPLFB_SWAPCHAIN *psSwapChain,
	unsigned long aPhyAddr)
{
	OMAPLFB_DEVINFO *psDevInfo = (OMAPLFB_DEVINFO *)psSwapChain->pvDevInfo;
	struct fb_info * framebuffer = psDevInfo->psLINFBInfo;
	struct omapfb_info *ofbi = FB2OFB(framebuffer);
	unsigned long fb_offset;

	struct fb_var_screeninfo *var = &framebuffer->var;
	int i = 0;
	int rotation = 0;
	unsigned int paddr = 0;
	fb_offset = aPhyAddr - psDevInfo->sSystemBuffer.sSysAddr.uiAddr;

	rotation = (var->rotate + ofbi->rotation[i]) % 4;
	for(i = 0; i < ofbi->num_overlays ; i++)
	{
		struct omap_dss_device *display = NULL;
		struct omap_dss_driver *driver = NULL;
		struct omap_overlay_manager *manager;
		struct omap_overlay *overlay;
		struct omap_overlay_info overlay_info;

		overlay = ofbi->overlays[i];
		manager = overlay->manager;

		overlay->get_overlay_info(overlay, &overlay_info );
		overlay_info.paddr = framebuffer->fix.smem_start + fb_offset;
		overlay_info.vaddr = framebuffer->screen_base + fb_offset;
		//CSR #OMAPS00242911
		if (sec_bootmode!= 2)
			ofbi->paddr2 = overlay_info.paddr;
		if (ofbi->rotation_type ==  OMAP_DSS_ROT_TILER) {
			/* copy from 1d to 2d shall happen only once */
			if(i == 0)
				paddr = copy1DTo2D(overlay_info.paddr);
			overlay_info.paddr = paddr;
		}	
		overlay->set_overlay_info(overlay, &overlay_info);

		if (manager) {
			display = manager->device;
			/* No display attached to this overlay, don't update */
			if (!display)
				continue;
			driver = display->driver;
			manager->apply(manager);
		}

		if (dss_ovl_manually_updated(overlay)) {
			if (driver->sched_update)
				driver->sched_update(display, 0, 0,
							overlay_info.width,
							overlay_info.height);
			else if (driver->update)
				driver->update(display, 0, 0,
							overlay_info.width,
							overlay_info.height);

		}

	}
}

#else
#error No flipping technique selected, please define \
	FLIP_TECHNIQUE_FRAMEBUFFER or FLIP_TECHNIQUE_OVERLAY
#endif

void OMAPLFBFlip(OMAPLFB_SWAPCHAIN *psSwapChain, unsigned long aPhyAddr)
{
	OMAPLFB_DEVINFO *psDevInfo = (OMAPLFB_DEVINFO *)psSwapChain->pvDevInfo;
	struct fb_info *framebuffer = psDevInfo->psLINFBInfo;
	struct omapfb_info *ofbi = FB2OFB(framebuffer);
	struct omapfb2_device *fbdev = ofbi->fbdev;

	omapfb_lock(fbdev);
	OMAPLFBFlipNoLock(psSwapChain, aPhyAddr);
	omapfb_unlock(fbdev);
}

/*
 * Present frame and synchronize with the display to prevent tearing
 * On DSI panels the sync function is used to handle FRAMEDONE IRQ
 * On DPI panels the wait_for_vsync is used to handle VSYNC IRQ
 * in: psDevInfo
 */
void OMAPLFBPresentSync(OMAPLFB_DEVINFO *psDevInfo,
	OMAPLFB_FLIP_ITEM *psFlipItem)
{
	struct fb_info *framebuffer = psDevInfo->psLINFBInfo;
	struct omapfb_info *ofbi = FB2OFB(framebuffer);
	struct omap_dss_device *display;
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omap_dss_driver *driver;
	struct omap_overlay_manager *manager;
	int err = 1;

	omapfb_lock(fbdev);

	display = fb2display(framebuffer);
	/* The framebuffer doesn't have a display attached, just bail out */
	if (!display) {
		omapfb_unlock(fbdev);
		return;
	}

	driver = display->driver;
	manager = display->manager;

	if (driver && driver->sync &&
		driver->get_update_mode(display) == OMAP_DSS_UPDATE_MANUAL) {
		/* Wait first for the DSI bus to be released then update */
		err = driver->sync(display);
		OMAPLFBFlipNoLock(psDevInfo->psSwapChain,
			(unsigned long)psFlipItem->sSysAddr->uiAddr);
	} else if (manager && manager->wait_for_vsync) {
		/*
		 * Update the video pipelines registers then wait until the
		 * frame is shown with a VSYNC
		 */
		OMAPLFBFlipNoLock(psDevInfo->psSwapChain,
			(unsigned long)psFlipItem->sSysAddr->uiAddr);
		err = manager->wait_for_vsync(manager);
	}

	if (err)
		WARNING_PRINTK("Unable to sync with display %u!",
			psDevInfo->uDeviceID);

	omapfb_unlock(fbdev);
}

#if defined(LDM_PLATFORM)

static volatile OMAP_BOOL bDeviceSuspended;

static int omaplfb_probe(struct platform_device *pdev)
{
	struct omaplfb_device *odev;

	odev = kzalloc(sizeof(*odev), GFP_KERNEL);

	if (!odev)
		return -ENOMEM;

	if (OMAPLFBInit(odev) != OMAP_OK) {
		dev_err(&pdev->dev, "failed to setup omaplfb\n");
		kfree(odev);
		return -ENODEV;
	}

	odev->dev = &pdev->dev;
	platform_set_drvdata(pdev, odev);
	omaplfb_create_sysfs(odev);

	return 0;
}

static int omaplfb_remove(struct platform_device *pdev)
{
	struct omaplfb_device *odev;

	odev = platform_get_drvdata(pdev);

	omaplfb_remove_sysfs(odev);

	if (OMAPLFBDeinit() != OMAP_OK)
		WARNING_PRINTK("Driver cleanup failed");

	kfree(odev);

	return 0;
}

/*
 * Common suspend driver function
 * in: psSwapChain, aPhyAddr
 */
static void OMAPLFBCommonSuspend(void)
{
	if (bDeviceSuspended)
	{
		DEBUG_PRINTK("Driver is already suspended");
		return;
	}

	OMAPLFBDriverSuspend();
	bDeviceSuspended = OMAP_TRUE;
}

#if 0
/*
 * Function called when the driver is requested to release
 * in: pDevice
 */
static void OMAPLFBDeviceRelease_Entry(struct device unref__ *pDevice)
{
	DEBUG_PRINTK("Requested driver release");
	OMAPLFBCommonSuspend();
}

static struct platform_device omaplfb_device = {
	.name = DEVNAME,
	.id = -1,
	.dev = {
		.release = OMAPLFBDeviceRelease_Entry
	}
};
#endif

#if defined(SGX_EARLYSUSPEND) && defined(CONFIG_HAS_EARLYSUSPEND)

/*
 * Android specific, driver is requested to be suspended
 * in: ea_event
 */
static void OMAPLFBDriverSuspend_Entry(struct early_suspend *ea_event)
{
	DEBUG_PRINTK("Requested driver suspend");
	OMAPLFBCommonSuspend();
}

/*
 * Android specific, driver is requested to be suspended
 * in: ea_event
 */
static void OMAPLFBDriverResume_Entry(struct early_suspend *ea_event)
{
	DEBUG_PRINTK("Requested driver resume");
	OMAPLFBDriverResume();
	bDeviceSuspended = OMAP_FALSE;
}

static struct platform_driver omaplfb_driver = {
	.driver = {
		.name = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.probe = omaplfb_probe,
	.remove = omaplfb_remove,
};

static struct early_suspend omaplfb_early_suspend = {
	.suspend = OMAPLFBDriverSuspend_Entry,
	.resume = OMAPLFBDriverResume_Entry,
	.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING,
};

#else /* defined(SGX_EARLYSUSPEND) && defined(CONFIG_HAS_EARLYSUSPEND) */

/*
 * Function called when the driver is requested to be suspended
 * in: pDevice, state
 */
static int OMAPLFBDriverSuspend_Entry(struct platform_device unref__ *pDevice,
	pm_message_t unref__ state)
{
	DEBUG_PRINTK("Requested driver suspend");
	OMAPLFBCommonSuspend();
	return 0;
}

/*
 * Function called when the driver is requested to resume
 * in: pDevice
 */
static int OMAPLFBDriverResume_Entry(struct platform_device unref__ *pDevice)
{
	DEBUG_PRINTK("Requested driver resume");
	OMAPLFBDriverResume();
	bDeviceSuspended = OMAP_FALSE;
	return 0;
}

/*
 * Function called when the driver is requested to shutdown
 * in: pDevice
 */
static IMG_VOID OMAPLFBDriverShutdown_Entry(
	struct platform_device unref__ *pDevice)
{
	DEBUG_PRINTK("Requested driver shutdown");
	OMAPLFBCommonSuspend();
}

static struct platform_driver omaplfb_driver = {
	.driver = {
		.name = DRVNAME,
		.owner  = THIS_MODULE,
	},
	.probe = omaplfb_probe,
	.remove = omaplfb_remove,
	.suspend = OMAPLFBDriverSuspend_Entry,
	.resume	= OMAPLFBDriverResume_Entry,
	.shutdown = OMAPLFBDriverShutdown_Entry,
};

#endif /* defined(SGX_EARLYSUSPEND) && defined(CONFIG_HAS_EARLYSUSPEND) */

#endif /* defined(LDM_PLATFORM) */

/*
 * Driver init function
 */
static int __init OMAPLFB_Init(void)
{
#if defined(LDM_PLATFORM)
	DEBUG_PRINTK("Registering platform driver");
	if (platform_driver_register(&omaplfb_driver))
		return -ENODEV;
#if 0
	DEBUG_PRINTK("Registering device driver");
	if (platform_device_register(&omaplfb_device))
	{
		WARNING_PRINTK("Unable to register platform device");
		platform_driver_unregister(&omaplfb_driver);
		if(OMAPLFBDeinit() != OMAP_OK)
			WARNING_PRINTK("Driver cleanup failed\n");
		return -ENODEV;
	}
#endif

#if defined(SGX_EARLYSUSPEND) && defined(CONFIG_HAS_EARLYSUSPEND)
	register_early_suspend(&omaplfb_early_suspend);
	DEBUG_PRINTK("Registered early suspend support");
#endif

#endif
	return 0;
}

/*
 * Driver exit function
 */
static IMG_VOID __exit OMAPLFB_Cleanup(IMG_VOID)
{    
#if defined(LDM_PLATFORM)
#if 0
	DEBUG_PRINTK(format,...)("Removing platform device");
	platform_device_unregister(&omaplfb_device);
#endif
	DEBUG_PRINTK("Removing platform driver");
	platform_driver_unregister(&omaplfb_driver);
#if defined(SGX_EARLYSUSPEND) && defined(CONFIG_HAS_EARLYSUSPEND)
	DEBUG_PRINTK("Removed early suspend support");
	unregister_early_suspend(&omaplfb_early_suspend);
#endif
#endif
}

int tiler_fb_mem_init(struct fb_info *fbi){

   enum tiler_fmt fmt;
   int rc = -1;
   int cnt;
   int bytespp;
   int stride;
   struct fb_var_screeninfo *var = &fbi->var;
   ovlDatafld.width = var->xres;
   ovlDatafld.height = var->yres;
   switch (var->bits_per_pixel) {
       case 16:
       fmt = TILFMT_16BIT;
       bytespp = 2;
       break;

       case 32:
       fmt = TILFMT_32BIT;
       bytespp = 4;
       break;

       default:
           printk(KERN_ERR "DSSMGR-OLY: Invalid Mirror Color Format\n");
           goto failed;
           break;
   }
   cnt = 2;
   tiler_alloc_packed(&cnt, fmt, ovlDatafld.width, ovlDatafld.height,
                   (void **) ovlDatafld.copy_buf,
                   (void **) ovlDatafld.copy_buf_alloc, 1);
   	if (cnt != 2)
       printk(KERN_ERR "DSSMGR-OLY: Failed to allocate memory Alloc\n");
   	else
       rc = 0;
       printk("Memory allocation finished \n");

   	ovlDatafld.copy_buf_idx = 0;
   	ovlDatafld.dma_en = (ovlDatafld.width * bytespp) / 4; /* 32 bit ES */
   	ovlDatafld.dma_fn = ovlDatafld.height;
   	ovlDatafld.dma_src_fi = (var->xres * bytespp) - (ovlDatafld.dma_en * 4) + 1;

	stride = tiler_stride(tiler_get_natural_addr((void *)ovlDatafld.copy_buf[0]));
   ovlDatafld.dma_dst_fi = stride - (ovlDatafld.dma_en * 4) + 1;

failed:
	return rc;
}

void omap_vout_Tiler_dma_tx_callback(int lch, u16 ch_status, void *data)
{
	struct vid_Tiler_dma *t = (struct vid_Tiler_dma *) data;
	t->tx_status = 1;
	wake_up_interruptible(&t->wait);
}

void dma_init(){
	int ret = -1;
	tiler_dma_tx.dev_id = OMAP_DMA_NO_DEVICE;
	tiler_dma_tx.dma_ch = -1;
	tiler_dma_tx.req_status = DMA_CHAN_ALLOTED;

   ret = omap_request_dma(tiler_dma_tx.dev_id, "Tiler DMA TX",
           omap_vout_Tiler_dma_tx_callback,
           (void *) &tiler_dma_tx, &tiler_dma_tx.dma_ch);

   if (ret < 0) {
       tiler_dma_tx.req_status = DMA_CHAN_NOT_ALLOTED;
       printk("error in DMA init");
   }
   init_waitqueue_head(&tiler_dma_tx.wait);
}

//Need to pass src address from where we have to start copy.
unsigned int copy1DTo2D(int phyadd){
	
	u32 src_paddr;
	u32 dst_paddr;
	ovlDatafld.copy_buf_idx = (ovlDatafld.copy_buf_idx) ? 0 : 1;
	src_paddr = phyadd; //data->info[s_idx].paddr;
	dst_paddr = ovlDatafld.copy_buf[ovlDatafld.copy_buf_idx];
	
	omap_set_dma_transfer_params(tiler_dma_tx.dma_ch, OMAP_DMA_DATA_TYPE_S32,
			ovlDatafld.dma_en, ovlDatafld.dma_fn, OMAP_DMA_SYNC_ELEMENT,tiler_dma_tx.dev_id, 0x0);
	
	omap_set_dma_src_params(tiler_dma_tx.dma_ch, 0, OMAP_DMA_AMODE_DOUBLE_IDX,
	        src_paddr, 1, ovlDatafld.dma_src_fi);
	
	omap_set_dma_src_data_pack(tiler_dma_tx.dma_ch, 1); //venky
	omap_set_dma_src_burst_mode(tiler_dma_tx.dma_ch, OMAP_DMA_DATA_BURST_16);
	omap_set_dma_dest_params(tiler_dma_tx.dma_ch, 0, OMAP_DMA_AMODE_DOUBLE_IDX,
	        dst_paddr, 1, ovlDatafld.dma_dst_fi);
	omap_set_dma_dest_data_pack(tiler_dma_tx.dma_ch, 1); //venky
	omap_set_dma_dest_burst_mode(tiler_dma_tx.dma_ch, OMAP_DMA_DATA_BURST_16);
	omap_dma_set_prio_lch(tiler_dma_tx.dma_ch,DMA_CH_PRIO_HIGH,DMA_CH_PRIO_HIGH);//venky - Set read and write priority
	omap_dma_set_prefetch(tiler_dma_tx.dma_ch,1);//venky:Setting prefetch
	
	omap_dma_set_global_params(DMA_DEFAULT_ARB_RATE, 0xFF, 0);//Venky - Increased FIFO Depth
	
	omap_start_dma(tiler_dma_tx.dma_ch);
	
	wait_event_interruptible_timeout(tiler_dma_tx.wait, tiler_dma_tx.tx_status,
	                        DMA_TX_TIMEOUT);
	
	if (!tiler_dma_tx.tx_status) {
	    printk(KERN_WARNING "DSSMGR-OLY: DMA timeout\n");
	omap_stop_dma(tiler_dma_tx.dma_ch);
	return -EIO;
	}
	return dst_paddr;
}





late_initcall(OMAPLFB_Init);
module_exit(OMAPLFB_Cleanup);

