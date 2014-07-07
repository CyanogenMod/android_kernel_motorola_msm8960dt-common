/* Copyright (c) 2011-2013, Motorola Mobility LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <asm/hardware/gic.h>
#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <asm/mach/mmc.h>
#include <asm/setup.h>
#include <asm/system_info.h>
#include <asm/bootinfo.h>

#include <linux/bootmem.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_fdt.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/persistent_ram.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sysdev.h>
#include <linux/seq_file.h>

#include <mach/devtree_util.h>
#include <mach/gpio.h>
#include <mach/gpiomux.h>
#include <mach/mpm.h>
#include <mach/msm_smsm.h>
#include <mach/restart.h>
#include <mach/socinfo.h>

#include "board-8960.h"
#include "board-mmi.h"
#include "devices-mmi.h"
#include "msm_watchdog.h"
#include "timer.h"

static void (*msm8960_common_cal_rsv_sizes)(void) __initdata;

static void __init msm8960_mmi_cal_rsv_sizes(void)
{
	if (msm8960_common_cal_rsv_sizes)
		msm8960_common_cal_rsv_sizes();
	reserve_memory_for_watchdog();
	reserve_info->memtype_reserve_table[MEMTYPE_EBI1].size
		+= bootinfo_bck_size();
	bootinfo_bck_buf_set_reserved();
}

static void __init mmi_msm8960_reserve(void)
{
	msm8960_common_cal_rsv_sizes = reserve_info->calculate_reserve_sizes;
	reserve_info->calculate_reserve_sizes = msm8960_mmi_cal_rsv_sizes;
	msm8960_reserve();

#ifdef CONFIG_ANDROID_RAM_CONSOLE
	persistent_ram_early_init(&mmi_ram_console_pram);
#endif
}

static u32 fdt_start_address; /* flattened device tree address */
static u32 fdt_size;
static u32 prod_id;

#define EXPECTED_MBM_PROTOCOL_VER 2
static u32 mbmprotocol;

struct dt_gpiomux {
	u16 gpio;
	u8 setting;
	u8 func;
	u8 pull;
	u8 drv;
	u8 dir;
} __attribute__ ((__packed__));

#define DT_PATH_MUX		"/System@0/IOMUX@0"
#define DT_PROP_MUX_SETTINGS	"settings"

#define BOOT_MODE_MAX_LEN 64
static char boot_mode[BOOT_MODE_MAX_LEN + 1];
int __init board_boot_mode_init(char *s)
{
	strlcpy(boot_mode, s, BOOT_MODE_MAX_LEN);
	boot_mode[BOOT_MODE_MAX_LEN] = '\0';
	return 1;
}
__setup("androidboot.mode=", board_boot_mode_init);

static int mmi_boot_mode_is_factory(void)
{
	return !strncmp(boot_mode, "factory", BOOT_MODE_MAX_LEN);
}

#define BATTERY_DATA_MAX_LEN 32
static char battery_data[BATTERY_DATA_MAX_LEN+1];
int __init board_battery_data_init(char *s)
{
	strlcpy(battery_data, s, BATTERY_DATA_MAX_LEN);
	battery_data[BATTERY_DATA_MAX_LEN] = '\0';
	return 1;
}
__setup("battery=", board_battery_data_init);

static int mmi_battery_data_is_meter_locked(void)
{
	return !strncmp(battery_data, "meter_lock", BATTERY_DATA_MAX_LEN);
}

static int mmi_battery_data_is_no_eprom(void)
{
	return !strncmp(battery_data, "no_eprom", BATTERY_DATA_MAX_LEN);
}

static void __init mmi_gpiomux_init(struct msm8960_oem_init_ptrs *oem_ptr)
{
	struct device_node *node;
	const struct dt_gpiomux *prop;
	int i;
	int size = 0;
	struct gpiomux_setting setting;
	u16 gpio;

	/* Override the default setting by devtree.  Do not manually
	 * install via msm_gpiomux_install hardcoded values.
	 */
	node = of_find_node_by_path(DT_PATH_MUX);
	if (!node) {
		pr_info("%s: no node found: %s\n", __func__, DT_PATH_MUX);
		return;
	}
	prop = (const void *)of_get_property(node, DT_PROP_MUX_SETTINGS, &size);
	if (prop && ((size % sizeof(struct dt_gpiomux)) == 0)) {
		for (i = 0; i < size / sizeof(struct dt_gpiomux); i++) {
			setting.func = prop->func;
			setting.drv = prop->drv;
			setting.pull = prop->pull;
			setting.dir = prop->dir;
			gpio = be16_to_cpup((__be16 *)&prop->gpio);
			if (msm_gpiomux_write(gpio, prop->setting,
						&setting, NULL))
				pr_err("%s: gpio%d mux setting %d failed\n",
					__func__, gpio, prop->setting);
			prop++;
		}
	}

	of_node_put(node);
}

static ssize_t cid_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%08X 0x%08X 0x%08X 0x%08X\n",
			k_atag_tcmd_raw_cid[0], k_atag_tcmd_raw_cid[1],
			k_atag_tcmd_raw_cid[2], k_atag_tcmd_raw_cid[3]);
}

static ssize_t csd_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%08X 0x%08X 0x%08X 0x%08X\n",
			k_atag_tcmd_raw_csd[0], k_atag_tcmd_raw_csd[1],
			k_atag_tcmd_raw_csd[2], k_atag_tcmd_raw_csd[3]);
}

