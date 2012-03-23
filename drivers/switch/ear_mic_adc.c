/*
 *  drivers/switch/ear_mic_adc.c
 *
 * Copyright (C) 2011 Google, Inc.
 * Author: Venkappa Mala <venkappa.m@samsung.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/switch.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/i2c/twl.h>
#include <linux/mfd/twl6040-codec.h>
#include <linux/i2c/twl6030-gpadc.h>

#undef DEBUG_SEC_HEADSET

#ifdef DEBUG_SEC_HEADSET
#define SEC_HEADSET_DBG(fmt, arg...) printk(KERN_INFO "[HEADSET] %s () " fmt "\r\n", __func__, ## arg)
#else
#define SEC_HEADSET_DBG(fmt, arg...) do {} while(0)
#endif

#define EAR_MICBIAS_EN2_GPIO		49
#define OMAP4TAB_EARPATH_SEL_GPIO	44
#define OMAP4TAB_EAR_SEND_END_GPIO	94

#define HEADSET_ATTACH_COUNT		3
#define HEADSET_DETACH_COUNT		5
#define HEADSET_CHECK_TIME		get_jiffies_64() + (HZ/20)// 1000ms / 20 = 50ms

#define HEADSET_4POLE_WITH_MIC		1
#define HEADSET_3POLE			2

#define MODULE_NAME "ear_mic_adc"


static unsigned int count;
static unsigned int headset_detect_timer_token;
static unsigned short int headset_status;
static struct timer_list headset_detect_timer;
struct work_struct ear_adc_detect_work;

static struct twl6030_gpadc_request conv_request = {
	.channels = (0x1 << 2),
	.do_avg = 0,
	.method = TWL6030_GPADC_SW2,
	.type = 0,
	.active = 0,
	.result_pending = 0,
	.func_cb = NULL,
};

static void ear_adc_calculator(struct work_struct *work)
{
	u8 value, state;
	int ear_adc_out = 0;

	del_timer(&headset_detect_timer);

	twl_i2c_read_u8(TWL_MODULE_AUDIO_VOICE, &value, TWL6040_REG_STATUS);
	state = value & TWL6040_PLUGCOMP;

	/* Debounce */
	msleep(100);

	if (state) {
		printk("[Headset] Headset attached\n");
		twl6030_gpadc_conversion(&conv_request);
		ear_adc_out = conv_request.rbuf[2];
	
		if(ear_adc_out > 230) {
			headset_status = HEADSET_4POLE_WITH_MIC;
			printk("[Headset] 4pole ear-mic adc is %d\n", ear_adc_out);
		} else {
			headset_status = HEADSET_3POLE;
			printk("[Headset] 3pole earphone adc is %d\n", ear_adc_out);
		} 
	} else {
		printk("[Headset] Headset detached\n");
		//gpio_direction_output(EAR_MICBIAS_EN2_GPIO, 1);
	}
}

static DECLARE_DELAYED_WORK(ear_adc_cal_work, ear_adc_calculator);

static void headset_detect_timer_handler(unsigned long arg)
{
	
	del_timer(&headset_detect_timer);

	if(headset_detect_timer_token < count) 
	{
		headset_detect_timer.expires = HEADSET_CHECK_TIME;
		add_timer(&headset_detect_timer);
		headset_detect_timer_token++;
		SEC_HEADSET_DBG("detect timer token count is %d", count);
	} 
	else if(headset_detect_timer_token == count)
	{
		cancel_delayed_work_sync(&ear_adc_cal_work);
		schedule_delayed_work(&ear_adc_cal_work, 20);
		SEC_HEADSET_DBG("add work queue - timer token is %d", count);
		headset_detect_timer_token = 0;
	} 
	else
	{
		printk(KERN_ALERT "wrong headset_detect_timer_token count %d", headset_detect_timer_token);
		gpio_direction_output(EAR_MICBIAS_EN2_GPIO, 0);	
	}
	
}
static void ear_adc_detect_cal_work(struct work_struct *work)
{
	u8 value, state;
	
	twl_i2c_read_u8(TWL_MODULE_AUDIO_VOICE, &value, TWL6040_REG_STATUS);
	state = value & TWL6040_PLUGCOMP;

	del_timer(&headset_detect_timer);
	cancel_delayed_work_sync(&ear_adc_cal_work);

	if (state || !state)
	{
		SEC_HEADSET_DBG("Headset attached timer start");
		headset_detect_timer_token = 0;
		headset_detect_timer.expires = HEADSET_CHECK_TIME;
		add_timer(&headset_detect_timer);
	}
	 else
		SEC_HEADSET_DBG("Headset state does not valid. or send_end event");
	
	if(state)
		count = HEADSET_ATTACH_COUNT;
	else if(!state)
		count = HEADSET_DETACH_COUNT;

}

/* Ear MIC adc IRQ handler */
static irqreturn_t ear_mic_adc_irq_handler(int irq, void *data)
{
	//gpio_direction_output(EAR_MICBIAS_EN2_GPIO, 0); //for detach noise
	schedule_work(&ear_adc_detect_work);
	return IRQ_HANDLED;
}

