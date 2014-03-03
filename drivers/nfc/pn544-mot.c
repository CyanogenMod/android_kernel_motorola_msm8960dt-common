/*
 * Copyright (C) 2011 NXP Semiconductors
 * Copyright (C) 2013 Motorola Mobility LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/nfc/pn544-mot.h>
#include <linux/reboot.h>
#include <linux/wakelock.h>

/*
 * Virtual device /sys/devices/virtual/misc/pn544/pn544_control_dev
**/
struct pn544_dev	{
	wait_queue_head_t	read_wq;
	struct mutex		read_mutex;
	struct mutex		ioctl_mutex;
	struct i2c_client	*client;
	struct miscdevice	pn544_device;
	struct device		*pn544_control_device;
	unsigned int		ven_gpio;
	unsigned int		firmware_gpio;
	unsigned int		irq_gpio;
	int			ven_polarity;
	unsigned int		discharge_delay;
	bool			irq_enabled;
	spinlock_t		irq_enabled_lock; /* irq lock for reading */
	struct notifier_block reboot_notify;
	struct wake_lock wakelock;
};

static void pn544_disable_irq(struct pn544_dev *pn544_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&pn544_dev->irq_enabled_lock, flags);
	if (pn544_dev->irq_enabled) {
		irq_set_irq_wake(pn544_dev->client->irq, 0);
		disable_irq_nosync(pn544_dev->client->irq);
		pn544_dev->irq_enabled = false;
	}
	spin_unlock_irqrestore(&pn544_dev->irq_enabled_lock, flags);
}

static irqreturn_t pn544_dev_irq_handler(int irq, void *dev_id)
{
	struct pn544_dev *pn544_dev = dev_id;

	pn544_disable_irq(pn544_dev);

	wake_lock_timeout(&pn544_dev->wakelock, HZ);

	/* Wake up waiting readers */
	wake_up(&pn544_dev->read_wq);

	return IRQ_HANDLED;
}

static ssize_t pn544_dev_read(struct file *filp, char __user *buf,
		size_t count, loff_t *offset)
{
	struct pn544_dev *pn544_dev = filp->private_data;
	char *data;
	int ret;
	int tries = 0;

	data = kzalloc(count, GFP_KERNEL);
	if (!data) {
		pr_err("%s: could not allocate buffer\n", __func__);
		return -ENOMEM;
	}

	pr_debug("%s : reading %zu bytes.\n", __func__, count);

	mutex_lock(&pn544_dev->read_mutex);

	if (!gpio_get_value(pn544_dev->irq_gpio)) {
		if (filp->f_flags & O_NONBLOCK) {
			ret = -EAGAIN;
			mutex_unlock(&pn544_dev->read_mutex);
			goto free_buf;
		}

		pn544_dev->irq_enabled = true;
		irq_set_irq_wake(pn544_dev->client->irq, 1);
		enable_irq(pn544_dev->client->irq);
		ret = wait_event_interruptible(pn544_dev->read_wq,
				gpio_get_value(pn544_dev->irq_gpio));

		pn544_disable_irq(pn544_dev);

		if (ret) {
			mutex_unlock(&pn544_dev->read_mutex);
			goto free_buf;
		}

	}

	/* Read data */
	do {
		ret = i2c_master_recv(pn544_dev->client, data, count);
		if (ret < 0)
			msleep_interruptible(15);
	} while ((ret < 0) && (++tries < 5));

	mutex_unlock(&pn544_dev->read_mutex);

	if (ret < 0) {
		pr_err("%s: i2c_master_recv failed, returned %d\n",
			__func__, ret);
		goto free_buf;
	}

	if (copy_to_user(buf, data, count)) {
		pr_err("%s : failed to copy to user space\n", __func__);
		ret = -EFAULT;
	}

free_buf:
	kfree(data);
	pr_debug("%s : read %d\n", __func__, ret);
	return ret;
}

static ssize_t pn544_dev_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offset)
{
	struct pn544_dev *pn544_dev = filp->private_data;
	char *data;
	int ret;
	int tries = 0;

	data = kzalloc(count, GFP_KERNEL);
	if (!data) {
		pr_err("%s: could not allocate buffer\n", __func__);
		return -ENOMEM;
	}

	if (copy_from_user(data, buf, count)) {
		pr_err("%s : failed to copy from user space\n", __func__);
		ret = -EFAULT;
		goto free_buf;
	}

	pr_debug("%s : writing %zu bytes.\n", __func__, count);

	/* Write data */
	do {
		ret = i2c_master_send(pn544_dev->client, data, count);
		if (ret < 0)
			msleep_interruptible(15);
	} while ((ret < 0) && (++tries < 5));

	if (ret < 0)
		pr_err("%s : i2c_master_send failed, returned %d\n",
			__func__, ret);

free_buf:
	kfree(data);
	return ret;
}

