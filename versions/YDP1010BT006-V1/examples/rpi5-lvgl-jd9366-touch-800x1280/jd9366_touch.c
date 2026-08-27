// SPDX-License-Identifier: GPL-2.0+
/*
 * JD9366 Touch Screen Driver
 *
 * This example is from the open-source sharing by engineers of Yuying Optoelectronics (鱼鹰光电)
 * on Github.com/osptek. Welcome to provide improvement suggestions.
 *
 * 本例程来源于鱼鹰光电的工程师的开源分享 Github.com/osptek，欢迎提出改进意见
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/input/touchscreen.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/gpio/consumer.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>

#define JD9366_TOUCH_NAME		"jd9366_touch"
#define CHIP_ID				0x9032
#define POLL_INTERVAL_MS		15

struct jd9366_touch_data {
	struct i2c_client *client;
	struct input_dev *input_dev;
	struct touchscreen_properties absinfo;
	int touch_size_x;
	int touch_size_y;
	bool report_as_mouse;
	struct delayed_work work;
	struct workqueue_struct *wq;
};

struct TouchPoint_Format {
	char x_area_id;
	char x;
	char y_area_id;
	char y;
	char fix_unknown;
};

struct TouchStatus {
	bool pressed;
	int x;
	int y;
};

static void report_touch_event(struct jd9366_touch_data *data,
			       struct TouchStatus *points)
{
	int i;

	if (data->report_as_mouse) {
		for (i = 0; i < 10; i++) {
			if (points[i].pressed) {
				input_report_key(data->input_dev, BTN_TOUCH, 1);
				input_report_abs(data->input_dev, ABS_X, points[i].x);
				input_report_abs(data->input_dev, ABS_Y, points[i].y);
				input_report_abs(data->input_dev, ABS_PRESSURE, 1);
				break;
			}
		}
		if (!points[0].pressed) {
			input_report_key(data->input_dev, BTN_TOUCH, 0);
			input_report_abs(data->input_dev, ABS_PRESSURE, 0);
		}
		input_sync(data->input_dev);
	} else {
		for (i = 0; i < 10; i++) {
			input_mt_slot(data->input_dev, i);
			if (points[i].pressed) {
				input_mt_report_slot_state(data->input_dev,
							   MT_TOOL_FINGER, true);
				input_report_abs(data->input_dev,
						 ABS_MT_POSITION_X, points[i].x);
				input_report_abs(data->input_dev,
						 ABS_MT_POSITION_Y, points[i].y);
			} else {
				input_mt_report_slot_state(data->input_dev,
							   MT_TOOL_FINGER, false);
			}
		}
		input_sync(data->input_dev);
	}
}

static void touch_work_func(struct jd9366_touch_data *data)
{
	int ret;
	u8 cmd[] = {0x20, 0x02, 0x11, 0x20};
	u8 read_data[80];
	int i;
	int actual_x, actual_y;
	struct TouchStatus touch_points[10];

	memset(touch_points, 0, sizeof(touch_points));

	ret = i2c_master_send(data->client, cmd, sizeof(cmd));
	if (ret != sizeof(cmd)) {
		dev_err(&data->client->dev,
			"Failed to send touch status command: %d\n", ret);
		return;
	}

	ret = i2c_master_recv(data->client, read_data, sizeof(read_data));
	if (ret != sizeof(read_data)) {
		dev_err(&data->client->dev,
			"Failed to read touch status: %d\n", ret);
		return;
	}

	if (read_data[0] == 0)
		goto report;

	for (i = 0; i < 10; i++) {
		struct TouchPoint_Format *point =
			(struct TouchPoint_Format *)&read_data[3 + i * 5];

		if (point->x_area_id > 100 || point->y_area_id > 100) {
			touch_points[i].pressed = false;
			continue;
		}

		actual_x = point->x_area_id * 255 + point->x;
		actual_y = point->y_area_id * 255 + point->y;

		if (actual_x > data->touch_size_x)
			actual_x = data->touch_size_x;
		if (actual_y > data->touch_size_y)
			actual_y = data->touch_size_y;

		touch_points[i].pressed = true;
		touch_points[i].x = actual_x;
		touch_points[i].y = actual_y;
	}

report:
	report_touch_event(data, touch_points);
}

static void jd9366_touch_work_handler(struct work_struct *work)
{
	struct jd9366_touch_data *data =
		container_of(to_delayed_work(work),
			     struct jd9366_touch_data, work);

	touch_work_func(data);
	queue_delayed_work(data->wq, &data->work,
			   msecs_to_jiffies(POLL_INTERVAL_MS));
}

static int jd9366_touch_read_chip_id(struct i2c_client *client, u16 *chip_id)
{
	int ret;
	u8 cmd[] = {0x40, 0x00, 0x80, 0x76};
	u8 data[2];

	ret = i2c_master_send(client, cmd, sizeof(cmd));
	if (ret != sizeof(cmd)) {
		dev_err(&client->dev, "Failed to send chip ID command: %d\n", ret);
		return ret < 0 ? ret : -EIO;
	}

	ret = i2c_master_recv(client, data, sizeof(data));
	if (ret != sizeof(data)) {
		dev_err(&client->dev, "Failed to read chip ID: %d\n", ret);
		return ret < 0 ? ret : -EIO;
	}

	*chip_id = (data[1] << 8) | data[0];
	dev_info(&client->dev, "JD9366 Chip ID: 0x%04X\n", *chip_id);

	return 0;
}

static int jd9366_touch_probe(struct i2c_client *client)
{
	struct jd9366_touch_data *data;
	int ret;
	u16 chip_id;

	dev_info(&client->dev, "Probing for JD9366 touch device\n");

	ret = jd9366_touch_read_chip_id(client, &chip_id);
	if (ret) {
		dev_err(&client->dev, "Failed to read chip ID, device may not exist\n");
		return ret;
	}

	if (chip_id != CHIP_ID) {
		dev_err(&client->dev, "Invalid chip ID detected: 0x%04X\n", chip_id);
		return -ENODEV;
	}

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->client = client;

	/* 从 Device Tree 读取分辨率（与你的 dts 完全匹配） */
	if (client->dev.of_node) {
		ret = of_property_read_u32(client->dev.of_node,
					   "touchscreen-size-x",
					   &data->touch_size_x);
		if (ret) {
			dev_err(&client->dev, "touchscreen-size-x not specified\n");
			data->touch_size_x = 800;	/* 默认值 */
		} else {
			dev_info(&client->dev, "touchscreen-size-x: %d\n",
				 data->touch_size_x);
		}

		ret = of_property_read_u32(client->dev.of_node,
					   "touchscreen-size-y",
					   &data->touch_size_y);
		if (ret) {
			dev_err(&client->dev, "touchscreen-size-y not specified\n");
			data->touch_size_y = 1280;	/* 默认值 */
		} else {
			dev_info(&client->dev, "touchscreen-size-y: %d\n",
				 data->touch_size_y);
		}

		data->report_as_mouse =
			of_property_read_bool(client->dev.of_node, "report-as-mouse");
		dev_info(&client->dev, "report-as-mouse: %s\n",
			 data->report_as_mouse ? "true" : "false");
	} else {
		data->touch_size_x = 800;
		data->touch_size_y = 1280;
	}

	data->input_dev = devm_input_allocate_device(&client->dev);
	if (!data->input_dev) {
		dev_err(&client->dev, "Failed to allocate input device\n");
		return -ENOMEM;
	}

	input_set_drvdata(data->input_dev, data);
	data->input_dev->name = JD9366_TOUCH_NAME;
	data->input_dev->phys = "i2c/jd9366_touch";
	data->input_dev->id.bustype = BUS_I2C;
	data->input_dev->dev.parent = &client->dev;

	if (data->report_as_mouse) {
		__set_bit(EV_KEY, data->input_dev->evbit);
		__set_bit(EV_ABS, data->input_dev->evbit);
		__set_bit(BTN_TOUCH, data->input_dev->keybit);

		input_set_abs_params(data->input_dev, ABS_X, 0,
				     data->touch_size_x, 0, 0);
		input_set_abs_params(data->input_dev, ABS_Y, 0,
				     data->touch_size_y, 0, 0);
		input_set_abs_params(data->input_dev, ABS_PRESSURE, 0, 255, 0, 0);

		touchscreen_parse_properties(data->input_dev, false, &data->absinfo);
	} else {
		__set_bit(EV_ABS, data->input_dev->evbit);
		__set_bit(INPUT_PROP_DIRECT, data->input_dev->propbit);

		input_mt_init_slots(data->input_dev, 10,
				    INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);

		input_set_abs_params(data->input_dev, ABS_MT_POSITION_X, 0,
				     data->touch_size_x, 0, 0);
		input_set_abs_params(data->input_dev, ABS_MT_POSITION_Y, 0,
				     data->touch_size_y, 0, 0);
		input_set_abs_params(data->input_dev, ABS_MT_TOOL_TYPE, 0,
				     MT_TOOL_MAX, 0, 0);
		input_set_abs_params(data->input_dev, ABS_MT_SLOT, 0, 9, 0, 0);

		touchscreen_parse_properties(data->input_dev, true, &data->absinfo);
	}

	ret = input_register_device(data->input_dev);
	if (ret) {
		dev_err(&client->dev, "Failed to register input device\n");
		return ret;
	}

	dev_info(&client->dev, "JD9366 touch driver probed successfully\n");

	data->wq = alloc_workqueue("jd9366_touch_wq",
				   WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (!data->wq) {
		dev_err(&client->dev, "Failed to create workqueue\n");
		return -ENOMEM;
	}

	INIT_DELAYED_WORK(&data->work, jd9366_touch_work_handler);
	queue_delayed_work(data->wq, &data->work,
			   msecs_to_jiffies(POLL_INTERVAL_MS));

	return 0;
}

