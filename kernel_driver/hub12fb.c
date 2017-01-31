/*
 * linux/drivers/video/hub12fb.c -- Frame buffer driver for "HUB12" based
 * monochromatic LED panels.
 *
 * based on skeletonfb.c by James Simmons and Geert Uytterhoeven
 * inspired by and borrowing from st7735fb.c by Matt Porter
 *
 * Copyright (C) 2013 Darren Garnier <dgarnier@reinrag.net>
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/gfp.h>

#include <linux/gpio.h>
#include <linux/spi/spi.h>
#include <linux/dma-mapping.h>
#include <linux/hrtimer.h>
#include <linux/backlight.h>

#include <linux/spi/hub12fb.h>

/*
 * Driver data
 */

#define DRIVER_NAME HUB12FB_MODALIAS

#define HUB12_MAX_FREQ 7500000

struct hub12_par {
	struct hub12fb_platform_data	pdata;
	struct spi_device *		spi;
	struct fb_info *		info;
	void *				fb_buffer;
	size_t				bsize;
	void *				hsync_buf[4];
	dma_addr_t			hsync_dma[4];
	unsigned			hsync_length;
	struct spi_message		message;
	struct spi_transfer		transfer;
	struct completion		hsync_done;
	struct hrtimer			hsync_timer;
	struct hrtimer			ledon_timer;
	ktime_t				hsync_period;
	ktime_t				ledon_period;
	int				hsync_timeout;
	int				blank;
	unsigned			running       :1;
	int				i_scan;
	u32				pseudo_palette[16];
};

static const struct hub12fb_platform_data default_platform_data = {
	.gpio = {
		.enable = 18,
		.latch  = 17,
		.a      = 22,
		.b      = 27,
	},
	.width      = 32,
	.height     = 16,
	.refresh    = 60,
	.bpp	    = 1,
	.brightness = 10,
};

static struct spi_board_info board_info = {
	.modalias	= HUB12FB_MODALIAS,
	.platform_data	= &default_platform_data,
	.mode		= SPI_MODE_0,
	.max_speed_hz	= HUB12_MAX_FREQ,
	.bus_num	= 0,
	.chip_select	= 0,
};

#define DECLARE_HUB12_MODE(hz, nx, ny) \
	{ .xres = 32*nx, .yres = 16*ny, .refresh=hz, \
	.pixclock = 1000 * KHZ2PICOS(512 * hz * nx * ny) }

static const struct fb_videomode hub12fb_modes[] = {
	DECLARE_HUB12_MODE(60,1,1), DECLARE_HUB12_MODE(100,1,1),
	DECLARE_HUB12_MODE(60,2,1), DECLARE_HUB12_MODE(60,1,2),
	DECLARE_HUB12_MODE(60,2,2), DECLARE_HUB12_MODE(60,2,2),
	DECLARE_HUB12_MODE(60,3,2), DECLARE_HUB12_MODE(60,4,2),
	DECLARE_HUB12_MODE(60,5,2), DECLARE_HUB12_MODE(60,6,2)
};

/*
 * Here ,we de,fine the default structs fb_fix_screeninfo and fb_var_screeninfo
 * if we don't use modedb. If we do use modedb see hub12fb_init how to use it
 * to get a fb_var_screeninfo. Otherwise define a default var as well.
 */
static const struct fb_fix_screeninfo hub12fb_fix __devinitconst = {
	.id 		= DRIVER_NAME,
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_MONO10,
	.xpanstep	= 0,
	.ypanstep	= 0,
	.ywrapstep	= 0,
	.accel =	FB_ACCEL_NONE,
};

static const u32 hub12fb_info_flags = FBINFO_DEFAULT | FBINFO_READS_FAST |
		FBINFO_VIRTFB | FBINFO_HWACCEL_IMAGEBLIT | FBINFO_HWACCEL_FILLRECT |
		FBINFO_HWACCEL_COPYAREA;


static char *mode __devinitdata = NULL;
static int spi[3] __devinitdata = {-1,0,HUB12_MAX_FREQ}; /* no default bus */
static int gpio[4] __devinitdata = {-1,-1,-1,-1}; /* overrides if not -1 */

static u16 red2[]   __read_mostly = { 0x0000, 0xFFFF};
static u16 green2[] __read_mostly = { 0x0000, 0x0000};
static u16 blue2[]  __read_mostly = { 0x0000, 0x0000};
static const struct fb_cmap hub12fb_cmap_2_colors = {
    .len=2, .red=red2, .green=green2, .blue=blue2
};

static const u8 palette_1bit[256] = {[0]=1,[1]=0};

/*
 * def bitreverse(x):
 *     x = ((x & 0xaa)>>1) | ((x & 0x55)<<1)
 *     x = ((x & 0xcc)>>2) | ((x & 0x33)<<2)
 *     x = ((x & 0xf0)>>4) | ((x & 0x0f)<<4)
 *     return x
 */