static int pn544_dev_open(struct inode *inode, struct file *filp)
{
	struct pn544_dev *pn544_dev = container_of(filp->private_data,
						struct pn544_dev,
						pn544_device);

	filp->private_data = pn544_dev;

	pr_info("%s : %d,%d\n", __func__, imajor(inode), iminor(inode));

	return 0;
}

static int pn544_dev_ioctl(struct pn544_dev *pn544_dev,
				unsigned int cmd,
				unsigned long arg)
{
	int ven_logic_high = 1;
	int ven_logic_low = 0;
	unsigned int delay = pn544_dev->discharge_delay;

	if (pn544_dev->ven_polarity == 1) {
		ven_logic_high = 0;
		ven_logic_low = 1;
	}
	pr_info("%s ven_logic_high = %d.\n", __func__, ven_logic_high);
	pr_info("%s ven_logic_low = %d.\n", __func__, ven_logic_low);
	pr_info("%s discharge_delay = %d.\n", __func__, delay);

	switch (cmd) {
	case PN544_SET_PWR:
		if (arg == 2) {
			/* power on with firmware download (requires hw reset)
			 */
			pr_info("%s : power on with firmware update.\n",
				__func__);
			gpio_set_value(pn544_dev->ven_gpio, ven_logic_high);
			msleep(10);
			gpio_set_value(pn544_dev->firmware_gpio, 1);
			msleep(10);
			gpio_set_value(pn544_dev->ven_gpio, ven_logic_low);
			if (delay != 0)
				msleep(delay);
			else
				msleep(10);
			gpio_set_value(pn544_dev->ven_gpio, ven_logic_high);
		} else if (arg == 1) {
			/* power on */
			pr_info("%s : normal power on\n", __func__);
			gpio_set_value(pn544_dev->firmware_gpio, 0);
			msleep(10);
			gpio_set_value(pn544_dev->ven_gpio, ven_logic_high);
		} else if (arg == 0) {
			/* power off */
			pr_info("%s : power off\n", __func__);
			gpio_set_value(pn544_dev->firmware_gpio, 0);
			msleep(10);
			gpio_set_value(pn544_dev->ven_gpio, ven_logic_low);
			if (delay != 0)
				msleep(delay);
			else
				msleep(10);
		} else {
			pr_err("%s : bad arg %lu\n", __func__, arg);
			return -EINVAL;
		}
		break;
	default:
		pr_err("%s : bad ioctl %u\n", __func__, cmd);
		return -EINVAL;
	}

	return 0;
}

static int nfc_blk_reboot_notify(struct notifier_block *notify_block,
					unsigned long mode, void *unused)
{
	int ret = 0;
	struct pn544_dev *pn544_dev = container_of(
		notify_block, struct pn544_dev, reboot_notify);

	pr_info("pn544 reboot notification received\n");
	if (pn544_dev != NULL) {
		pn544_dev->discharge_delay = 0;
		ret = pn544_dev_ioctl(pn544_dev, PN544_SET_PWR, 0);
		pr_info("pn544 calling ioctl power off returned = %d\n", ret);
	}

	return NOTIFY_DONE;
}

static long pn544_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pn544_dev *pn544_dev;
	int ret = 0;

    pn544_dev = file->private_data;

	mutex_lock(&pn544_dev->ioctl_mutex);

	ret = pn544_dev_ioctl(pn544_dev, cmd, arg);

	if (ret != 0) {
		pr_err ("pn544_ioctl return error\n");
	}
	mutex_unlock(&pn544_dev->ioctl_mutex);

	return ret;
}

static const struct file_operations pn544_dev_fops = {
	.owner	= THIS_MODULE,
	.llseek	= no_llseek,
	.read	= pn544_dev_read,
	.write	= pn544_dev_write,
	.open	= pn544_dev_open,
	.unlocked_ioctl = pn544_ioctl,
};

static ssize_t pn544_control_func(struct device *dev,
				struct device_attribute *attr,
				const char *buf,
				size_t count)
{
	struct miscdevice *pn544_device = dev_get_drvdata(dev);
	struct pn544_dev *pn544_dev = container_of(pn544_device,
		struct pn544_dev, pn544_device);

	unsigned long val = simple_strtoul(buf, NULL, 10);
	int ret = 0;

	pr_info("pn544_control_func called value in ioctl is = %ld\n", val);

	ret = pn544_dev_ioctl(pn544_dev, PN544_SET_PWR, val);

	if (ret != 0)
		pr_err("pn544_control_function ioctl return error\n");

	return count;
}