static ssize_t ecsd_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf)
{
	char *d = buf;
	char b[8];
	int i = 0;

	while (i < 512) {
		snprintf(b, 8, "%02X", k_atag_tcmd_raw_ecsd[i]);
		*d++ = b[0];
		*d++ = b[1];
		*d++ = ' ';
		i++;
	}
	*d++ = 10;
	*d = 0;

	return (512*3) + 1;
}

static struct kobj_attribute cid_attribute =
	__ATTR(cid, 0444, cid_show, NULL);

static struct kobj_attribute csd_attribute =
	__ATTR(csd, 0444, csd_show, NULL);

static struct kobj_attribute ecsd_attribute =
	__ATTR(ecsd, 0444, ecsd_show, NULL);

static struct attribute *emmc_attrs[] = {
	&cid_attribute.attr,
	&csd_attribute.attr,
	&ecsd_attribute.attr,
	NULL
};

static struct attribute_group emmc_attr_group = {
	.attrs = emmc_attrs,
};

static int emmc_version_init(void)
{
	static struct kobject *emmc_kobj;
	int retval;

	emmc_kobj = kobject_create_and_add("emmc", NULL);
	if (!emmc_kobj) {
		pr_err("%s: failed to create /sys/emmc\n", __func__);
		return -ENOMEM;
	}

	retval = sysfs_create_group(emmc_kobj, &emmc_attr_group);
	if (retval)
		pr_err("%s: failed for entries under /sys/emmc\n", __func__);

	return retval;
}

static struct msm_i2c_platform_data mmi_msm8960_i2c_qup_gsbi12_pdata = {
	.clk_freq = 100000,
	.src_clk_rate = 24000000,
};


/* This sysfs allows sensor TCMD to switch the control of I2C-12
 *  from DSPS to Krait at runtime by issuing the following command:
 *	echo 1 > /sys/kernel/factory_gsbi12_mode/install
 * Upon phone reboot, everything will be back to normal.
 */
static ssize_t factory_gsbi12_mode_install_set(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	int Error;

	mmi_msm8960_device_qup_i2c_gsbi12.dev.platform_data =
				&mmi_msm8960_i2c_qup_gsbi12_pdata;
	Error = platform_device_register(&mmi_msm8960_device_qup_i2c_gsbi12);

	if (Error)
		printk(KERN_ERR "%s: failed to register gsbi12\n", __func__);

	/* We must return # of bytes used from buffer
	(do not return 0,it will throw an error) */
	return count;
}

static struct kobj_attribute factory_gsbi12_mode_install_attribute =
	__ATTR(install, S_IRUGO|S_IWUSR, NULL, factory_gsbi12_mode_install_set);
static struct kobject *factory_gsbi12_mode_kobj;

static int sysfs_factory_gsbi12_mode_init(void)
{
	int retval;

	/* creates a new folder(node) factory_gsbi12_mode under /sys/kernel */
	factory_gsbi12_mode_kobj = kobject_create_and_add("factory_gsbi12_mode",
			kernel_kobj);
	if (!factory_gsbi12_mode_kobj)
		return -ENOMEM;

	/* creates a file named install under /sys/kernel/factory_gsbi12_mode */
	retval = sysfs_create_file(factory_gsbi12_mode_kobj,
				&factory_gsbi12_mode_install_attribute.attr);
	if (retval)
		kobject_put(factory_gsbi12_mode_kobj);

	return retval;
}

static void __init mmi_gsbi_init(struct msm8960_oem_init_ptrs *oem_ptr)
{
	mmi_init_gsbi_devices_from_dt();
}

static void __init mmi_gpio_mpp_init(struct msm8960_oem_init_ptrs *oem_ptr)
{
	mmi_init_pm8921_gpio_mpp();
}

static void __init mmi_i2c_init(struct msm8960_oem_init_ptrs *oem_ptr)
{
	mmi_register_i2c_devices_from_dt();
}

static void __init mmi_pmic_init(struct msm8960_oem_init_ptrs *oem_ptr,
				 void *pdata)
{
	mmi_pm8921_init(oem_ptr->oem_data, pdata);
	mmi_pm8921_keypad_init(pdata);
}

static void __init mmi_clk_init(struct msm8960_oem_init_ptrs *oem_ptr,
				struct clock_init_data *data)
{
	struct clk_lookup *mmi_clks;
	int size;

	mmi_clks = mmi_init_clocks_from_dt(&size);
	if (mmi_clks) {
		data->oem_clk_tbl = mmi_clks;
		data->oem_clk_size = size;
	}
}

static struct platform_device *mmi_devices[] __initdata = {
	&mmi_w1_gpio_device,
#ifdef CONFIG_ANDROID_RAM_CONSOLE
	&mmi_ram_console_device,
#endif
	&mmi_pm8xxx_rgb_leds_device,
	&mmi_alsa_to_h2w_hs_device,
	&mmi_bq5101xb_device,
#ifdef CONFIG_EMU_DETECTION
	&msm8960_device_uart_gsbi,
#endif
};

#define SERIALNO_MAX_LEN 64
static char serialno[SERIALNO_MAX_LEN + 1];
int __init board_serialno_init(char *s)
{
	strncpy(serialno, s, SERIALNO_MAX_LEN);
	serialno[SERIALNO_MAX_LEN] = '\0';
	return 1;
}
__setup("androidboot.serialno=", board_serialno_init);