static const unsigned char bit_reverse_inverse[] = {
	0xff, 0x7f, 0xbf, 0x3f, 0xdf, 0x5f, 0x9f, 0x1f,
	0xef, 0x6f, 0xaf, 0x2f, 0xcf, 0x4f, 0x8f, 0x0f,
	0xf7, 0x77, 0xb7, 0x37, 0xd7, 0x57, 0x97, 0x17,
	0xe7, 0x67, 0xa7, 0x27, 0xc7, 0x47, 0x87, 0x07,
	0xfb, 0x7b, 0xbb, 0x3b, 0xdb, 0x5b, 0x9b, 0x1b,
	0xeb, 0x6b, 0xab, 0x2b, 0xcb, 0x4b, 0x8b, 0x0b,
	0xf3, 0x73, 0xb3, 0x33, 0xd3, 0x53, 0x93, 0x13,
	0xe3, 0x63, 0xa3, 0x23, 0xc3, 0x43, 0x83, 0x03,
	0xfd, 0x7d, 0xbd, 0x3d, 0xdd, 0x5d, 0x9d, 0x1d,
	0xed, 0x6d, 0xad, 0x2d, 0xcd, 0x4d, 0x8d, 0x0d,
	0xf5, 0x75, 0xb5, 0x35, 0xd5, 0x55, 0x95, 0x15,
	0xe5, 0x65, 0xa5, 0x25, 0xc5, 0x45, 0x85, 0x05,
	0xf9, 0x79, 0xb9, 0x39, 0xd9, 0x59, 0x99, 0x19,
	0xe9, 0x69, 0xa9, 0x29, 0xc9, 0x49, 0x89, 0x09,
	0xf1, 0x71, 0xb1, 0x31, 0xd1, 0x51, 0x91, 0x11,
	0xe1, 0x61, 0xa1, 0x21, 0xc1, 0x41, 0x81, 0x01,
	0xfe, 0x7e, 0xbe, 0x3e, 0xde, 0x5e, 0x9e, 0x1e,
	0xee, 0x6e, 0xae, 0x2e, 0xce, 0x4e, 0x8e, 0x0e,
	0xf6, 0x76, 0xb6, 0x36, 0xd6, 0x56, 0x96, 0x16,
	0xe6, 0x66, 0xa6, 0x26, 0xc6, 0x46, 0x86, 0x06,
	0xfa, 0x7a, 0xba, 0x3a, 0xda, 0x5a, 0x9a, 0x1a,
	0xea, 0x6a, 0xaa, 0x2a, 0xca, 0x4a, 0x8a, 0x0a,
	0xf2, 0x72, 0xb2, 0x32, 0xd2, 0x52, 0x92, 0x12,
	0xe2, 0x62, 0xa2, 0x22, 0xc2, 0x42, 0x82, 0x02,
	0xfc, 0x7c, 0xbc, 0x3c, 0xdc, 0x5c, 0x9c, 0x1c,
	0xec, 0x6c, 0xac, 0x2c, 0xcc, 0x4c, 0x8c, 0x0c,
	0xf4, 0x74, 0xb4, 0x34, 0xd4, 0x54, 0x94, 0x14,
	0xe4, 0x64, 0xa4, 0x24, 0xc4, 0x44, 0x84, 0x04,
	0xf8, 0x78, 0xb8, 0x38, 0xd8, 0x58, 0x98, 0x18,
	0xe8, 0x68, 0xa8, 0x28, 0xc8, 0x48, 0x88, 0x08,
	0xf0, 0x70, 0xb0, 0x30, 0xd0, 0x50, 0x90, 0x10,
	0xe0, 0x60, 0xa0, 0x20, 0xc0, 0x40, 0x80, 0x00
};

/* ---------------------------------------------------------------------------
 * Backlight driver
 */

static inline void hub12fb_set_brightness(struct hub12_par *par)
{
	u64 ns;

	ns = par->pdata.brightness * ktime_to_ns(par->hsync_period) / 256;

	/* should have some reasonable min and max here
	 * and the change the run mode if you are outside those ranges...
	 */

	par->ledon_period = ns_to_ktime(ns);
}

#if CONFIG_FB_HUB12_BACKLIGHT
static int hub12fb_blank(int blank_mode, struct fb_info *info);

static int hub12bl_update_status(struct backlight_device *bd)
{
	struct hub12_par *par = bl_get_data(bd);
	int retval = 0;
	int blank = FB_BLANK_UNBLANK;

	par->pdata.brightness = bd->props.brightness;
	hub12fb_set_brightness(par);

	if (blank != bd->props.power)
		blank = bd->props.power;

	if (blank != bd->props.fb_blank)
		blank = bd->props.fb_blank;

	if (blank != par->blank)
		retval = hub12fb_blank(blank, par->info);

	return retval;
}

static int hub12bl_get_brightness(struct backlight_device *bl)
{
	struct hub12_par *par = bl_get_data(bl);
	return par->pdata.brightness;
}

static const struct backlight_ops hub12bl_ops = {
	.get_brightness = hub12bl_get_brightness,
	.update_status = hub12bl_update_status,
};

static void __devinit init_hub12bl(struct fb_info *info)
{
	struct hub12_par *par = info->par;
	struct backlight_device	*	bl;
	struct backlight_properties	props;
	char name[12];

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = 255;
	props.power = FB_BLANK_UNBLANK;

	snprintf(name, sizeof(name), DRIVER_NAME "-bl%d", info->node);

	bl = backlight_device_register(name, &par->spi->dev,
					 par, &hub12bl_ops, &props);
	if (IS_ERR(bl)) {
		dev_err( &par->spi->dev, "error %ld on backlight register\n",
			PTR_ERR(bl));
	}
	info->bl_dev = bl;
}

