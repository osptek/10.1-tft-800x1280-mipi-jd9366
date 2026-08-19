// SPDX-License-Identifier: GPL-2.0+
/*
 * DRM driver for Jadard JD9366 800x1280 2-Lane MIPI-DSI panel
 *
 * This example is from the open-source sharing by engineers of Yuying Optoelectronics (鱼鹰光电)
 * on Github.com/osptek. Welcome to provide improvement suggestions.
 *
 * 本例程来源于鱼鹰光电的工程师的开源分享 Github.com/osptek，欢迎提出改进意见
 */

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>

#include <linux/gpio/consumer.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

struct jadard;

struct jadard_panel_desc {
	const struct drm_display_mode mode;
	unsigned int lanes;
	enum mipi_dsi_pixel_format format;
	int (*init)(struct jadard *jadard);
	unsigned long flags;
};

struct jadard {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct jadard_panel_desc *desc;
	enum drm_panel_orientation orientation;
	struct gpio_desc *reset;
};

static inline struct jadard *panel_to_jadard(struct drm_panel *panel)
{
	return container_of(panel, struct jadard, panel);
}

static int jd9366_800x1280_init(struct jadard *jadard)
{
	struct mipi_dsi_multi_context ctx = { .dsi = jadard->dsi };

	/* Page 1 */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x78, 0x49, 0x61, 0x02, 0x00);

	/* Page 2 */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x02);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x31, 0x12);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x32, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x33, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3c, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3d, 0x78);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3e, 0x43);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3f, 0x30);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x42, 0xa2);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x43, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x44, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x46, 0x17);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x49, 0xc0);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6d, 0x30);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6e, 0x21);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x41,
		0x5b, 0x5b, 0x03, 0x03, 0x5b, 0x5b, 0x02, 0x02,
		0x03, 0x03, 0x03, 0x03);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5a,
		0x00, 0x00, 0x34, 0x34, 0x31, 0x31, 0x23, 0x23, 0x24, 0x24, 0x23);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5b,
		0x23, 0x0b, 0x0b, 0x09, 0x09, 0x0f, 0x0f, 0x0d, 0x0d, 0x06, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5c,
		0x00, 0x00, 0x34, 0x34, 0x31, 0x31, 0x23, 0x23, 0x24, 0x24, 0x23);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5d,
		0x23, 0x0a, 0x0a, 0x08, 0x08, 0x0e, 0x0e, 0x0c, 0x0c, 0x05, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5e,
		0x00, 0x00, 0x31, 0x31, 0x34, 0x34, 0x23, 0x23, 0x24, 0x24, 0x23);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5f,
		0x23, 0x0c, 0x0c, 0x0e, 0x0e, 0x08, 0x08, 0x0a, 0x0a, 0x05, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x60,
		0x00, 0x00, 0x31, 0x31, 0x34, 0x34, 0x23, 0x23, 0x24, 0x24, 0x23);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x61,
		0x23, 0x0d, 0x0d, 0x0f, 0x0f, 0x09, 0x09, 0x0b, 0x0b, 0x06, 0x06);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x64, 0xff, 0xff, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x65, 0xff, 0xff, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x6f, 0x03, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x70, 0x03, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x71, 0x00, 0x00, 0x80);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x72, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4c, 0x22, 0x22);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x73, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4e, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x50, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x55, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x56, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x57, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x58, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x66, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x67, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4a, 0x3f);

	/* Page 8 */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x08);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x31, 0x65);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x33, 0x05);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x40, 0x50);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x41, 0x80);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x42, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x47, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x48, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x50, 0x17);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5a, 0x20);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5c, 0x53);
	/* MIPI 2-Lane */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5D, 0x0D);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x5F, 0x01);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x62, 0x04);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x65, 0x5f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x73, 0x01);

	/* Page A */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x32, 0xff);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x33, 0x28);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3f, 0x53);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x40, 0x15);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x47, 0x20);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x48, 0x80);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x49, 0x03);

	/* Page B Gamma */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x33, 0x00, 0x3d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3c, 0x00, 0xbf);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x43, 0xb1);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x44, 0x31);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3e, 0x00, 0x10, 0x1e, 0x27, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x3f,
		0x54, 0x6f, 0x73, 0x7c, 0x77, 0x91, 0x90, 0x99,
		0xa8, 0xa5, 0xae, 0xb5, 0xc5, 0xb8);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x40,
		0x52, 0x52, 0x53, 0x7f, 0x00, 0x10, 0x1e, 0x27, 0x2f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x41,
		0x54, 0x6f, 0x73, 0x7c, 0x77, 0x91, 0x98, 0x99,
		0xa8, 0xa5, 0xae, 0xb5, 0xc5, 0xb8);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x42, 0x52, 0x52, 0x53, 0x7f);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x45, 0x70);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x46, 0x3b);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x48, 0x7c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x49, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4a, 0x3a);

	/* Page C */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x32, 0x62);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x71, 0x77);

	/* Page D */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x4c, 0x74);

	/* Back to Page 0 */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x30, 0x00);

	/* TE on */
	mipi_dsi_dcs_write_seq_multi(&ctx, 0x35, 0x00);

	/* Sleep out */
	mipi_dsi_dcs_exit_sleep_mode_multi(&ctx);
	mipi_dsi_msleep(&ctx, 120);

	/* Display on */
	mipi_dsi_dcs_set_display_on_multi(&ctx);
	mipi_dsi_msleep(&ctx, 20);

	return ctx.accum_err;
}

