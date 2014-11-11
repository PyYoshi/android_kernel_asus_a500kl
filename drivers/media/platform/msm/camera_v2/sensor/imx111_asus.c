/* Copyright (c) 2012-2013, The Linux Foundation. All rights reserved.
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
#include "msm_sensor.h"
#define IMX111_ASUS_SENSOR_NAME "imx111_asus"
DEFINE_MSM_MUTEX(imx111_asus_mut);


#define DBG_TXT_BUF_SIZE 256
static char debugTxtBuf[DBG_TXT_BUF_SIZE];
static u32 i2c_get_value;
#define SENSOR_MAX_RETRIES      50 /* max counter for retry I2C access */
#undef CDBG
#ifdef CONFIG_MSMB_CAMERA_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
//#define CDBG(fmt, args...) do { } while (0)
#else
#define CDBG(fmt, args...) do { } while (0)
#endif

bool g_camera_power = 0;
static int imx111_asus_status=0;
static struct msm_sensor_ctrl_t imx111_asus_s_ctrl;

static u8 imx111_asus_otp_value[24]={0};
#define imx111_opt_start 0x3500
#define opt_size 24
extern int a500kl_camera_debug;

int sensor_read_reg(struct i2c_client *client, u16 addr, u16 *val)
{
	int err;

      
	err=imx111_asus_s_ctrl.sensor_i2c_client->i2c_func_tbl->i2c_read(imx111_asus_s_ctrl.sensor_i2c_client,addr,val,MSM_CAMERA_I2C_BYTE_DATA);
	CDBG("sensor_read_reg 0x%x\n",*val);
	if(err <0)
		return -EINVAL;	
	else return 0;
	
}

int sensor_write_reg(struct i2c_client *client, u16 addr, u16 val)
{
	int err;
	int retry = 0;
	do {
		err =imx111_asus_s_ctrl.sensor_i2c_client->i2c_func_tbl->i2c_write(imx111_asus_s_ctrl.sensor_i2c_client,addr,val,MSM_CAMERA_I2C_BYTE_DATA);		

		if (err == 0)
			return 0;
		retry++;
		pr_err("yuv_sensor : i2c transfer failed, retrying %x %x\n",
		       addr, val);
		msleep(1); //LiJen: increate i2c retry delay to avoid ISP i2c fail issue
	} while (retry <= SENSOR_MAX_RETRIES);

	if(err == 0) {
		printk("%s(%d): i2c_transfer error, but return 0!?\n", __FUNCTION__, __LINE__);
		err = 0xAAAA;
	}


	return err;
}

#define	IMX111_ASUS_PROC_FILE_OTP	"driver/imx111_asus_otp"

#include <linux/proc_fs.h>


static ssize_t imx111_asus_proc_read_otp(char *page, char **start, off_t off, int count, 
            	int *eof, void *data){
	int len=0;
	int i=0;



	if(*eof == 0){
		for(i=0;i<8;i++){
			len+=sprintf(page+len, "0x%02x ", imx111_asus_otp_value[i]);
		*eof = 1;
		}
		//pr_info("%s\n",(char *)page);
	}
  	if ( len <= off+count )

        *eof = 1;

    	*start  = page + off;

   	 len -= off;

    if ( len > count )

        len = count;

    if ( len < 0 )

        len = 0;
	
	  return len;
}