static void __devexit exit_hub12bl(struct fb_info *info)
{
	if (info->bl_dev) {
		backlight_device_unregister(info->bl_dev);
		info->bl_dev = NULL;
	}
}

#else
static void init_hub12bl(struct fb_info *info)
{
	struct hub12_par *par = info->par;
	dev_warn(  &par->spi->dev, "backlight control is not available\n");
}

static void exit_hub12bl(struct fb_info *info)
{
}

#endif

/* ---------------------------------------------------------------------------
 *  Framebuffer driver loop
 */

/*
 * do_vsync breaks up the framebuffer into the
 * to data that will go over the wire to the shift registers.
 * individual displays are 32 w x 16 h with 4 scan lines, in 4 colums.
 *
 * many things to iterate over...
 *  i, 4 "scan lines"
 *  j, height / 16 "module rows"
 *  k, width / 8 "columes per row"
 *  l, 4 lines per column (per scanline, bottom up!)
 */

/* some spi masters can write lsb mode, so in the future
 * one could implement a different transfer mode
 * and not use the bit reverse table
 */

#define CONVERT(byte)	bit_reverse_inverse[byte]

static void inline load_interlace_1bit(u8 *lb, u8 *fb, u8 lace,
					u16 width, u16 height)
{
	int j, k, l, rowbytes = width/8;
	/*
	* iterates in direction of output stream...
	* copy the data, 8 pixel byte by byte, need bitswap for SPI
	* (lsb first spi driver is rare)
	*/

	for (j=0; j<height/16; j++)
		for (k=0; k<rowbytes; k++)
			for (l=3; l>=0; l--)
				*lb++ = CONVERT(fb[rowbytes*(j*16+l*4+lace) + k]);
}



static void inline load_interlace_pseudo_8bit(u8 *lb, u8 *fb, u8 lace,
					 u16 width, u16 height, const u8 *palette)
{
	int j, k, l, rowbytes= width/8;
	u8 *px;
	u8 byte;
	/* same loop as 1 bit, but build byte from 8 pixels with palette */

	for (j=0; j<height/16; j++)
		for (k=0; k<rowbytes; k++)
			for (l=3; l>=0; l--) {
				px = fb + width * (j*16+l*4+lace) + k*8;
				byte  = palette[*px++] << 7;
				byte |= palette[*px++] << 6;
				byte |= palette[*px++] << 5;
				byte |= palette[*px++] << 4;
				byte |= palette[*px++] << 3;
				byte |= palette[*px++] << 2;
				byte |= palette[*px++] << 1;
				byte |= palette[*px++];
				*lb++ = byte;
			}
}

static void do_vsync(struct hub12_par *par)
{
	s16 i;

	for(i=0;i<4;i++)
		if (par->pdata.bpp == 1)
			load_interlace_1bit(par->hsync_buf[i], par->fb_buffer, i,
				par->pdata.width, par->pdata.height);
		else
			load_interlace_pseudo_8bit(par->hsync_buf[i], par->fb_buffer, i,
				par->pdata.width, par->pdata.height, palette_1bit);
	par->i_scan = 0;
}

static void shift_scanline_completion(void *context)
{
	/* run after the spi transfer is complete */

	static const int avals[4] = {0,1,0,1};
	static const int bvals[4] = {0,0,1,1};

	struct hub12_par *par = (struct hub12_par *) context;

	/* bits have been shifted.. now lets enable the proper output */
	/* move them to the output */
	gpio_set_value(par->pdata.gpio.latch, 1);
	/* select the proper output lines */
	gpio_set_value(par->pdata.gpio.a, avals[par->i_scan]);
	gpio_set_value(par->pdata.gpio.b, bvals[par->i_scan]);

	if (par->blank == FB_BLANK_UNBLANK && par->running) {
		/* turn on the light */
		gpio_set_value(par->pdata.gpio.enable, 1);
		hrtimer_start(&par->ledon_timer, par->ledon_period,
				HRTIMER_MODE_REL);
	}

	par->i_scan++;

	if (par->i_scan == 4) 	/* do the vsync now */
		do_vsync(par);

	complete(&par->hsync_done);
}


static void setup_hsync(struct hub12_par *par)
{
	/* setup the spi transfer
	 * we don't have to do this every time
	 * just after we change settings
	 */

	struct spi_message *m = &par->message;
	struct spi_transfer *t = &par->transfer;

	t->rx_buf = NULL;
	t->rx_dma = 0;
	t->len    = par->hsync_length;

	t->cs_change		= 1;
	t->bits_per_word	= 8;
	t->delay_usecs		= 0;
	t->speed_hz		= 0; /* default */

	spi_message_init(m);
	spi_message_add_tail(t,m);

	m->context = par;
	m->complete = shift_scanline_completion;

	/* mark our completion done so we can start again */
	init_completion(&par->hsync_done);
	complete(&par->hsync_done);
}

static void shift_scanline_start(struct hub12_par *par)
{
	struct spi_message *m = &par->message;
	struct spi_transfer *t = &par->transfer;

	t->tx_buf = par->hsync_buf[par->i_scan];
	t->tx_dma = par->hsync_dma[par->i_scan];

	/* only our driver until we can latch */
	spi_async_locked(par->spi, m);
}