static ssize_t pn544_debug_read(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{

	pr_info("pn544_debug_read\n");
	return snprintf(buf, 50, "pn544_debug_read returning\n");
}

static DEVICE_ATTR(pn544_control_dev, 0660, pn544_debug_read,
			pn544_control_func);

static int pn544_gpio_init(struct pn544_i2c_platform_data *pdata)
{
	int ret;

	/* Validate and get the IRQ line */
	ret = gpio_request(pdata->irq_gpio, "pn544 irq");
	if (ret)
		return ret;

	gpio_direction_input(pdata->irq_gpio);
	ret = gpio_export(pdata->irq_gpio, 0);
	if (ret)
		goto free_irq;

	/* Validate and get the reset line */
	ret = gpio_request(pdata->ven_gpio, "pn544 reset");
	if (ret)
		goto free_irq;

	/* Set VEN GPIO to disabled/reset, whose value depends on polarity */
	gpio_direction_output(pdata->ven_gpio, pdata->ven_polarity ? 1 : 0);
	ret = gpio_export(pdata->ven_gpio, 0);
	if (ret)
		goto free_ven;

	/* Validate and get the firmware line */
	ret = gpio_request(pdata->firmware_gpio, "pn544 firmware download");
	if (ret)
		goto free_ven;

	gpio_direction_output(pdata->firmware_gpio, 0);
	ret = gpio_export(pdata->firmware_gpio, 0);
	if (ret)
		goto free_fw;

	return 0;

free_fw:
	gpio_free(pdata->firmware_gpio);
free_ven:
	gpio_free(pdata->ven_gpio);
free_irq:
	gpio_free(pdata->irq_gpio);
	return ret;
}

static void pn544_gpio_free(struct pn544_dev *dev)
{
	gpio_free(dev->firmware_gpio);
	gpio_free(dev->ven_gpio);
	gpio_free(dev->irq_gpio);
}

#ifdef CONFIG_OF
static struct pn544_i2c_platform_data *
pn544_of_init(struct i2c_client *client)
{
	struct pn544_i2c_platform_data *pdata;
	struct device_node *np = client->dev.of_node;

	pdata = devm_kzalloc(&client->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(&client->dev, "pdata allocation failure\n");
		return NULL;
	}

	of_property_read_u32(np, "nxp,pnxxx-discharge-delay",
				&pdata->discharge_delay);
	of_property_read_u32(np, "nxp,pnxxx-ven-inv-polarity",
				&pdata->ven_polarity);

	pdata->irq_gpio = of_get_gpio(np, 0);
	pdata->ven_gpio = of_get_gpio(np, 1);
	pdata->firmware_gpio = of_get_gpio(np, 2);

	return pdata;
}
#else
static inline struct pn544_i2c_platform_data *
pn544_of_init(struct i2c_client *client)
{
	return NULL;
}
#endif

static int pn544_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret = -ENODEV;
	struct pn544_i2c_platform_data *platform_data;
	struct pn544_dev *pn544_dev;

	pr_debug("%s : Probing pn544 driver\n", __func__);

	if (client->dev.of_node)
		platform_data = pn544_of_init(client);
	else
		platform_data = client->dev.platform_data;

	if (!platform_data) {
		pr_err("%s : GPIO has value 0, nfc probe fail.\n", __func__);
		goto err_exit;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s : i2c_check_functionality I2C_FUNC_I2C failed.\n",
			__func__);
		goto err_exit;
	}

	pn544_dev = kzalloc(sizeof(*pn544_dev), GFP_KERNEL);
	if (pn544_dev == NULL) {
		dev_err(&client->dev,
				"failed to allocate memory for module data\n");
		ret = -ENOMEM;
		goto err_exit;
	}

	ret = pn544_gpio_init(platform_data);
	if (ret) {
		dev_err(&client->dev, "gpio init failed\n");
		goto err_gpio_init;
	}

	pn544_dev->irq_gpio = platform_data->irq_gpio;
	pn544_dev->ven_gpio  = platform_data->ven_gpio;
	pn544_dev->firmware_gpio = platform_data->firmware_gpio;
	pn544_dev->ven_polarity = platform_data->ven_polarity;
	pn544_dev->discharge_delay = platform_data->discharge_delay;
	pn544_dev->client   = client;

	wake_lock_init(&pn544_dev->wakelock, WAKE_LOCK_SUSPEND, "pn544c3");

	/* init mutex and queues */
	init_waitqueue_head(&pn544_dev->read_wq);
	mutex_init(&pn544_dev->read_mutex);
	mutex_init(&pn544_dev->ioctl_mutex);
	spin_lock_init(&pn544_dev->irq_enabled_lock);

	pn544_dev->pn544_device.minor = MISC_DYNAMIC_MINOR;
	pn544_dev->pn544_device.name = "pn544";
	pn544_dev->pn544_device.fops = &pn544_dev_fops;

	ret = misc_register(&pn544_dev->pn544_device);
	if (ret) {
		pr_err("%s : misc_register failed.\n", __FILE__);
		goto err_misc_register;
	}

	pr_debug("%s : PN544 Misc Minor: %d\n",
		__func__, pn544_dev->pn544_device.minor);

	/* Get the device structure */
	pn544_dev->pn544_control_device = pn544_dev->pn544_device.this_device;

	/* Create sysfs device for PN544 control functionality */
	ret = device_create_file(pn544_dev->pn544_control_device,
				&dev_attr_pn544_control_dev);
	if (ret) {
		pr_err("%s : device_create_file failed\n", __FILE__);
		goto err_device_create_file_failed;
	}

	/* request irq.  the irq is set whenever the chip has data available
	 * for reading.  it is cleared when all data has been read.
	 */
	pr_debug("%s : requesting IRQ %d\n", __func__, client->irq);
	pn544_dev->irq_enabled = true;
	ret = request_irq(client->irq, pn544_dev_irq_handler,
			  IRQF_TRIGGER_HIGH, client->name, pn544_dev);
	if (ret) {
		dev_err(&client->dev, "request_irq failed\n");
		goto err_request_irq_failed;
	}
	if (unlikely(irq_set_irq_wake(client->irq, 1)))
		pr_err("%s : unable to make irq %d wakeup\n", __func__,
					client->irq);
	pn544_disable_irq(pn544_dev);
	pn544_dev->reboot_notify.notifier_call = nfc_blk_reboot_notify;
	register_reboot_notifier(&pn544_dev->reboot_notify);
	i2c_set_clientdata(client, pn544_dev);

	return 0;