static char carrier[CARRIER_MAX_LEN + 1];
int __init board_carrier_init(char *s)
{
	strncpy(carrier, s, CARRIER_MAX_LEN);
	carrier[CARRIER_MAX_LEN] = '\0';
	return 1;
}
__setup("androidboot.carrier=", board_carrier_init);

static char baseband[BASEBAND_MAX_LEN + 1];
int __init board_baseband_init(char *s)
{
	strncpy(baseband, s, BASEBAND_MAX_LEN);
	baseband[BASEBAND_MAX_LEN] = '\0';
	return 1;
}
__setup("androidboot.baseband=", board_baseband_init);

static int bare_board;
int __init board_bareboard_init(char *s)
{
	if (!strcmp(s, "1"))
		bare_board = 1;
	else
		bare_board = 0;

	return 1;
}
__setup("bare_board=", board_bareboard_init);

#define ANDROIDBOOT_DEVICE_MAX_LEN 32
static char androidboot_device[ANDROIDBOOT_DEVICE_MAX_LEN + 1];
int __init setup_androidboot_device_init(char *s)
{
	strlcpy(androidboot_device, s, ANDROIDBOOT_DEVICE_MAX_LEN + 1);
	return 1;
}
__setup("androidboot.device=", setup_androidboot_device_init);

static unsigned int androidboot_radio;
int __init setup_androidboot_radio_init(char *s)
{
	int retval = kstrtouint(s, 16, &androidboot_radio);

	if (retval < 0)
		return 0;

	return 1;
}
__setup("androidboot.radio=", setup_androidboot_radio_init);

void mach_cpuinfo_show(struct seq_file *m, void *v)
{
	seq_printf(m, "Device\t\t: %s\n", androidboot_device);
	/* Zero is not a valid "Radio" value.      */
	/* Lack of "Radio" entry in cpuinfo means: */
	/*	look for radio in "Revision"       */
	if (androidboot_radio)
		seq_printf(m, "Radio\t\t: %x\n", androidboot_radio);
}

static char extended_baseband[BASEBAND_MAX_LEN+1] = "\0";
static int __init mot_parse_atag_baseband(const struct tag *tag)
{
	const struct tag_baseband *baseband_tag = &tag->u.baseband;
	strncpy(extended_baseband, baseband_tag->baseband, BASEBAND_MAX_LEN);
	extended_baseband[BASEBAND_MAX_LEN] = '\0';
	pr_info("%s: %s\n", __func__, extended_baseband);
	return 0;
}
__tagtable(ATAG_BASEBAND, mot_parse_atag_baseband);

static int mmi_unit_is_bareboard(void)
{
	return bare_board;
}

static uint32_t mmi_unit_get_radio(void)
{
	return androidboot_radio;
}

static void __init mmi_unit_info_init(void){
	struct mmi_unit_info *mui;

	#define SMEM_KERNEL_RESERVE_SIZE 1024
#ifdef CONFIG_MMI_JB_FIRMWARE
	#undef MMI_UNIT_INFO_VER
	#define MMI_UNIT_INFO_VER 1
	mui = (struct mmi_unit_info *) smem_alloc2(SMEM_KERNEL_RESERVE,
#else
	mui = (struct mmi_unit_info *) smem_alloc(SMEM_KERNEL_RESERVE,
#endif
		SMEM_KERNEL_RESERVE_SIZE);

	if (!mui) {
		pr_err("%s: failed to allocate mmi_unit_info in SMEM\n",
			__func__);
		return;
	}

	mui->version = MMI_UNIT_INFO_VER;
	mui->system_rev = system_rev;
	mui->system_serial_low = system_serial_low;
	mui->system_serial_high = system_serial_high;
	strlcpy(mui->machine, machine_desc->name, MACHINE_MAX_LEN + 1);
	strlcpy(mui->barcode, serialno, BARCODE_MAX_LEN + 1);
	strlcpy(mui->baseband, extended_baseband, BASEBAND_MAX_LEN + 1);
	strlcpy(mui->carrier, carrier, CARRIER_MAX_LEN + 1);
	strlcpy(mui->device, androidboot_device, DEVICE_MAX_LEN + 1);
	mui->radio = androidboot_radio;

	if (mui->version != MMI_UNIT_INFO_VER) {
		pr_err("%s: unexpected unit_info version %d in SMEM\n",
			__func__, mui->version);
	}

	pr_err("mmi_unit_info (SMEM) for modem: version = 0x%02x,"
		" device = '%s', radio = %d, system_rev = 0x%04x,"
		" system_serial = 0x%08x%08x, machine = '%s',"
		" barcode = '%s', baseband = '%s', carrier = '%s'\n",
		mui->version,
		mui->device, mui->radio, mui->system_rev,
		mui->system_serial_high, mui->system_serial_low,
		mui->machine, mui->barcode,
		mui->baseband, mui->carrier);
}

static int mot_tcmd_export_gpio(void)
{
	int rc;

	rc = gpio_request(1, "USB_HOST_EN");
	if (!rc) {
		gpio_direction_output(1, 0);
		rc = gpio_export(1, 0);
		if (rc) {
			pr_err("%s: GPIO USB_HOST_EN export failure\n",
					__func__);
			gpio_free(1);
		}
	}

	rc = gpio_request(PM8921_GPIO_PM_TO_SYS(36), "SIM_DET");
	if (!rc) {
		gpio_direction_input(PM8921_GPIO_PM_TO_SYS(36));
		rc = gpio_export(PM8921_GPIO_PM_TO_SYS(36), 0);
		if (rc) {
			pr_err("%s: GPIO SIM_DET export failure\n", __func__);
			gpio_free(PM8921_GPIO_PM_TO_SYS(36));
		}
	}

	/* RF connection detect GPIOs */
	rc = gpio_request(24, "RF_CONN_DET_2G3G");
	if (!rc) {
		gpio_direction_input(24);
		rc = gpio_export(24, 0);
		if (rc) {
			pr_err("%s: GPIO 24 export failure\n", __func__);
			gpio_free(24);
		}
	}

	rc = gpio_request(25, "RF_CONN_DET_LTE_1");
	if (!rc) {
		gpio_direction_input(25);
		rc = gpio_export(25, 0);
		if (rc) {
			pr_err("%s: GPIO 25 export failure\n", __func__);
			gpio_free(25);
		}
	}

	rc = gpio_request(81, "RF_CONN_DET_LTE_2");
	if (!rc) {
		gpio_direction_input(81);
		rc = gpio_export(81, 0);
		if (rc) {
			pr_err("%s: GPIO 81 export failure\n", __func__);
			gpio_free(81);
		}
	}
	return 0;
}

static ssize_t hw_rev_txt_pmic_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
/* Format: TYPE:VENDOR:HWREV:DATE:FIRMWARE_REV:INFO  */
	return snprintf(buf, PAGE_SIZE,
			"PMIC:QUALCOMM-PM8921:%s:::rev1=0x%02X,rev2=0x%02X\n",
			pmic_hw_rev_txt_version,
			pmic_hw_rev_txt_rev1,
			pmic_hw_rev_txt_rev2);
}