static enum hrtimer_restart ledon_expired(struct hrtimer *t)
{
	struct hub12_par *par = container_of(t, struct hub12_par, ledon_timer);

	/* turn out the light. */
	gpio_set_value(par->pdata.gpio.enable, 0);

	/* timer is restarted by end of hsync spi transfer */
	return HRTIMER_NORESTART;
}


static enum hrtimer_restart do_hsync(struct hrtimer *timer)
{
	struct hub12_par *par = container_of(timer,
					     struct hub12_par, hsync_timer);
	/* turn off the latch */
	gpio_set_value(par->pdata.gpio.latch, 0);

	/* no more hsync */
	if (!par->running) {
		/* signal vsync in case of waiters... */
		return HRTIMER_NORESTART;
	}

	/* sometimes the last transfer can still be running!
	 * this happens on the RPi because the SPI master can take
	 * up to 10msec to respond sometimes
	 *
	 * we will just skip the transfer and wait for the next
	 * go around if we weren't "done"
	 *
	 * if last hsync was complete, eat the done and start anew
	 */

	if (try_wait_for_completion(&par->hsync_done))
		shift_scanline_start(par);

	/* really shift it forward */
	hrtimer_add_expires(timer, par->hsync_period);
	/* in case it fell to far behind */
	hrtimer_forward_now(timer, par->hsync_period);
	return HRTIMER_RESTART;
}

static void hub12fb_stop_running(struct hub12_par *par)
{
	if (!par->running)
		return;

	par->running = 0;
	/* will sleep until hsync is stopped */
	hrtimer_cancel(&par->hsync_timer);

	/* in the chance a shift was already started */
	wait_for_completion_interruptible_timeout(&par->hsync_done,
						  par->hsync_timeout);

	/* lets also make sure the enable is off */
	hrtimer_cancel(&par->ledon_timer);
	gpio_set_value(par->pdata.gpio.enable, 0);

	/* shutdown other gpio */
	gpio_set_value(par->pdata.gpio.a, 0);
	gpio_set_value(par->pdata.gpio.b, 0);
	gpio_set_value(par->pdata.gpio.latch, 0);

}

static void hub12fb_start_running(struct hub12_par *par)
{
	if (par->running)
		return;

	/* begin with vsync */
	do_vsync(par);
	setup_hsync(par);

	par->running = 1;
	hrtimer_start(&par->hsync_timer, par->hsync_period, HRTIMER_MODE_REL);
}

static inline u32 refresh_to_pixclock(int refresh, struct fb_var_screeninfo *var)
{
	u32 pixclock;
	pixclock = 1000 * KHZ2PICOS(refresh * var->xres * var->yres);
	return pixclock;
}

static inline u32 hub12fb_refresh_rate(struct fb_var_screeninfo *var)
{
	u32 refresh;
	refresh = PICOS2KHZ(var->pixclock/1000) / (var->xres * var->yres);
	return refresh;
}


/**
 *      hub12fb_check_var - Validates a var passed in.
 *      @var: frame buffer variable screen structure
 *      @info: frame buffer structure that represents a single frame buffer
 *
 */
static int hub12fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	if (!var->xres)
		var->xres = 1;
	if (!var->yres)
		var->yres = 1;

	/* fixed array of panels. Round up to next unit size. */
	var->xres = ((var->xres + 31) / 32) * 32;
	var->yres = ((var->yres + 15) / 16) * 16;

	/* limit refresh to 1000 hz */
	if (var->pixclock < refresh_to_pixclock(1000,var))
		var->pixclock = refresh_to_pixclock(1000, var);

	/* don't try "virtual" */
	var->xres_virtual = var->xres;
	var->yres_virtual = var->yres;
	var->xoffset = 0;
	var->yoffset = 0;

	var->height = var->yres * 10; /* for P10 arrays */
	var->width  = var->xres * 10;

	var->upper_margin = 0;
	var->lower_margin = 0;
	var->left_margin  = 0;
	var->right_margin = 0;

	if (var->bits_per_pixel > 1)
		var->bits_per_pixel = 8;
	else
		var->bits_per_pixel = 1;

	var->vmode = FB_VMODE_NONINTERLACED;

	/* in future, allow longer length palette in 8 bit mode */
	switch (var->bits_per_pixel) {
		case 1:
			var->red.offset = 0;
			var->red.length = 1;
			var->green.offset = 0;
			var->green.length = 1;
			var->blue.offset = 0;
			var->blue.length = 1;
			var->transp.offset = 0;
			var->transp.length = 0;
			break;
		case 8:
			var->red.offset = 0;
			var->red.length = 1;
			var->green.offset = 0;
			var->green.length = 1;
			var->blue.offset = 0;
			var->blue.length = 1;
			var->transp.offset = 0;
			var->transp.length = 0;
			break;
	}
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;


	return 0;
}