err_request_irq_failed:
	device_remove_file(pn544_dev->pn544_control_device,
				&dev_attr_pn544_control_dev);
err_device_create_file_failed:
	misc_deregister(&pn544_dev->pn544_device);
err_misc_register:
	mutex_destroy(&pn544_dev->read_mutex);
	mutex_destroy(&pn544_dev->ioctl_mutex);
	pn544_gpio_free(pn544_dev);
	wake_unlock(&pn544_dev->wakelock);
	wake_lock_destroy(&pn544_dev->wakelock);
err_gpio_init:
	kfree(pn544_dev);
err_exit:
	return ret;
}

static int pn544_remove(struct i2c_client *client)
{
	struct pn544_dev *pn544_dev;

	pn544_dev = i2c_get_clientdata(client);
	unregister_reboot_notifier(&pn544_dev->reboot_notify);
	free_irq(client->irq, pn544_dev);
	pn544_gpio_free(pn544_dev);
	device_remove_file(pn544_dev->pn544_control_device,
				&dev_attr_pn544_control_dev);
	misc_deregister(&pn544_dev->pn544_device);
	mutex_destroy(&pn544_dev->read_mutex);
	mutex_destroy(&pn544_dev->ioctl_mutex);
	wake_unlock(&pn544_dev->wakelock);
	wake_lock_destroy(&pn544_dev->wakelock);
	kfree(pn544_dev);

	return 0;
}

static const struct i2c_device_id pn544_id[] = {
	{ "pn544", 0 },
	{ }
};

#ifdef CONFIG_OF
static struct of_device_id pn544_match_tbl[] = {
	{ .compatible = "nxp,pn544" },
	{ },
};
MODULE_DEVICE_TABLE(of, pn544_match_tbl);
#endif

static struct i2c_driver pn544_driver = {
	.id_table	= pn544_id,
	.probe		= pn544_probe,
	.remove		= pn544_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "pn544",
		.of_match_table = of_match_ptr(pn544_match_tbl),
	},
};

static int __init pn544_dev_init(void)
{
	pr_info("Loading pn544 driver\n");
	return i2c_add_driver(&pn544_driver);
}
module_init(pn544_dev_init);

static void __exit pn544_dev_exit(void)
{
	pr_info("Unloading pn544 driver\n");
	i2c_del_driver(&pn544_driver);
}
module_exit(pn544_dev_exit);

MODULE_AUTHOR("Motorola Mobility");
MODULE_DESCRIPTION("NFC PN544 driver");
MODULE_LICENSE("GPL");
