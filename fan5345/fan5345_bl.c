/*
 * Copyright (C) 2018, Pendar Technologies LLC.  All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/of_platform.h>
#include <linux/backlight.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/delay.h>

#define NUM_STEPS 32
#define START_STEP 32
#define MIN_STEP 1
// The FAN5345 decrements with each step.
#define DIR(x) (x--)

struct fan5345_backlight {
	struct gpio_desc *level_gpio;

	int cur_level;
};

static void fan5345_disable(struct backlight_device *bl)
{
	struct fan5345_backlight *fanbl = bl_get_data(bl);

	fanbl->cur_level = 0;
	gpiod_set_value_cansleep(fanbl->level_gpio, false);
  // The gpio must be low for 1ms to cause a shutdown.
  mdelay(1);
}

static int fan5345_set_level(struct backlight_device *bl)
{
	struct fan5345_backlight *fanbl = bl_get_data(bl);

	if(bl->props.brightness < MIN_STEP)
		fan5345_disable(bl);

	// Match the actual brightness to the user-requested brightness.
	while(fanbl->cur_level != bl->props.brightness) {
		// Pulse the GPIO off and on again.
		gpiod_set_value_cansleep(fanbl->level_gpio, false);
      // The minimum time between pulses is 500ns.
      ndelay(500);
		gpiod_set_value_cansleep(fanbl->level_gpio, true);
      // The minimum time between pulses is 500ns.
      ndelay(500);

		DIR(fanbl->cur_level);	

		// Roll over the current level if needed.
		if(fanbl->cur_level <= 0)
			fanbl->cur_level = NUM_STEPS;
	}

	return 0;
}

static int fan5345_get_level(struct backlight_device *bl)
{
	return bl->props.brightness;
}

static const struct backlight_ops fan5345bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.get_brightness = fan5345_get_level,
	.update_status  = fan5345_set_level,
};

static const struct of_device_id fan5345_dt_ids[] = {
	{ .compatible = "fairchild,fan5345", },
	{ }
};

MODULE_DEVICE_TABLE(of, fan5345_dt_ids);

static int fan5345_probe(struct platform_device *pdev)
{
	struct backlight_device *bl;
	struct backlight_properties props;
	struct fan5345_backlight *fanbl;
	struct device_node *np = pdev->dev.of_node;
	int err;
	unsigned int def_level;

	fanbl = devm_kzalloc(&pdev->dev, sizeof(*fanbl), GFP_KERNEL);
	if (!fanbl)
		return -ENOMEM;

	fanbl->level_gpio = devm_gpiod_get(&pdev->dev, "level",
	                                   GPIOD_OUT_LOW);
	if (IS_ERR(fanbl->level_gpio)) {
		err = PTR_ERR(fanbl->level_gpio);
		dev_err(&pdev->dev, "failed to request GPIO: %d\n", err);
		return err;
	}

	err = of_property_read_u32(np, "default-level", &def_level);
	if (err) {
		dev_err(&pdev->dev, "Can't parse the level property\n");
		return err;
	}

	if(def_level > NUM_STEPS) {
		dev_info(&pdev->dev, 
		         "FAN5345 level of %d is out of range. Setting to %d.\n", 
		         def_level, NUM_STEPS);
		def_level = NUM_STEPS;
	}

	memset(&props, 0, sizeof(props));
	props.type = BACKLIGHT_RAW;
	props.max_brightness = NUM_STEPS;

	fanbl->cur_level = 0;

	bl = devm_backlight_device_register(&pdev->dev, dev_name(&pdev->dev),
	                                    &pdev->dev, fanbl, &fan5345bl_ops,
	                                    &props);

	if (IS_ERR(bl)) {
		dev_err(&pdev->dev, "failed to register backlight\n");
		return PTR_ERR(bl);
	}

	platform_set_drvdata(pdev, bl);

	if(def_level) {
		bl->props.brightness = def_level;
		err = backlight_update_status(bl);
		bl->props.state &= ~BL_CORE_FBBLANK;
		bl->props.power = FB_BLANK_UNBLANK;
	}

	if(!err)
		dev_info(&pdev->dev, "Driver Initialized.\n");

	return err;
}

static int fan5345_remove(struct platform_device *pdev)
{
	int err;
	struct backlight_device *bl = platform_get_drvdata(pdev);
	bl->props.power = 0;
	bl->props.brightness = 0;
	err = backlight_update_status(bl);

	if(!err)
		dev_info(&pdev->dev, "Driver Unloaded.\n");

	return 0;
}

static struct platform_driver fan5345_driver = {
	.driver = {
		.name   = "fan5345-bl",
		.of_match_table = fan5345_dt_ids,
	},
	.probe      = fan5345_probe,
	.remove     = fan5345_remove,
};

module_platform_driver(fan5345_driver);

MODULE_AUTHOR("David Lockhart <dlockhart@pendar.com>");
MODULE_DESCRIPTION("Fairchild Semiconductor FAN5345 LED Driver");
MODULE_LICENSE("GPL");