static void hub12fb_free_buffers(struct hub12_par *par)
{
	int i;
	struct device *device = &par->spi->dev;

	DEFINE_DMA_ATTRS(attrs);
	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);

	/* stop running the kernel refresh thread before this! */
	hub12fb_stop_running(par);

	if (!device->coherent_dma_mask)
		device = NULL;

	for (i=0; i<4; i++) {
		if (par->hsync_dma[i])
			dma_free_attrs(device, par->hsync_length, par->hsync_buf[i],
				       par->hsync_dma[i], &attrs);

		par->hsync_buf[i] = NULL;
		par->hsync_dma[i] = 0;
	}

	if (par->fb_buffer) {
		free_pages_exact(par->fb_buffer, par->bsize);
		par->fb_buffer = NULL;
	}
}

static int hub12fb_allocate_buffers(struct fb_info *info){
	struct hub12_par *par = info->par;
	struct fb_fix_screeninfo *fix = &info->fix;
	int i;
	struct device *device = &par->spi->dev;
	DEFINE_DMA_ATTRS(attrs);
	dma_addr_t dma_handle = 0;
	int good_dma_mask = 1;
	void *p;

	if (!device->coherent_dma_mask) {
		printk(KERN_ALERT DRIVER_NAME " should set device coherent_dma_mask."
			" Driver (Master) masks %llx (%llx)\n", device->coherent_dma_mask,
			par->spi->master->dev.coherent_dma_mask);
		good_dma_mask = 0;
	}

	if (!good_dma_mask)
		device = NULL;

	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);

	fix->line_length = (par->pdata.width * par->pdata.bpp + 7) / 8;

	//par->bsize = ((par->pdata.height * fix->line_length)/PAGE_SIZE + 1) * PAGE_SIZE;
	par->bsize = PAGE_ALIGN(par->pdata.height * fix->line_length);

	/* page aligned memory of at least bsize length */
	p = alloc_pages_exact(par->bsize, GFP_DMA | __GFP_ZERO);
	if (!p)
		return -ENOMEM;

	memset(p,0,par->bsize);

	/* set buffers now to fb device */
	par->fb_buffer    = p;
	info->screen_base = p;

	fix->smem_start   = (unsigned long) p;
	//fix->smem_start   = virt_to_phys(p);
	fix->smem_len     = par->bsize;

	/* Report alloc memory info! */
	printk("framebuffer: kernel=0x%p, smem=0x%p, len=0x%x\n",
		p, (void *)fix->smem_start, fix->smem_len);

	par->hsync_length = (par->pdata.width/8) * par->pdata.height / 4;

	for (i=0; i<4; i++) {
		p = dma_alloc_attrs(device, par->hsync_length,
				    &dma_handle, GFP_KERNEL, &attrs);

		if (!p)
			goto cleanup_buffers;

		par->hsync_buf[i] = p;
		par->hsync_dma[i] = dma_handle;
	}

	return 0;

cleanup_buffers:
	hub12fb_free_buffers(par);
	return -ENOMEM;
}


/**
 *      hub12fb_set_par - Alters the hardware state.
 *      @info: frame buffer structure that represents a single frame buffer
 *
 *	Using the fb_var_screeninfo in fb_info we set the resolution of the
 *	this particular framebuffer. This function alters the par AND the
 *	fb_fix_screeninfo stored in fb_info. It doesn't not alter var in
 *	fb_info since we are using that data. This means we depend on the
 *	data in var inside fb_info to be supported by the hardware.
 *
 *      This function is also used to recover/restore the hardware to a
 *      known working state.
 *
 *	hub12fb_check_var is always called before hub12fb_set_par to ensure that
 *      the contents of var is always valid.
 *
 *	Returns negative errno on error, or zero on success.
 */
static int hub12fb_set_par(struct fb_info *info)
{
	int retval=0;
	struct hub12_par *par = info->par;
	struct fb_fix_screeninfo *fix = &info->fix;
	struct fb_var_screeninfo *var = &info->var;

	/* before we do anything... lets stop the current framebuffer */

	hub12fb_stop_running(info->par);
	/* clear old buffers if size or depth change */
	if (par->fb_buffer && ( par->pdata.width  != var->xres ||
				par->pdata.height != var->yres ||
				par->pdata.bpp    != var->bits_per_pixel ))
		hub12fb_free_buffers(par);

	par->pdata.width  = var->xres;
	par->pdata.height = var->yres;
	par->pdata.bpp    = var->bits_per_pixel;

	if (par->fb_buffer == NULL)
		retval = hub12fb_allocate_buffers(info);

	if (retval) {
		dev_err(&par->spi->dev, "Unable to allocate buffers.");
		return retval;
	}

	/* set fix based on var */
	if (var->bits_per_pixel == 1)
		fix->visual = FB_VISUAL_MONO10;
	else
		fix->visual = FB_VISUAL_STATIC_PSEUDOCOLOR;


	/* set timings */
	par->hsync_period = ns_to_ktime((NSEC_PER_SEC /
					 hub12fb_refresh_rate(&info->var))/4);

	par->hsync_timeout = (HZ/4) / hub12fb_refresh_rate(&info->var) + 1;

	hub12fb_set_brightness(par);

	printk(KERN_ALERT DRIVER_NAME
	       " timings (usec) hsync: %lld, led: %lld, hsync timeout: %ld\n",
	       ktime_to_us(par->hsync_period),
	       ktime_to_us(par->ledon_period),
	       (long) jiffies_to_usecs(par->hsync_timeout));

	info->flags = hub12fb_info_flags;

	if (par->blank == FB_BLANK_UNBLANK)
		hub12fb_start_running(par);

	return 0;
}

