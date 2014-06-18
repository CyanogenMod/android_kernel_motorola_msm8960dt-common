#include <asm/setup.h>
#include <libfdt.h>

static int node_offset(void *fdt, const char *node_path)
{
	int offset = fdt_path_offset(fdt, node_path);
	if (offset == -FDT_ERR_NOTFOUND)
		offset = fdt_add_subnode(fdt, 0, node_path);
	return offset;
}

static int setprop(void *fdt, const char *node_path, const char *property,
		   uint32_t *val_array, int size)
{
	int offset = node_offset(fdt, node_path);
	if (offset < 0)
		return offset;
	return fdt_setprop(fdt, offset, property, val_array, size);
}

static int setprop_string(void *fdt, const char *node_path,
			  const char *property, const char *string)
{
	int offset = node_offset(fdt, node_path);
	if (offset < 0)
		return offset;
	return fdt_setprop_string(fdt, offset, property, string);
}

static int setprop_cell(void *fdt, const char *node_path,
			const char *property, uint32_t val)
{
	int offset = node_offset(fdt, node_path);
	if (offset < 0)
		return offset;
	return fdt_setprop_cell(fdt, offset, property, val);
}

/*
 * Convert and fold provided ATAGs into the provided FDT.
 *
 * REturn values:
 *    = 0 -> pretend success
 *    = 1 -> bad ATAG (may retry with another possible ATAG pointer)
 *    < 0 -> error from libfdt
 */
int atags_to_fdt(void *atag_list, void *fdt, int total_space)
{
	struct tag *atag = atag_list;
	uint32_t mem_reg_property[2 * NR_BANKS];
	int memcount = 0;
	int ret;

	/* make sure we've got an aligned pointer */
	if ((u32)atag_list & 0x3)
		return 1;

	/* if we get a DTB here we're done already */
	if (*(u32 *)atag_list == fdt32_to_cpu(FDT_MAGIC))
	       return 0;

	/* validate the ATAG */
	if (atag->hdr.tag != ATAG_CORE ||
	    (atag->hdr.size != tag_size(tag_core) &&
	     atag->hdr.size != 2))
		return 1;

	/* let's give it all the room it could need */
	ret = fdt_open_into(fdt, fdt, total_space);
	if (ret < 0)
		return ret;

	for_each_tag(atag, atag_list) {
		if (atag->hdr.tag == ATAG_CMDLINE) {
			setprop_string(fdt, "/chosen", "bootargs",
					atag->u.cmdline.cmdline);
#ifdef CONFIG_MMI_JB_FIRMWARE
		} else if (atag->hdr.tag == ATAG_REVISION) {
			setprop_cell(fdt, "/chosen", "linux,hwrev",
					atag->u.revision.rev);
		} else if (atag->hdr.tag == ATAG_SERIAL) {
			setprop_cell(fdt, "/chosen", "linux,seriallow",
					atag->u.serialnr.low);
			setprop_cell(fdt, "/chosen", "linux,serialhigh",
					atag->u.serialnr.high);
		} else if (atag->hdr.tag == ATAG_DISPLAY) {
			setprop_string(fdt, "/chosen", "mmi,panel_name",
					atag->u.display.display);
#ifdef CONFIG_BOOTINFO
		} else if (atag->hdr.tag == ATAG_MBM_VERSION) {
			setprop_cell(fdt, "/chosen", "mmi,mbmversion",
					atag->u.mbm_version.mbm_version);
		} else if (atag->hdr.tag == ATAG_POWERUP_REASON) {
			setprop_cell(fdt, "/chosen", "mmi,powerup_reason",
					atag->u.powerup_reason.powerup_reason);
#endif //CONFIG_BOOTINFO
		} else if (atag->hdr.tag == ATAG_BASEBAND) {
			setprop_string(fdt, "/chosen", "mmi,baseband",
					atag->u.baseband.baseband);
#endif //CONFIG_MMI_JB_FIRMWARE
		} else if (atag->hdr.tag == ATAG_MEM) {
			if (memcount >= sizeof(mem_reg_property)/4)
				continue;
			if (!atag->u.mem.size)
				continue;
			mem_reg_property[memcount++] = cpu_to_fdt32(atag->u.mem.start);
			mem_reg_property[memcount++] = cpu_to_fdt32(atag->u.mem.size);
		} else if (atag->hdr.tag == ATAG_INITRD2) {
			uint32_t initrd_start, initrd_size;
			initrd_start = atag->u.initrd.start;
			initrd_size = atag->u.initrd.size;
			setprop_cell(fdt, "/chosen", "linux,initrd-start",
					initrd_start);
			setprop_cell(fdt, "/chosen", "linux,initrd-end",
					initrd_start + initrd_size);
		}
	}
#ifdef CONFIG_MMI_JB_FIRMWARE
	setprop_cell(fdt, "/chosen", "mmi,mbmprotocol", 0x02);
#endif

	if (memcount)
		setprop(fdt, "/memory", "reg", mem_reg_property, 4*memcount);

	return fdt_pack(fdt);
}