int init_read_otp(void){
	int i=0;
	u16 opt_start_addr=imx111_opt_start;
	int block=2;
	int bank=0;

	sensor_write_reg(imx111_asus_s_ctrl.sensor_i2c_client->client,0x34c9,block*3+1);
	sensor_read_reg(imx111_asus_s_ctrl.sensor_i2c_client->client, opt_start_addr+(block*24+8), (u16 *)&i2c_get_value); 
	pr_err("init_read_otp  +++ i2c_get_value =%x\n",i2c_get_value);
	if( i2c_get_value!=3){
		block=1;	
		sensor_write_reg(imx111_asus_s_ctrl.sensor_i2c_client->client,0x34c9,block*3+1);
		sensor_read_reg(imx111_asus_s_ctrl.sensor_i2c_client->client, opt_start_addr+(block*24+8), (u16 *)&i2c_get_value);
		if(i2c_get_value!=3){
			block=0;
			sensor_write_reg(imx111_asus_s_ctrl.sensor_i2c_client->client,0x34c9,block*3+1);
			sensor_read_reg(imx111_asus_s_ctrl.sensor_i2c_client->client, opt_start_addr+(block*24+8), (u16 *)&i2c_get_value);
			if(i2c_get_value!=3) return -1;
		}	
	}
	for(bank=0;bank<3;bank++){
		sensor_write_reg(imx111_asus_s_ctrl.sensor_i2c_client->client,0x34c9,block*3+bank);		
		for(i=0;i<8;i++){
			sensor_read_reg(imx111_asus_s_ctrl.sensor_i2c_client->client, opt_start_addr+(block*24+bank*8+i), (u16 *)&i2c_get_value);	  	 
			pr_err("init_read_otp  bank=%x addr=%x value=%x\n",block*3+bank,opt_start_addr+(block*24+bank*8+i),i2c_get_value);
			imx111_asus_otp_value[bank*8+i]=i2c_get_value;
		}
	}
#ifdef CONFIG_MSMB_CAMERA_DEBUG	
	for(i=0;i<opt_size;i++)
		printk("imx111_asus_otp_value i=%d =%x\n",i,imx111_asus_otp_value[i]);
#endif


	return 0;
}	

void create_imx111_asus_proc_file(void){
	int rc=0;


	if(create_proc_read_entry(IMX111_ASUS_PROC_FILE_OTP, 0666, NULL, 
			imx111_asus_proc_read_otp, NULL) == NULL){
		pr_err("[Camera]proc file create failed!\n");
	}
	rc=imx111_asus_s_ctrl.func_tbl->sensor_power_up(&imx111_asus_s_ctrl);
	if(rc){
		rc=imx111_asus_s_ctrl.func_tbl->sensor_power_up(&imx111_asus_s_ctrl);
		if(rc) {
			imx111_asus_s_ctrl.func_tbl->sensor_power_down(&imx111_asus_s_ctrl);
		return;
		}
	}	
	init_read_otp();
	imx111_asus_s_ctrl.func_tbl->sensor_power_down(&imx111_asus_s_ctrl);
}	


static ssize_t otp_read_read(struct file *file, char __user *buf, size_t count,loff_t *ppos){


	int i=0;
	int len=0;
	char *bp = debugTxtBuf;
       if (*ppos)
		return 0;	/* the end */
	 for(i=0;i<opt_size;i=i+2) {
		if(i%8==0 && i!=0){
			printk("\n");
			len+=sprintf(bp+len, "\n");
		}	
		//printk("0x%02x   0x%02x   ",imx111_asus_otp_value[i],imx111_asus_otp_value[i+1]);
		len+=sprintf(bp+len, "0x%02x   0x%02x   ", imx111_asus_otp_value[i],imx111_asus_otp_value[i+1]);

		
	 }
	if (copy_to_user(buf, debugTxtBuf, len))
		return -EFAULT;
	*ppos += len;	
	 printk("\n");
	return len;

}	

static ssize_t i2c_get_write(struct file *file, const char __user *buf, size_t count,
				loff_t *ppos)
{
  int len;
  int arg = 0;


	if (*ppos)
		return 0;	/* the end */

//+ parsing......
	 len=(count > DBG_TXT_BUF_SIZE-1)?(DBG_TXT_BUF_SIZE-1):(count);
	 if (copy_from_user(debugTxtBuf,buf,len))
		return -EFAULT;

	 debugTxtBuf[len]=0; //add string end

	sscanf(debugTxtBuf, "%x", &arg);
	*ppos=len;

	sensor_read_reg(imx111_asus_s_ctrl.sensor_i2c_client->client, arg, (u16 *)&i2c_get_value);
	CDBG("the value is 0x%x\n", i2c_get_value);

	return len;	/* the end */
}