/**
 *      hub12fb_blank - NOT a required function. Blanks the display.
 *      @blank_mode: the blank mode we want.
 *      @info: frame buffer structure that represents a single frame buffer
 *
 *      If implementing this function, at least support FB_BLANK_UNBLANK.
 *      Return !0 for any modes that are unimplemented.
 *
 */
static int hub12fb_blank(int blank_mode, struct fb_info *info)
{
	struct hub12_par *par = info->par;

	par->blank = blank_mode;

	switch(blank_mode) {

	case FB_BLANK_NORMAL:         /* Normal blanking */
	case FB_BLANK_VSYNC_SUSPEND:  /* VESA blank (vsync off) */
	case FB_BLANK_HSYNC_SUSPEND:  /* VESA blank (hsync off) */
	case FB_BLANK_POWERDOWN:      /* Poweroff */
		hub12fb_stop_running(par);
		break;

	case FB_BLANK_UNBLANK: /* Unblanking */
		hub12fb_start_running(par);
		break;
	}
	return 0;
}

/*
 *  Frame buffer operations
*/

static struct fb_ops hub12fb_ops = {
	.owner		= THIS_MODULE,
	.fb_check_var	= hub12fb_check_var,
	.fb_set_par	= hub12fb_set_par,
	.fb_blank	= hub12fb_blank,
	.fb_read	= fb_sys_read,
	.fb_write	= fb_sys_write,
	.fb_fillrect	= sys_fillrect,
	.fb_copyarea	= sys_copyarea,
	.fb_imageblit	= sys_imageblit,
};

/* -------------------------------------------------------------------------
 *  Initialization
 */

static int hub12fb_modestr_to_info(const char *mode_option, struct fb_info *info)
{
	/* parseing section borrowed form modedb.c */
	struct fb_var_screeninfo *var = &info->var;
	int i;
	const char *name;
	unsigned int namelen;
	int res_specified = 0, refresh_specified = 0;
	int yres_specified = 0, bpp_specified = 0;
	unsigned int xres = 32, yres = 16, refresh = 60, bpp=1;

	if (!mode_option)
		mode_option = "32x32@60";

	name = mode_option;
	namelen = strlen(name);

	for (i = namelen-1; i >= 0; i--) {
		switch (name[i]) {
		case '@':
			namelen = i;
			if (!refresh_specified && !yres_specified) {
				refresh = simple_strtol(&name[i+1], NULL,
							10);
				refresh_specified = 1;
			} else
				goto done;
			break;
		case '-':
			namelen = i;
			if (!bpp_specified && !yres_specified) {
				bpp = simple_strtol(&name[i+1], NULL,
						    10);
				bpp_specified = 1;
			} else
				goto done;
			break;
		case 'x':
			if (!yres_specified) {
				yres = simple_strtol(&name[i+1], NULL,
						     10);
				yres_specified = 1;
			} else
				goto done;
			break;
		case '0' ... '9':
			break;
		default:
			goto done;
		}
	}
	if (i < 0 && yres_specified) {
		xres = simple_strtol(name, NULL, 10);
		res_specified = 1;
	}
done:
	if (res_specified)
		var->xres = 32*((xres - 1)/32 + 1);

	if (yres_specified)
		var->yres = 16*((yres - 1)/16 + 1);

	if (refresh_specified) {
		var->pixclock = refresh_to_pixclock(refresh, var);
	}

	if (bpp_specified)
		var->bits_per_pixel = bpp;

	hub12fb_check_var(var, info);

	printk(KERN_ALERT DRIVER_NAME " setting mode %dx%d-%d@%dHz.\n",
	       var->xres, var->yres, var->bits_per_pixel, hub12fb_refresh_rate(var));

	return 0;
}