static ssize_t hw_rev_txt_display_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE,
			"Display:0x%02X:0x%02X::0x%02X:\n",
			display_hw_rev_txt_manufacturer,
			display_hw_rev_txt_controller,
			display_hw_rev_txt_controller_drv);
}

static struct kobj_attribute hw_rev_txt_pmic_attribute =
	__ATTR(pmic, 0444, hw_rev_txt_pmic_show, NULL);

static struct kobj_attribute hw_rev_txt_display_attribute =
	__ATTR(display, 0444, hw_rev_txt_display_show, NULL);

static struct attribute *hw_rev_txt_attrs[] = {
	&hw_rev_txt_pmic_attribute.attr,
	&hw_rev_txt_display_attribute.attr,
	NULL
};

static struct attribute_group hw_rev_txt_attr_group = {
	.attrs = hw_rev_txt_attrs,
};

static int hw_rev_txt_init(void)
{
	static struct kobject *hw_rev_txt_kobj;
	int retval;

	hw_rev_txt_kobj = kobject_create_and_add("hardware_revisions", NULL);
	if (!hw_rev_txt_kobj) {
		pr_err("%s: failed to create /sys/hardware_revisions\n",
				__func__);
		return -ENOMEM;
	}

	retval = sysfs_create_group(hw_rev_txt_kobj, &hw_rev_txt_attr_group);
	if (retval)
		pr_err("%s: failed for entries in /sys/hardware_revisions\n",
				__func__);

	return retval;
}

static ssize_t
sysfs_extended_baseband_show(struct sys_device *dev,
		struct sysdev_attribute *attr,
		char *buf)
{
	if (!strnlen(extended_baseband, BASEBAND_MAX_LEN)) {
		pr_err("%s: No extended_baseband available!\n", __func__);
		return 0;
	}
	return snprintf(buf, BASEBAND_MAX_LEN, "%s\n", extended_baseband);
}

static struct sysdev_attribute baseband_files[] = {
	_SYSDEV_ATTR(extended_baseband, 0444,
			sysfs_extended_baseband_show, NULL),
};

static struct sysdev_class baseband_sysdev_class = {
	.name = "baseband",
};

static struct sys_device baseband_sys_device = {
	.id = 0,
	.cls = &baseband_sysdev_class,
};

static void init_sysfs_extended_baseband(void){
	int err;

	if (!strnlen(extended_baseband, BASEBAND_MAX_LEN)) {
		pr_err("%s: No extended_baseband available!\n", __func__);
		return;
	}

	err = sysdev_class_register(&baseband_sysdev_class);
	if (err) {
		pr_err("%s: sysdev_class_register fail (%d)\n",
				__func__, err);
		return;
	}

	err = sysdev_register(&baseband_sys_device);
	if (err) {
		pr_err("%s: sysdev_register fail (%d)\n",
			__func__, err);
		return;
	}

	err = sysdev_create_file(&baseband_sys_device, &baseband_files[0]);
	if (err) {
		pr_err("%s: sysdev_create_file(%s)=%d\n",
				__func__, baseband_files[0].attr.name, err);
		return;
	}
}

static void __init mmi_device_init(struct msm8960_oem_init_ptrs *oem_ptr)
{
	platform_add_devices(mmi_devices, ARRAY_SIZE(mmi_devices));
	mmi_audio_dsp_init();

	mmi_vibrator_init();
	mmi_unit_info_init();
	init_sysfs_extended_baseband();

	/* Factory gsbi12 sysfs entry and tcmd gpio exports */
	sysfs_factory_gsbi12_mode_init();
	mot_tcmd_export_gpio();
	emmc_version_init();
	hw_rev_txt_init();

	if (mbmprotocol == 0) {
		/* do not reboot - version was not reported */
		/* not expecting bootloader to recognize reboot flag*/
		pr_err("ERROR: mbm protocol version missing\n");
	} else if (EXPECTED_MBM_PROTOCOL_VER != mbmprotocol) {
		pr_err("ERROR: mbm protocol version mismatch\n");
		msm_restart(0, "mbmprotocol_ver_mismatch");
	}
}