static ssize_t i2c_get_read(struct file *file, char __user *buf, size_t count,
				loff_t *ppos)
{
	int len = 0;
	char *bp = debugTxtBuf;

       if (*ppos)
		return 0;	/* the end */
	len = snprintf(bp, DBG_TXT_BUF_SIZE, "the value is 0x%x\n", i2c_get_value);

	if (copy_to_user(buf, debugTxtBuf, len))
		return -EFAULT;
       *ppos += len;
	return len;

}

static ssize_t i2c_set_write(struct file *file, const char __user *buf, size_t count,
				loff_t *ppos)
{
 	int len;
	int arg[2];
	arg[0]=0;

	if (*ppos)
		return 0;	/* the end */

	len=(count > DBG_TXT_BUF_SIZE-1)?(DBG_TXT_BUF_SIZE-1):(count);
	if (copy_from_user(debugTxtBuf,buf,len))
		return -EFAULT;

	debugTxtBuf[len]=0; //add string end

	sscanf(debugTxtBuf, "%x %x", &arg[0], &arg[1]);



	*ppos=len;
  

	sensor_write_reg(imx111_asus_s_ctrl.sensor_i2c_client->client, arg[0], arg[1]);
	return len;	/* the end */
}
static ssize_t imx111_asus_chip_power_write(struct file *file, const char __user *buf, size_t count,
				loff_t *ppos)
{
    int len;
    int arg;
    arg=0;

    if (*ppos)
        return 0;	/* the end */

    len=(count > DBG_TXT_BUF_SIZE-1)?(DBG_TXT_BUF_SIZE-1):(count);
    if (copy_from_user(debugTxtBuf,buf,len))
    		return -EFAULT;

    debugTxtBuf[len]=0; //add string end

    sscanf(debugTxtBuf, "%x", &arg);
    printk("argument is arg=0x%x\n",arg);


    *ppos=len;
    if (arg==1)  //power on
    {
		pr_info("ISP power_on\n");
 		imx111_asus_s_ctrl.func_tbl->sensor_power_up(&imx111_asus_s_ctrl);
    }else //power down 
    {
        	pr_info("ISP power_down\n");
		imx111_asus_s_ctrl.func_tbl->sensor_power_down(&imx111_asus_s_ctrl);
    }
    	return len;	/* the end */
}
static ssize_t status_read(struct file *file, char __user *buf, size_t count,
				loff_t *ppos)
{
	int len = 0;
	char *bp = debugTxtBuf;
	
       if (*ppos)
		return 0;	/* the end */
	   
	len = snprintf(bp, DBG_TXT_BUF_SIZE, "%d\n", imx111_asus_status);

	if (copy_to_user(buf, debugTxtBuf, len))
		return -EFAULT;
       *ppos += len;
	return len;

}

static ssize_t cci_stress_test_write(struct file *file, const char __user *buf, size_t count,
				loff_t *ppos)
{
    int len;
    int arg;
    int i,failtime;
    u16 data;
    arg=0;
    failtime = 0;
	
    if (*ppos)
        return 0;	/* the end */

    len=(count > DBG_TXT_BUF_SIZE-1)?(DBG_TXT_BUF_SIZE-1):(count);
    if (copy_from_user(debugTxtBuf,buf,len))
    		return -EFAULT;

    debugTxtBuf[len]=0; //add string end

    sscanf(debugTxtBuf, "%d", &arg);
    printk("argument is arg=%d\n",arg);

	if (arg == 0)
	{
		a500kl_camera_debug = 1;
		return len;
	} else if (arg == 0xff)
	{
		a500kl_camera_debug = 0;
		return len;
	}

    *ppos=len;
    
     imx111_asus_s_ctrl.func_tbl->sensor_power_up(&imx111_asus_s_ctrl);
    for(i=0; i<arg;i++){
		data = 0;
    		imx111_asus_s_ctrl.sensor_i2c_client->i2c_func_tbl->i2c_read(
			imx111_asus_s_ctrl.sensor_i2c_client,
			imx111_asus_s_ctrl.sensordata->slave_info->sensor_id_reg_addr,
			&data, MSM_CAMERA_I2C_WORD_DATA);
		pr_err("addr is 0x%x, data is 0x%x\n",
			imx111_asus_s_ctrl.sensordata->slave_info->sensor_id_reg_addr,
			data);
		if(data!=0x111){
			failtime++;
			imx111_asus_s_ctrl.func_tbl->sensor_power_down(&imx111_asus_s_ctrl);
			msleep(8000);
			imx111_asus_s_ctrl.func_tbl->sensor_power_up(&imx111_asus_s_ctrl);
		}else{
			msleep(100);
		}
    }
		imx111_asus_s_ctrl.func_tbl->sensor_power_down(&imx111_asus_s_ctrl);
		pr_err("use cci %d fail %d\n",arg,failtime);
    return len;	/* the end */
}