static int __devinit hub12fb_probe (struct spi_device *spidev)
{
	struct fb_info   *info;
	struct hub12_par *par;
	struct device    *device = &spidev->dev;
	struct fb_var_screeninfo *var;
	struct fb_fix_screeninfo *fix;
	struct hub12fb_platform_data *pdata;
	int retval = -ENOMEM;

	/*
	 * Dynamically allocate info and par
	 */

	info = framebuffer_alloc(sizeof(struct hub12_par), device);

	if (!info) {
		dev_err(device, "Unable to allocate framebuffer.");
		goto probe_fail_final;
	}
	par = info->par;
	memset(par,0,sizeof(struct hub12_par));
	par->spi = spidev;
	par->info = info;

	info->fbops = &hub12fb_ops;

	var = &info->var;
	fix = &info->fix;

	/* keep a copy in the parameters */
	memcpy(&par->pdata, spidev->dev.platform_data,
	       sizeof(struct hub12fb_platform_data));
	pdata = &par->pdata;

	/* lets fill in fb_info */
	memcpy(&info->fix, &hub12fb_fix, sizeof(struct fb_fix_screeninfo));

	/* module command line overrides of pdata */
	if (gpio[0] != -1)
		pdata->gpio.enable = gpio[0];
	if (gpio[1] != -1)
		pdata->gpio.latch  = gpio[1];
	if (gpio[2] != -1)
		pdata->gpio.a      = gpio[2];
	if (gpio[3] != -1)
		pdata->gpio.b      = gpio[3];

	/* request the gpio pins, released when device is */
	retval = devm_gpio_request_one(device, pdata->gpio.enable,
					GPIOF_OUT_INIT_LOW, DRIVER_NAME "_oe");
	if (retval) goto probe_fail_free_fb;

	retval = devm_gpio_request_one(device, pdata->gpio.latch,
					GPIOF_OUT_INIT_LOW, DRIVER_NAME "_la");
	if (retval) goto probe_fail_free_fb;

	retval = devm_gpio_request_one(device, pdata->gpio.a,
					GPIOF_OUT_INIT_LOW, DRIVER_NAME "_a");
	if (retval) goto probe_fail_free_fb;

	retval = devm_gpio_request_one(device, pdata->gpio.b,
					GPIOF_OUT_INIT_LOW, DRIVER_NAME "_b");
	if (retval) goto probe_fail_free_fb;

	/* register the backlight before we register the framebuffer. */
	init_hub12bl(info);

	/*
	if (!info->bl_dev) {
		dev_err(device, "Unable to register backlight.\n");
		goto probe_fail_free_fb;
	}
	*/

	/* initialize kernel thread structures in hub12_par */
	hrtimer_init(&par->hsync_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	par->hsync_timer.function = do_hsync;

	hrtimer_init(&par->ledon_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	par->ledon_timer.function = ledon_expired;


	/* some filling in of fb_var_screeninfo */
	var->xres 	= par->pdata.width;
	var->yres 	= par->pdata.height;
	var->bits_per_pixel = par->pdata.bpp;
	var->pixclock	= refresh_to_pixclock(par->pdata.refresh, var);
	var->activate	= FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;

	/* lets take care of initial mode override now */
	if (mode)
		hub12fb_modestr_to_info(mode, info);

	/*
	if (fb_alloc_cmap(&info->cmap, 256, 0)) {
		retval = -ENOMEM;
		goto probe_fail_free_bl;
	}
	*/

	/* static cmap of 2 colors (red and black) */
	info->cmap = hub12fb_cmap_2_colors;

	info->fbops = &hub12fb_ops;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->pseudo_palette = par->pseudo_palette; /* not used */

	/* calls check_var and set_par */
	retval = fb_set_var(info, &info->var);
	if (retval)
		goto probe_fail_free_cmap;

	if (register_framebuffer(info) < 0) {
		printk(KERN_ERR DRIVER_NAME ": unable to register framebuffer.\n");
		retval = -EINVAL;
		goto probe_fail_free_buffers;
	}

	/* this should be done by fb_set var */
	/*
	fb_var_to_videomode(&fbmode, &info->var);
	fb_add_videomode(&fbmode, &info->modelist);
	*/

	spi_set_drvdata(spidev, info);

	return retval;

probe_fail_free_buffers:
	hub12fb_free_buffers(par);

probe_fail_free_cmap:
	/* fb_dealloc_cmap(&info->cmap);*/

/*probe_fail_free_bl: */
	exit_hub12bl(info);

probe_fail_free_fb:
	framebuffer_release(info);

probe_fail_final:
	return retval;
}

    /*
     *  Cleanup
     */
static int __devexit hub12fb_remove(struct spi_device *spidev)
{
	struct fb_info *info = spi_get_drvdata(spidev);

	if (info) {
		hub12fb_stop_running(info->par);
		unregister_framebuffer(info);
		spi_set_drvdata(spidev, NULL);
		/*fb_dealloc_cmap(&info->cmap);*/
		exit_hub12bl(info);
		hub12fb_free_buffers(info->par);
		/* free gpio ? */
		framebuffer_release(info);
	}
	return 0;
}

#ifdef CONFIG_PM
/**
 *	hub12fb_suspend - Suspend the device.
 *	@dev: spi device
 *	@msg: the suspend event code.
 */
static int hub12fb_suspend(struct spi_device *dev, pm_message_t msg)
{
	struct fb_info *info = spi_get_drvdata(dev);

	if (!info)
		return -EINVAL;

	/* suspend is easy... just stop the line writing */
	hub12fb_stop_running(info->par);

	return 0;
}

/**
 *	hub12fb_resume - Resume the device.
 *	@dev: spi device
 */
static int hub12fb_resume(struct spi_device *dev)
{
	struct fb_info *info = spi_get_drvdata(dev);

	if (!info)
		return -EINVAL;

	/* resume here */
	hub12fb_start_running(info->par);

	return 0;
}
#else
#define hub12fb_suspend NULL
#define hub12fb_resume NULL
#endif /* CONFIG_PM */


#ifndef MODULE
/*
 * Only necessary if your driver takes special options,
 * otherwise we fall back on the generic fb_setup().
 */
static int __init hub12fb_setup(char *options)
{
	char *this_opt;
	DBG(__func__)

	if (!options || !*options)
		return 0;

	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!*this_opt) continue;
	}

	return 0;
}
#endif /* MODULE */


static const struct spi_device_id hub12fb_ids[] = {
	{ HUB12FB_MODALIAS,	0 },
	{ },
};

MODULE_DEVICE_TABLE(spi, hub12fb_ids);