static void jd9366_touch_remove(struct i2c_client *client)
{
	struct jd9366_touch_data *data = i2c_get_clientdata(client);

	dev_info(&client->dev, "Removing JD9366 touch driver\n");

	if (data->wq) {
		cancel_delayed_work_sync(&data->work);
		destroy_workqueue(data->wq);
	}
}

#ifdef CONFIG_OF
static const struct of_device_id jd9366_touch_of_match[] = {
	{ .compatible = "jd9366_touch", },
	{ }
};
MODULE_DEVICE_TABLE(of, jd9366_touch_of_match);
#endif

static const struct i2c_device_id jd9366_touch_id[] = {
	{ JD9366_TOUCH_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, jd9366_touch_id);

static struct i2c_driver jd9366_touch_driver = {
	.driver = {
		.name		= JD9366_TOUCH_NAME,
		.of_match_table	= of_match_ptr(jd9366_touch_of_match),
	},
	.id_table	= jd9366_touch_id,
	.probe		= jd9366_touch_probe,
	.remove		= jd9366_touch_remove,
};

module_i2c_driver(jd9366_touch_driver);

MODULE_AUTHOR("sc-bin");
MODULE_DESCRIPTION("JD9366 Touch Screen Driver");
MODULE_LICENSE("GPL v2");