static const struct file_operations cci_stress_test_fops = {
	.write = cci_stress_test_write,
};

static const struct file_operations i2c_set_fops = {
//	.open		= i2c_set_open,
	//.read		= i2c_config_read,
	//.llseek		= seq_lseek,
	//.release	= single_release,
	.write = i2c_set_write,
};

static const struct file_operations i2c_get_fops = {
//	.open		= i2c_get_open,
	.read		= i2c_get_read,
	//.llseek		= seq_lseek,
	//.release	= single_release,
	.write = i2c_get_write,
};
static const struct file_operations imx111_asus_power_fops = {
//	.open		= dbg_iCatch7002a_chip_power_open,
	//.read		= i2c_get_read,
	//.llseek		= seq_lseek,
	//.release	= single_release,
	.write = imx111_asus_chip_power_write,
};

static const struct file_operations status_fops = {
//	.open		= dbg_iCatch7002a_chip_power_open,
	.read		= status_read,
	//.llseek		= seq_lseek,
	//.release	= single_release,
	//.write = dbg_iCatch7002a_chip_power_write,
};

static const struct file_operations otp_read_fops = {
	.read = otp_read_read,
};


static struct msm_sensor_power_setting imx111_asus_power_setting[] = {

	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_CAM_PWDN,
		.config_val = GPIO_OUT_LOW,
		.delay = 1,
	},	
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_CAM_1P8,
		.config_val = GPIO_OUT_LOW,
		.delay = 1,
	},	
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_CAM_2P8,
		.config_val = GPIO_OUT_LOW,
		.delay = 1,
	},	
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VANA,
		.config_val = 0,
		.delay = 5,
	},	
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_CAM_2P8,
		.config_val = GPIO_OUT_HIGH,
		.delay = 5,
	},//chip 2.8v high		
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VDIG,
		.config_val = 0,
		.delay = 5,
	},//vreg L5 1.2v high
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VIO,
		.config_val = 0,
		.delay = 5,
	},	
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_CAM_1P8,
		.config_val = GPIO_OUT_HIGH,
		.delay = 70,
	},		
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_CAM_PWDN,
		.config_val = GPIO_OUT_HIGH,
		.delay = 5,
	},			
	{
		.seq_type = SENSOR_CLK,
		.seq_val = SENSOR_CAM_MCLK,
		.config_val = 0,
		.delay = 5,
	},
	{
		.seq_type = SENSOR_VREG, //VCM power 
		.seq_val = CAM_VAF,
		.config_val = 0,
		.delay = 5,
	},		
	{
		.seq_type = SENSOR_I2C_MUX,
		.seq_val = 0,
		.config_val = 0,
		.delay = 0,
	},	
};

static struct v4l2_subdev_info imx111_asus_subdev_info[] = {
	{
		.code   = V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.fmt    = 1,
		.order    = 0,
	},
};
#if 0
static const struct i2c_device_id imx111_asus_i2c_id[] = {
	{IMX111_ASUS_SENSOR_NAME, (kernel_ulong_t)&imx111_asus_s_ctrl},
	{ }
};

static struct i2c_driver imx111_asus_i2c_driver = {
	.id_table =imx111_asus_i2c_id,
	.probe  = msm_sensor_i2c_probe,
	.driver = {
		.name = IMX111_ASUS_SENSOR_NAME,
	},
};
#endif
static struct msm_camera_i2c_client imx111_asus_sensor_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_WORD_ADDR,
};

