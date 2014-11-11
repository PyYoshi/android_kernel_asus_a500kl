#include "mdss_asus_debug.h"
#include <linux/debugfs.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>

extern u32 mdss_dsi_panel_cmd_read(struct mdss_dsi_ctrl_pdata *ctrl, char cmd0,
		char cmd1, void (*fxn)(int), char *rbuf, int len);

static struct mdss_dsi_ctrl_pdata *ctrl_asus_global;

#define LCM_TYPE_ID 28

int notify_panel_on_cmds_start(struct mdss_dsi_ctrl_pdata *ctrl)

{
       ctrl_asus_global = ctrl;
       printk("[mDSS][Hyde]keeep dsi control\n");
       return 0;
}

ssize_t lcd_unique_id_read_proc(char *page, char **start, off_t off, int count, 
            	int *eof, void *data)
{
	char *hsd_rbuffer_01;
	char *hsd_rbuffer_02;
	char *hsd_rbuffer_03;
	char *hsd_rbuffer_04;
	int panel_value;
	hsd_rbuffer_01 = kmalloc(sizeof(ctrl_asus_global->rx_buf.len), GFP_KERNEL);
	hsd_rbuffer_02 = kmalloc(sizeof(ctrl_asus_global->rx_buf.len), GFP_KERNEL);
	hsd_rbuffer_03 = kmalloc(sizeof(ctrl_asus_global->rx_buf.len), GFP_KERNEL);
	hsd_rbuffer_04 = kmalloc(sizeof(ctrl_asus_global->rx_buf.len), GFP_KERNEL);
	if (IS_ERR_OR_NULL(ctrl_asus_global)) {
		printk("[MDSSS][Hyde] keep  ctrl_asus_global: fail !!\n");
		return -EINVAL;
	}
	mdss_dsi_panel_cmd_read(ctrl_asus_global,0xDA,0x00,NULL,hsd_rbuffer_01,0);
	msleep(2);
	mdss_dsi_panel_cmd_read(ctrl_asus_global,0xDB,0x00,NULL,hsd_rbuffer_02,0);
	msleep(2);
	mdss_dsi_panel_cmd_read(ctrl_asus_global,0xDC,0x0,NULL,hsd_rbuffer_03,0);
	msleep(2);
	mdss_dsi_panel_cmd_read(ctrl_asus_global,0x0A,0x00,NULL,hsd_rbuffer_04,0);
	printk("[Display][Hyde] reg: addr 0x0A = (0x%x)\n" ,*(hsd_rbuffer_04));
	printk("[Display][Hyde] reg: addr 0xDA = (0x%x)\n" ,*(hsd_rbuffer_01));
	printk("[Display][Hyde] reg: addr 0xDB = (0x%x)\n" ,*(hsd_rbuffer_02));
	printk("[Display][Hyde] reg: addr 0xDC = (0x%x)\n" ,*(hsd_rbuffer_03));
	panel_value = sprintf(page, "%02x%02x%02x\n", *(hsd_rbuffer_01),*(hsd_rbuffer_02),*(hsd_rbuffer_03));
	kfree(hsd_rbuffer_01);
	kfree(hsd_rbuffer_02);
	kfree(hsd_rbuffer_03);
	kfree(hsd_rbuffer_04);
return panel_value;
	
}

void create_lcd_unique_id_proc_file(void)
{
	struct proc_dir_entry *lcd_unique_id_proc_file = create_proc_entry("lcd_unique_id", 0644, NULL);

	if (lcd_unique_id_proc_file) 
	{
		lcd_unique_id_proc_file->read_proc = lcd_unique_id_read_proc;
	}
	else 
	{
		printk("[MDSS][Hyde]proc lcd_unique_id_proc_file create failed!\n");
	}

	return;
}

ssize_t lcd_type_read_proc(char *page, char **start, off_t off, int count,
		int *eof, void *data)
{
	volatile int panel_type = -1;
	panel_type = gpio_get_value(LCM_TYPE_ID);

	return sprintf(page, "%d\n", panel_type);
}

void create_lcd_type_proc_file(void)
{
	struct proc_dir_entry *lcd_type_proc_file = create_proc_entry("lcd_type", 0644, NULL);

	if (lcd_type_proc_file)
	{
		lcd_type_proc_file->read_proc = lcd_type_read_proc;
	}
	else
	{
		printk("[MDSS][Hyde]proc lcd_type_proc_file create failed!\n");
	}

	return;
}