static struct spi_driver hub12fb_driver = {
	.driver		= {
		.name  = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.id_table	= hub12fb_ids,
	.probe		= hub12fb_probe,
	.remove		= __devexit_p(hub12fb_remove),
	.suspend	= hub12fb_suspend,
	.resume		= hub12fb_resume,
};

/*
 * dynamic loading of device not setup with board_info from mach driver
 *
 * expanding on Scott Ellis's idea https://gist.github.com/scottellis/716613
 *
 * probably should go away, but great for Raspberry Pi or other
 * hackable boards, at least until Device Tables are fully implemented
 *
 */
static struct spi_device * dynamic_loaded_device = NULL;

#ifdef MODULE
static int check_params_for_dynamic_loading(void)
{
	if (spi[0] != -1) {
		board_info.bus_num = spi[0];
		if (spi[1] != -1)
			board_info.chip_select = spi[1];
		if (spi[2] != -1)
			board_info.max_speed_hz = spi[2];
		return 1;
	}
	return 0;
}

static int __init add_hub12fb_device_to_bus(void)
{
	struct spi_master *spi_master;
	struct spi_device *spi_device;
	char buff[64];
	int retval = 0;
	u16 spi_bus = board_info.bus_num;
	u8  spi_cs  = board_info.chip_select;

	spi_master = spi_busnum_to_master(spi_bus);
	if (!spi_master) {
		printk(KERN_ALERT "spi_busnum_to_master(%d) returned NULL\n",
		       spi_bus);
		printk(KERN_ALERT "Missing modprobe of spi master?\n");
		return -1;
	}

	/* Check whether this SPI bus.cs is already claimed */
	snprintf(buff, sizeof(buff), "%s.%u",
		 dev_name(&spi_master->dev), spi_cs);

	spi_device = to_spi_device(bus_find_device_by_name(&spi_bus_type,
							   NULL, buff));

	if (spi_device) {
		/* There is already a device configured for this bus.cs
		 * combination. If there is no driver loaded, we remove the
		 * driverless device (clear away spidev for example).
                 * If a driver is loaded and it is not us, we complain and fail.
		 */
		spi_dev_put(spi_device); /* we won't keep a reference */
		printk(KERN_ALERT DRIVER_NAME
		       " found existing device [%s], modalias = [%s]\n",
		       dev_name(&spi_device->dev), spi_device->modalias);
		if (!spi_device->dev.driver) {
			printk(KERN_ALERT DRIVER_NAME
			       " Removing driverless device at %s\n", buff);
			spi_unregister_device(spi_device);
			spi_device = NULL; /* lets add ours now! */
		} else if (strcmp(DRIVER_NAME, spi_device->dev.driver->name)) {
			/* not us.. :-( */
			printk(KERN_ALERT DRIVER_NAME
				": Driver [%s] already registered for %s\n",
				spi_device->dev.driver->name, buff);
			printk(KERN_ALERT DRIVER_NAME ": Try \"rmmod %s\" ?\n",
			       spi_device->dev.driver->name);
			retval = -1;
		}
	}
	if (!spi_device) {
		/* board info already modified and ready */

		dynamic_loaded_device = spi_new_device(spi_master, &board_info);

		if (dynamic_loaded_device) {
			printk(KERN_ALERT DRIVER_NAME
			       " loaded dynamically on spi%d.%d\n",
			       spi_bus, spi_cs);
		} else {
			printk(KERN_ERR DRIVER_NAME
			       ": spi_new_device() failed.\n");
			retval = -1;
		}
	}

	spi_master_put(spi_master);

	return retval;
}
#endif

static int __init hub12fb_init(void)
{
	int retval;
#ifndef MODULE
	/*
	 *  For kernel boot options (in 'video=hub12fb:<options>' format)
	 */
	char *option = NULL;

	if (fb_get_options("hub12fb", &option))
		return -ENODEV;

	if (option) mode = option; /* save option for probe */
#else
	/* let's first try to add the device in case it doesn't exist yet */
	/* which would be the case if we didn't add it to the board driver */

	if (check_params_for_dynamic_loading()) {
		retval = add_hub12fb_device_to_bus();
		if (retval)
			return retval;
	}
#endif
	return spi_register_driver(&hub12fb_driver);
}

static void __exit hub12fb_exit(void)
{
	spi_unregister_driver(&hub12fb_driver);
	if (dynamic_loaded_device) {
		spi_unregister_device(dynamic_loaded_device);
		spi_dev_put(dynamic_loaded_device);
		dynamic_loaded_device = NULL;
	}
}

/* -------------------------------------------------------------------------
 * Modularization
 */

module_init(hub12fb_init);
module_exit(hub12fb_exit);

module_param(mode, charp, 0);
MODULE_PARM_DESC(mode,
	" specify video mode as \"<xres>x<yres>[-<bpp>][@<refresh>]\"");

module_param_array(spi, int, NULL, 0);
MODULE_PARM_DESC(spi,
	" dynamically bind to SPI with spi=<bus>,<cs>,<maxspeed>");

module_param_array(gpio, int, NULL, 0);
MODULE_PARM_DESC(gpio,
		 " specifiy GPIO connections as gpio=<oe>,<la>,<a>,<b>");

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Darren Garnier");
/* covered by spi_register_driver
MODULE_ALIAS("spi:" HUB12FB_MODALIAS);
*/