static void __init mmi_disp_init(struct msm8960_oem_init_ptrs *oem_ptr,
				struct msm_fb_platform_data *msm_fb_pdata,
				struct mipi_dsi_platform_data *mipi_dsi_pdata)
{
	mmi_display_init(msm_fb_pdata, mipi_dsi_pdata);
}

static int mmi_factory_kill_gpio;

static void __init mmi_get_factory_kill_gpio(void)
{
	struct device_node *chosen;
	int len = 0, enable = 1, rc;
	u32 gpio = 0;
	const void *prop;

	chosen = of_find_node_by_path("/Chosen@0");
	if (!chosen)
		goto out;

	prop = of_get_property(chosen, "factory_kill_disable", &len);
	if (prop && (len == sizeof(u8)) && *(u8 *)prop)
		enable = 0;

	rc = of_property_read_u32(chosen, "factory_kill_gpio", &gpio);
	if (rc) {
		pr_err("%s: factory_kill_gpio not present\n", __func__);
		goto putnode;
	}

	/*Write gpio number in mmi_factory_kill_gpio*/
	mmi_factory_kill_gpio = gpio;

	rc = gpio_request(gpio, "Factory Kill Disable");
	if (rc) {
		pr_err("%s: GPIO request failure\n", __func__);
		goto putnode;
	}

	rc = gpio_direction_output(gpio, enable);
	if (rc) {
		pr_err("%s: GPIO configuration failure\n", __func__);
		goto gpiofree;
	}

	rc = gpio_export(gpio, 0);

	if (rc) {
		pr_err("%s: GPIO export failure\n", __func__);
		goto gpiofree;
	}

	pr_info("%s: Factory Kill Circuit: %s\n", __func__,
		(enable ? "enabled" : "disabled"));

	return;

gpiofree:
	gpio_free(gpio);
putnode:
	of_node_put(chosen);
out:
	return;
}

bool check_factory_kill_gpio(void)
{
	int state;
	state = mmi_factory_kill_gpio ?
		!gpio_get_value(mmi_factory_kill_gpio) : 0;
	return cpu_is_msm8960() || state;
}

/* Motorola ULPI default register settings
 * TXPREEMPAMPTUNE[5:4] = 11 (3x preemphasis current)
 * TXVREFTUNE[3:0] = 1111 increasing the DC level
 */
static int mmi_phy_settings[] = {0x34, 0x82, 0x38, 0x81, -1};

static void __init mmi_otg_init(struct msm8960_oem_init_ptrs *oem_ptr,
			void *pdata)
{
	struct device_node *chosen;
	int len;
	const void *prop;
	int ret;
	unsigned int val;
	struct msm_otg_platform_data *otg_pdata =
				(struct msm_otg_platform_data *) pdata;

	chosen = of_find_node_by_path("/Chosen@0");

	/*
	 * the phy init sequence read from the device tree should be a
	 * sequence of value/register pairs
	 */
	prop = of_get_property(chosen, "ulpi_phy_init_seq", &len);
	if (prop && !(len % 2)) {
		int i;
		u8 *prop_val;

		otg_pdata->phy_init_seq = kzalloc(sizeof(int)*(len+1),
							GFP_KERNEL);

		if (!otg_pdata->phy_init_seq) {
			otg_pdata->phy_init_seq = mmi_phy_settings;
			goto put_node;
		}

		otg_pdata->phy_init_seq[len] = -1;
		prop_val = (u8 *)prop;

		for (i = 0; i < len; i += 2) {
			otg_pdata->phy_init_seq[i] = prop_val[i];
			otg_pdata->phy_init_seq[i+1] = prop_val[i+1];
		}
	} else
		otg_pdata->phy_init_seq = mmi_phy_settings;

	/*
	 * If the EMU circuitry provides id, then read the id irq and
	 * active logic from the device tree.
	 */
	ret = of_property_read_u32(chosen, "emu_id_mpp_gpio", &val);
	if (!ret) {
		otg_pdata->pmic_id_irq = gpio_to_irq(val);
		pr_debug("%s: PMIC id irq = %d\n", __func__,
				otg_pdata->pmic_id_irq);
	}

	ret = of_property_read_u32(chosen, "emu_id_activehigh", &val);
	if (!ret) {
		pr_debug("%s: PMIC id irq is active %s\n",
				__func__, val ? "high" : "low");
		otg_pdata->pmic_id_irq_active_high = val;
	}

	ret = of_property_read_u32(chosen, "emu_id_flt_gpio", &val);
	if (!ret) {
		otg_pdata->pmic_id_flt_gpio = val;
		pr_debug("%s: PMIC id irq = %d\n", __func__,
				otg_pdata->pmic_id_flt_gpio);
	}

	ret = of_property_read_u32(chosen, "emu_id_flt_activehigh", &val);
	if (!ret) {
		pr_debug("%s: PMIC id flt is active %s\n",
				__func__, val ? "high" : "low");
		otg_pdata->pmic_id_flt_gpio_active_high = val;
	}

#ifdef CONFIG_EMU_DETECTION
	mmi_init_emu_detection(otg_pdata);
#endif

put_node:
	of_node_put(chosen);
	mmi_get_factory_kill_gpio();
	otg_pdata->check_factory_kill = check_factory_kill_gpio;
	return;
}