static int jadard_prepare(struct drm_panel *panel)
{
	struct jadard *jadard = panel_to_jadard(panel);
	int ret;

	if (jadard->reset) {
		gpiod_set_value_cansleep(jadard->reset, 0);
		msleep(10);
		gpiod_set_value_cansleep(jadard->reset, 1);
		msleep(20);
		gpiod_set_value_cansleep(jadard->reset, 0);
		msleep(120);
	}

	ret = jadard->desc->init(jadard);
	if (ret)
		return ret;

	return 0;
}

static int jadard_enable(struct drm_panel *panel)
{
	struct mipi_dsi_multi_context ctx = { .dsi = to_mipi_dsi_device(panel->dev) };

	mipi_dsi_dcs_set_display_on_multi(&ctx);
	return ctx.accum_err;
}

static int jadard_disable(struct drm_panel *panel)
{
	struct mipi_dsi_multi_context ctx = { .dsi = to_mipi_dsi_device(panel->dev) };

	mipi_dsi_dcs_set_display_off_multi(&ctx);
	return ctx.accum_err;
}

static int jadard_unprepare(struct drm_panel *panel)
{
	struct jadard *jadard = panel_to_jadard(panel);
	struct mipi_dsi_multi_context ctx = { .dsi = jadard->dsi };

	mipi_dsi_dcs_enter_sleep_mode_multi(&ctx);
	msleep(120);

	if (jadard->reset)
		gpiod_set_value_cansleep(jadard->reset, 1);

	return ctx.accum_err;
}

static int jadard_get_modes(struct drm_panel *panel,
			    struct drm_connector *connector)
{
	struct jadard *jadard = panel_to_jadard(panel);
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &jadard->desc->mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = mode->width_mm;
	connector->display_info.height_mm = mode->height_mm;

	return 1;
}

static enum drm_panel_orientation jadard_get_orientation(struct drm_panel *panel)
{
	return panel_to_jadard(panel)->orientation;
}

static const struct drm_panel_funcs jadard_funcs = {
	.prepare = jadard_prepare,
	.enable = jadard_enable,
	.disable = jadard_disable,
	.unprepare = jadard_unprepare,
	.get_modes = jadard_get_modes,
	.get_orientation = jadard_get_orientation,
};

static const struct jadard_panel_desc jd9366_800x1280_desc = {
	.mode = {
		.clock = 76000,
		.hdisplay = 800,
		.hsync_start = 800 + 40,
		.hsync_end = 800 + 40 + 8,
		.htotal = 800 + 40 + 8 + 28,
		.vdisplay = 1280,
		.vsync_start = 1280 + 140,
		.vsync_end = 1280 + 140 + 8,
		.vtotal = 1280 + 140 + 8 + 20,
		.width_mm = 135,
		.height_mm = 216,
		.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	},
	.lanes = 2,
	.format = MIPI_DSI_FMT_RGB888,
	.flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST | MIPI_DSI_MODE_LPM,
	.init = jd9366_800x1280_init,
};

static int jadard_dsi_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct jadard_panel_desc *desc;
	struct jadard *jadard;
	int ret;

	jadard = devm_kzalloc(dev, sizeof(*jadard), GFP_KERNEL);
	if (!jadard)
		return -ENOMEM;

	desc = of_device_get_match_data(dev);
	dsi->mode_flags = desc->flags;
	dsi->format = desc->format;
	dsi->lanes = desc->lanes;

	jadard->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(jadard->reset))
		return dev_err_probe(dev, PTR_ERR(jadard->reset), "Failed to get reset GPIO\n");

	ret = of_drm_get_panel_orientation(dev->of_node, &jadard->orientation);
	if (ret < 0)
		jadard->orientation = DRM_MODE_PANEL_ORIENTATION_NORMAL;

	drm_panel_init(&jadard->panel, dev, &jadard_funcs, DRM_MODE_CONNECTOR_DSI);
	jadard->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&jadard->panel);
	if (ret)
		return ret;

	drm_panel_add(&jadard->panel);

	mipi_dsi_set_drvdata(dsi, jadard);
	jadard->dsi = dsi;
	jadard->desc = desc;

	ret = mipi_dsi_attach(dsi);
	if (ret)
		drm_panel_remove(&jadard->panel);

	return ret;
}

static void jadard_dsi_remove(struct mipi_dsi_device *dsi)
{
	struct jadard *jadard = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&jadard->panel);
}

static const struct of_device_id jadard_of_match[] = {
	{ .compatible = "jadard,jd9366-800x1280", .data = &jd9366_800x1280_desc },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, jadard_of_match);

static struct mipi_dsi_driver jadard_driver = {
	.probe = jadard_dsi_probe,
	.remove = jadard_dsi_remove,
	.driver = {
		.name = "panel-jadard-jd9366",
		.of_match_table = jadard_of_match,
	},
};
module_mipi_dsi_driver(jadard_driver);

MODULE_AUTHOR("Adapted for JD9366 800x1280");
MODULE_DESCRIPTION("Jadard JD9366 800x1280 2-Lane MIPI-DSI Panel Driver");
MODULE_LICENSE("GPL");