static const struct of_device_id imx111_asus_dt_match[] = {
	{.compatible = "qcom,imx111_asus", .data = &imx111_asus_s_ctrl},
	{}
};

MODULE_DEVICE_TABLE(of, imx111_asus_dt_match);

static struct platform_driver imx111_asus_platform_driver = {
	.driver = {
		.name = "qcom,imx111_asus",
		.owner = THIS_MODULE,
		.of_match_table = imx111_asus_dt_match,
	},
};

static int32_t imx111_asus_platform_probe(struct platform_device *pdev)
{
	int32_t rc = 0;
	const struct of_device_id *match;
	int i;
	for(i=0; i<ARRAY_SIZE(imx111_asus_power_setting); i++){
		if((imx111_asus_power_setting[i].seq_val == SENSOR_GPIO_CAM_1P8 )&&
			(imx111_asus_power_setting[i].config_val == GPIO_OUT_HIGH)){
				if(g_ASUS_hwID < A500KL_SR2){
					//After SR2, EE change Capacitance
					imx111_asus_power_setting[i].delay = 70;
				}else{
					imx111_asus_power_setting[i].delay = 10;
				}
			pr_err("%dth is 1P8 PULL HIGH and delay %dms\n",
				i,imx111_asus_power_setting[i].delay);
			break;
		}
	}
	match = of_match_device(imx111_asus_dt_match, &pdev->dev);
	rc = msm_sensor_platform_probe(pdev, match->data);
	return rc;
}

static int __init imx111_asus_init_module(void)
{
	int32_t rc = 0;
	struct dentry *dent ;
	pr_info("imx111_asus_init_module         %s:%d\n", __func__, __LINE__);
	
	rc = platform_driver_probe(&imx111_asus_platform_driver,
		imx111_asus_platform_probe);
       dent = debugfs_create_dir("imx111_asus", NULL);	
	(void) debugfs_create_file("i2c_set", S_IRWXUGO,
					dent, NULL, &i2c_set_fops);
	(void) debugfs_create_file("i2c_get", S_IRWXUGO,
					dent, NULL, &i2c_get_fops);
	(void) debugfs_create_file("imx111_asus_power", S_IRWXUGO, dent, NULL, &imx111_asus_power_fops);
	(void) debugfs_create_file("otp_read", S_IRWXUGO, dent, NULL, &otp_read_fops);
	(void) debugfs_create_file("status", S_IRWXUGO, dent, NULL, &status_fops); 
	(void) debugfs_create_file("cci_stress_test",S_IRWXUGO,dent,NULL,&cci_stress_test_fops);
	if (!rc){
		imx111_asus_status=1;
		create_imx111_asus_proc_file();
		return rc;
	}	
	pr_err("%s:%d rc %d\n", __func__, __LINE__, rc);

	return rc;
//	return i2c_add_driver(&imx111_asus_i2c_driver);
}

static void __exit imx111_asus_exit_module(void)
{
	pr_info("%s:%d\n", __func__, __LINE__);
	if (imx111_asus_s_ctrl.pdev) {
		msm_sensor_free_sensor_data(&imx111_asus_s_ctrl);
		platform_driver_unregister(&imx111_asus_platform_driver);
	} else
//		i2c_del_driver(&imx111_asus_i2c_driver);
	return;
}

static struct msm_sensor_ctrl_t imx111_asus_s_ctrl = {
	.sensor_i2c_client = &imx111_asus_sensor_i2c_client,
	.power_setting_array.power_setting = imx111_asus_power_setting,
	.power_setting_array.size = ARRAY_SIZE(imx111_asus_power_setting),
	.msm_sensor_mutex = &imx111_asus_mut,
	.sensor_v4l2_subdev_info = imx111_asus_subdev_info,
	.sensor_v4l2_subdev_info_size = ARRAY_SIZE(imx111_asus_subdev_info),
};

module_init(imx111_asus_init_module);
module_exit(imx111_asus_exit_module);
MODULE_DESCRIPTION("imx111_asus");
MODULE_LICENSE("GPL v2");