static void __init mmi_mmc_init_legacy(struct msm8960_oem_init_ptrs *oem_ptr,
					int host, void *pdata, int *disable)
{
	struct mmc_platform_data *sdcc = (struct mmc_platform_data *)pdata;

	pr_info("%s: sdcc.%d\n", __func__, host);
	switch (host) {
	case 1:		/* SDCC1 */
		sdcc->pin_data->pad_data->drv->on[0].val = GPIO_CFG_6MA;
		sdcc->pin_data->pad_data->drv->on[1].val = GPIO_CFG_6MA;
		sdcc->pin_data->pad_data->drv->on[2].val = GPIO_CFG_6MA;
		/* Enable UHS rates up to DDR50 */
		sdcc->uhs_caps = (MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
				  MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_DDR50 |
				  MMC_CAP_1_8V_DDR);
		sdcc->uhs_caps2 = 0;
		sdcc->msmsdcc_max_pwrclass = 6;
		break;
	case 3:		/* SDCC3 */
		sdcc->pin_data->pad_data->drv->on[0].val = GPIO_CFG_8MA;
		sdcc->pin_data->pad_data->drv->on[1].val = GPIO_CFG_8MA;
		sdcc->pin_data->pad_data->drv->on[2].val = GPIO_CFG_8MA;
		sdcc->status_gpio = PM8921_GPIO_PM_TO_SYS(20);
		sdcc->status_irq = gpio_to_irq(sdcc->status_gpio);
		sdcc->is_status_gpio_active_low = true;
		/* Enable UHS rates up to DDR50 */
		sdcc->uhs_caps = (MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
				  MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_DDR50 |
				  MMC_CAP_MAX_CURRENT_600);
		sdcc->uhs_caps2 = 0;
		break;
	default:
		if (disable)
			*disable = 1;
		break;
	}
}

/*
 * MSM8974-style device tree -> MSM8960 platform data glue
 */
static void __init mmi_mmc_init(struct msm8960_oem_init_ptrs *oem_ptr,
				int host, void *pdata, int *disable)
{
	struct mmc_platform_data *sdcc = (struct mmc_platform_data *)pdata;
	struct device_node *n = NULL;
	int i = -1, len;

	do {
		n = of_find_compatible_node(n, NULL, "mmi,msm-sdcc");
		of_property_read_u32(n, "cell-index", &i);
	} while (n && i != host);

	/* Fall back on legacy settings if DT node not found */
	if (i != host)
		return mmi_mmc_init_legacy(oem_ptr, host, pdata, disable);

	if (!of_device_is_available(n)) {
		pr_info("%s: sdcc.%d: disabled\n", __func__, host);
		if (disable)
			*disable = 1;
		goto put_node;
	}

	/* Rebuild a new clock table, if necessary */
	if (of_get_property(n, "qcom,sdcc-clk-rates", &len)) {
		u32 *clks;

		clks = kzalloc(len, GFP_KERNEL);
		if (clks) {
			len /= sizeof(*clks);
			if (!of_property_read_u32_array(n,
					"qcom,sdcc-clk-rates", clks, len))
				sdcc->sup_clk_table = clks;
				sdcc->sup_clk_cnt = len;
		} else
			pr_err("%s: sdcc%d: sdcc-clk-rates: out of memory\n",
				__func__, host);
	}

	/* Update any drive strength settings for this PCB layout */
	if (of_get_property(n, "qcom,sdcc-pad-drv-on", &len)) {
		u32 drvs[3] = { GPIO_CFG_8MA, GPIO_CFG_8MA, GPIO_CFG_8MA };

		if (len > sizeof(drvs))
			len = sizeof(drvs);
		len /= sizeof(drvs[0]);
		of_property_read_u32_array(n, "qcom,sdcc-pad-drv-on",
				drvs, len);
		sdcc->pin_data->pad_data->drv->on[0].val = drvs[0];
		sdcc->pin_data->pad_data->drv->on[1].val = drvs[1];
		sdcc->pin_data->pad_data->drv->on[2].val = drvs[2];
	}

	/* Pry the card detect GPIO config from the clutches of DT */
	if (of_get_property(n, "cd-gpios", &len)) {
		int gpio;
		enum of_gpio_flags flags = OF_GPIO_ACTIVE_LOW;
		struct of_phandle_args args;

		gpio = of_get_named_gpio_flags(n, "cd-gpios", 0, &flags);
		/* pm8xxx's of_xlate() doesn't fill in flags >:( */
		if (of_parse_phandle_with_args(n, "cd-gpios", "#gpio-cells",
				0, &args) == 0)
			flags = args.args[1];
		if (gpio_is_valid(gpio)) {
			sdcc->status_gpio = gpio;
			sdcc->status_irq = gpio_to_irq(gpio);
			sdcc->is_status_gpio_active_low =
					(flags & OF_GPIO_ACTIVE_LOW);
		}
	}

	/* Update bus speed capabilities, if specified */
	len = of_property_count_strings(n, "qcom,sdcc-bus-speed-mode");
	if (len > 0)
		sdcc->uhs_caps = sdcc->uhs_caps2 = 0;	/* reset and reload */
	for (i = 0; i < len; i++) {
		const char *name = NULL;

		of_property_read_string_index(n,
			"qcom,sdcc-bus-speed-mode", i, &name);
		if (!name)
			continue;

		if (!strncmp(name, "SDR12", sizeof("SDR12")))
			sdcc->uhs_caps |= MMC_CAP_UHS_SDR12;
		else if (!strncmp(name, "SDR25", sizeof("SDR25")))
			sdcc->uhs_caps |= MMC_CAP_UHS_SDR25;
		else if (!strncmp(name, "SDR50", sizeof("SDR50")))
			sdcc->uhs_caps |= MMC_CAP_UHS_SDR50;
		else if (!strncmp(name, "DDR50", sizeof("DDR50")))
			sdcc->uhs_caps |= MMC_CAP_UHS_DDR50;
		else if (!strncmp(name, "SDR104", sizeof("SDR104")))
			sdcc->uhs_caps |= MMC_CAP_UHS_SDR104;
		else if (!strncmp(name, "HS200_1p8v", sizeof("HS200_1p8v")))
			sdcc->uhs_caps2 |= MMC_CAP2_HS200_1_8V_SDR;
		else if (!strncmp(name, "HS200_1p2v", sizeof("HS200_1p2v")))
			sdcc->uhs_caps2 |= MMC_CAP2_HS200_1_2V_SDR;
		else if (!strncmp(name, "DDR_1p8v", sizeof("DDR_1p8v")))
			sdcc->uhs_caps |= MMC_CAP_1_8V_DDR
						| MMC_CAP_UHS_DDR50;
		else if (!strncmp(name, "DDR_1p2v", sizeof("DDR_1p2v")))
			sdcc->uhs_caps |= MMC_CAP_1_2V_DDR
						| MMC_CAP_UHS_DDR50;
	}

	/* Maximum power class for eMMC cards */
	if (of_property_read_u32(n, "qcom,sdcc-max-power-class", &i) == 0)
		sdcc->msmsdcc_max_pwrclass = i;

	/* Current limit for SD cards */
	of_property_read_u32(n, "qcom,sdcc-current-limit", &i);
	if (i == 800)
		sdcc->uhs_caps |= MMC_CAP_MAX_CURRENT_800;
	else if (i == 600)
		sdcc->uhs_caps |= MMC_CAP_MAX_CURRENT_600;
	else if (i == 400)
		sdcc->uhs_caps |= MMC_CAP_MAX_CURRENT_400;
	else if (i == 200)
		sdcc->uhs_caps |= MMC_CAP_MAX_CURRENT_200;

	pr_info("%s: sdcc.%d: maxclk=%dMHz, class=%d, caps=0x%08X:%08X, "
			"drv=%d:%d:%d\n",
			__func__, host,
			sdcc->sup_clk_table[sdcc->sup_clk_cnt - 1] / 1000000,
			sdcc->msmsdcc_max_pwrclass,
			sdcc->uhs_caps, sdcc->uhs_caps2,
			sdcc->pin_data->pad_data->drv->on[0].val,
			sdcc->pin_data->pad_data->drv->on[1].val,
			sdcc->pin_data->pad_data->drv->on[2].val);

put_node:
	of_node_put(n);
}