static int ear_mic_adc_probe(struct platform_device *pdev)
{
	struct input_dev *ear_key=NULL;
	int ret = 0, ear_mic_adc_irq = -1;
	unsigned ear_mic_gpio_ip, ear_mic_gpio_op;

#ifdef DEBUG_SEC_HEADSET
	printk(KERN_INFO MODULE_NAME ": Registering EAR_MIC_ADC driver\n");
#endif


	ear_mic_gpio_op = EAR_MICBIAS_EN2_GPIO;
	gpio_request(ear_mic_gpio_op, "EAR_MICBIAS_EN2_GPIO");
	ret = gpio_direction_output(ear_mic_gpio_op, 1);
	if (ret < 0) {
		printk("%s:%d EAR_MICBIAS_EN2_GPIO gpio_direction_output fails\n",__FILE__,__LINE__);
		goto err_set_gpio_output;
	}

	/* 
	 * EARPATH_SEL - low -> TV_OUT 
	 * EARPATH_SEL - high -> EAR_MIC_3.5
	 */
	ear_mic_gpio_op = OMAP4TAB_EARPATH_SEL_GPIO;
	gpio_request(ear_mic_gpio_op, "Switch to EAR_MIC");
	ret = gpio_direction_output(ear_mic_gpio_op, 1);
	if (ret < 0) {
		printk("%s:%d Switch to EAR_MIC gpio_direction_output fails\n",__FILE__,__LINE__);
		goto err_set_gpio_output;
	}

	ear_mic_gpio_ip = OMAP4TAB_EAR_SEND_END_GPIO;
	gpio_request(ear_mic_gpio_ip, "EAR_SEND_END");
	ret = gpio_direction_input(ear_mic_gpio_ip);
	if (ret < 0) {
		printk("%s:%d EAR_SEND_END gpio_direction_input fails\n",__FILE__,__LINE__);
		goto err_set_gpio_input;
	}

	INIT_WORK(&ear_adc_detect_work, ear_adc_detect_cal_work);

	ear_mic_adc_irq = gpio_to_irq(ear_mic_gpio_ip);
	if (ear_mic_adc_irq < 0) {
		printk("%s:%d EAR_SEND_END gpio_to_irq fails\n",__FILE__,__LINE__);
		goto err_detect_irq_num_failed;
	}

	ear_key = input_allocate_device();
	if(!ear_key) {
		printk("%s:%d failed to allocate an input devd %d \n",__FILE__,__LINE__,ear_mic_adc_irq);
		ret = -ENOMEM;
		return ret;
	}
	ret = request_irq(ear_mic_adc_irq, &ear_mic_adc_irq_handler,
				IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
				"ear-mic-adc-3.5", ear_key);
	if (ret < 0)
		goto err_request_irq;

	ear_key->name = "ear_mic_adc_driver";
	ear_key->phys = "ear_mic_adc_driver/input3";
	ear_key->dev.parent = &pdev->dev;
	platform_set_drvdata(pdev, ear_key);

	ret = input_register_device(ear_key);
	if (ret) {
		goto release_irq_num;
	}

	init_timer(&headset_detect_timer);
	headset_detect_timer.function = headset_detect_timer_handler;		

	return ret;

err_request_irq:
err_detect_irq_num_failed:
err_set_gpio_input:
	gpio_free(ear_mic_gpio_ip);
err_set_gpio_output:
	gpio_free(ear_mic_gpio_op);
release_irq_num:
	free_irq(ear_mic_adc_irq,NULL); //pass devID as NULL as device registration failed

}

static int __devexit ear_mic_adc_remove(struct platform_device *pdev)
{
	struct input_dev *idev= platform_get_drvdata(pdev);
#ifdef DEBUG_SEC_HEADSET
	printk(KERN_INFO MODULE_NAME ": Unregistered EAR_MIC_ADC driver\n");
#endif
	free_irq(gpio_to_irq(OMAP4TAB_EAR_SEND_END_GPIO), idev);
	gpio_free(OMAP4TAB_EAR_SEND_END_GPIO);
	input_unregister_device(idev);

	return 0;
}

static int ear_mic_adc_suspend()
{
	return 0;
}

static int ear_mic_adc_resume()
{
	return 0;
}

static struct platform_driver ear_mic_adc_driver = {
	.probe		= ear_mic_adc_probe,
	.remove		= __devexit_p(ear_mic_adc_remove),
	.driver		= {
		.name	= MODULE_NAME,
		.owner	= THIS_MODULE,
	},
	.suspend = ear_mic_adc_suspend,
	.resume = ear_mic_adc_resume
};

static int __init ear_mic_adc_init(void)
{
	return platform_driver_register(&ear_mic_adc_driver);
}

static void __exit ear_mic_adc_exit(void)
{
	platform_driver_unregister(&ear_mic_adc_driver);
}

module_init(ear_mic_adc_init);
module_exit(ear_mic_adc_exit);

MODULE_AUTHOR("Venkappa Mala <venkappa.m@samsung.com>");
MODULE_DESCRIPTION("EAR mic adc driver");
MODULE_LICENSE("GPL");