static struct mmi_oem_data mmi_data;

static void __init mmi_msm8960_init_early(void)
{
	msm8960_allocate_memory_regions();
	if (fdt_start_address) {
		void *mem;
		mem = __alloc_bootmem(fdt_size, __alignof__(int), 0);
		BUG_ON(!mem);
		memcpy(mem, (const void *)fdt_start_address, fdt_size);
		initial_boot_params = (struct boot_param_header *)mem;
		pr_info("Unflattening device tree: 0x%08x\n", (u32)mem);

		unflatten_device_tree();
	}

	/* Initialize OEM initialization overrides */
	msm8960_oem_funcs.msm_gpio_init = mmi_gpiomux_init;
	msm8960_oem_funcs.msm_gsbi_init = mmi_gsbi_init;
	msm8960_oem_funcs.msm_gpio_mpp_init = mmi_gpio_mpp_init;
	msm8960_oem_funcs.msm_i2c_init = mmi_i2c_init;
	msm8960_oem_funcs.msm_pmic_init = mmi_pmic_init;
	msm8960_oem_funcs.msm_clock_init = mmi_clk_init;
	msm8960_oem_funcs.msm_device_init = mmi_device_init;
	msm8960_oem_funcs.msm_display_init = mmi_disp_init;
	msm8960_oem_funcs.msm_regulator_init = mmi_regulator_init;
	msm8960_oem_funcs.msm_otg_init = mmi_otg_init;
	msm8960_oem_funcs.msm_mmc_init = mmi_mmc_init;
	msm8960_oem_funcs.msm_hdmi_init = is_mmi_hdmi_dt_available;

	/* Custom OEM Platform Data */
	mmi_data.is_factory = mmi_boot_mode_is_factory;
	mmi_data.is_meter_locked = mmi_battery_data_is_meter_locked;
	mmi_data.is_no_eprom = mmi_battery_data_is_no_eprom;
	mmi_data.mmi_camera = true;
	mmi_data.is_bareboard = mmi_unit_is_bareboard;
	mmi_data.get_radio = mmi_unit_get_radio;
	msm8960_oem_funcs.oem_data = &mmi_data;
}

static int __init parse_tag_flat_dev_tree_address(const struct tag *tag)
{
	struct tag_flat_dev_tree_address *fdt_addr =
		(struct tag_flat_dev_tree_address *)&tag->u.fdt_addr;

	if (fdt_addr->size) {
		fdt_start_address = (u32)phys_to_virt(fdt_addr->address);
		fdt_size = fdt_addr->size;
	}

	pr_info("flat_dev_tree_address=0x%08x, flat_dev_tree_size == 0x%08X\n",
		fdt_addr->address, fdt_addr->size);

	return 0;
}
__tagtable(ATAG_FLAT_DEV_TREE_ADDRESS, parse_tag_flat_dev_tree_address);

static const char *mmi_dt_match[] __initdata = {
	"mmi,msm8960",
	NULL
};

static const char *mmi_dt_match_8960[] __initdata = {
	"mmi,msm8960-old",
	NULL
};

static const char *mmi_dt_match_dinara[] __initdata = {
	"mmi,msm8960-qinara",
	NULL
};

static const struct of_device_id mmi_msm8960_dt_gic_match[] __initconst = {
	{ .compatible = "qcom,msm-qgic2", .data = gic_of_init },
	{ .compatible = "qcom,msm-gpio", .data = msm_gpio_of_init_legacy, },
	{ }
};

static void __init mmi_msm8960_init_irq(void)
{
	struct device_node *node;

	node = of_find_matching_node(NULL, mmi_msm8960_dt_gic_match);
	if (node) {
		msm_mpm_irq_extn_init(&msm8960_mpm_dev_data);
		of_irq_init(mmi_msm8960_dt_gic_match);
		of_node_put(node);
	} else
		msm8960_init_irq();
}

static struct of_device_id mmi_of_setup[] __initdata = {
	{ .compatible = "linux,seriallow", .data = &system_serial_low },
	{ .compatible = "linux,serialhigh", .data = &system_serial_high },
	{ .compatible = "linux,hwrev", .data = &system_rev },
	{ .compatible = "mmi,prod_id", .data = &prod_id },
	{ .compatible = "mmi,mbmprotocol", .data = &mbmprotocol },
	{ }
};

static void __init mmi_of_populate_setup(void)
{
	struct device_node *n = of_find_node_by_path("/chosen");
	struct of_device_id *tbl = mmi_of_setup;
	const char *baseband;

	while (tbl->data) {
		of_property_read_u32(n, tbl->compatible, tbl->data);
		tbl++;
	}

	if (0 == of_property_read_string(n, "mmi,baseband", &baseband))
		strlcpy(extended_baseband, baseband, sizeof(extended_baseband));

	of_node_put(n);
}

static struct of_dev_auxdata mmi_auxdata[] __initdata = {
	OF_DEV_AUXDATA("qcom,i2c-qup", 0x16200000, "qup_i2c.3", NULL),
	OF_DEV_AUXDATA("qcom,i2c-qup", 0x16300000, "qup_i2c.4", NULL),
	OF_DEV_AUXDATA("qcom,i2c-qup", 0x1a000000, "qup_i2c.8", NULL),
	OF_DEV_AUXDATA("qcom,i2c-qup", 0x1a200000, "qup_i2c.10", NULL),
	OF_DEV_AUXDATA("qcom,i2c-qup", 0x12480000, "qup_i2c.12", NULL),
	OF_DEV_AUXDATA("qcom,msm-hsuart", 0x16440000, "msm_serial_hs.3", NULL),
	{}
};

static void __init mmi_msm8960_dt_init(void)
{
	struct of_dev_auxdata *adata = mmi_auxdata;

	mmi_of_populate_setup();
	/* Register extra devices which are not part of GSBISetup@0 */
	if (cpu_is_msm8960ab())
		of_platform_populate(NULL,
			of_default_bus_match_table, adata, NULL);
	msm8960_cdp_init();
}

MACHINE_START(DINARA, "Qinara")
	.map_io = msm8960_map_io,
	.reserve = mmi_msm8960_reserve,
	.init_irq = mmi_msm8960_init_irq,
	.handle_irq = gic_handle_irq,
	.timer = &msm_timer,
	.init_machine = mmi_msm8960_dt_init,
	.init_early = mmi_msm8960_init_early,
	.init_very_early = msm8960_early_memory,
	.restart = msm_restart,
	.dt_compat = mmi_dt_match_dinara,
MACHINE_END

MACHINE_START(VANQUISH, "Vanquish")
	.map_io = msm8960_map_io,
	.reserve = mmi_msm8960_reserve,
	.init_irq = mmi_msm8960_init_irq,
	.handle_irq = gic_handle_irq,
	.timer = &msm_timer,
	.init_machine = mmi_msm8960_dt_init,
	.init_early = mmi_msm8960_init_early,
	.init_very_early = msm8960_early_memory,
	.restart = msm_restart,
	.dt_compat = mmi_dt_match_8960,
MACHINE_END

MACHINE_START(MSM8960DT, "msm8960dt")
	.map_io = msm8960_map_io,
	.reserve = mmi_msm8960_reserve,
	.init_irq = mmi_msm8960_init_irq,
	.handle_irq = gic_handle_irq,
	.timer = &msm_timer,
	.init_machine = mmi_msm8960_dt_init,
	.init_early = mmi_msm8960_init_early,
	.init_very_early = msm8960_early_memory,
	.restart = msm_restart,
	.dt_compat = mmi_dt_match,
MACHINE_END
