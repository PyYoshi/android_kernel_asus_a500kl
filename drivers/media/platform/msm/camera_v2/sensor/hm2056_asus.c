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
#define HM2056_ASUS_SENSOR_NAME "hm2056_asus"
DEFINE_MSM_MUTEX(hm2056_asus_mut);

static int hm2056_asus_status=0;
static struct msm_sensor_ctrl_t hm2056_asus_s_ctrl;
#define DBG_TXT_BUF_SIZE 256


#define DBG_TXT_BUF_SIZE 256
static u32 i2c_get_value;
#define SENSOR_MAX_RETRIES      50 /* max counter for retry I2C access */

static char debugTxtBuf[DBG_TXT_BUF_SIZE];


static ssize_t status_read(struct file *file, char __user *buf, size_t count,
				loff_t *ppos)
{
	int len = 0;
	char *bp = debugTxtBuf;
	
       if (*ppos)
		return 0;	/* the end */
	   
	len = snprintf(bp, DBG_TXT_BUF_SIZE, "%d\n", hm2056_asus_status);

	if (copy_to_user(buf, debugTxtBuf, len))
		return -EFAULT;
       *ppos += len;
	return len;

}
static int sensor_read_reg(struct i2c_client *client, u16 addr, u16 *val)
{
	int err;

      
	err=hm2056_asus_s_ctrl.sensor_i2c_client->i2c_func_tbl->i2c_read(hm2056_asus_s_ctrl.sensor_i2c_client,addr,val,MSM_CAMERA_I2C_BYTE_ADDR);
	pr_err("sensor_read_reg 0x%x   addr=0x%x \n",*val,addr);
	if(err <0)
		return -EINVAL;	
	else return 0;
	
}

static int sensor_write_reg(struct i2c_client *client, u16 addr, u16 val)
{
	int err;
	int retry = 0;
	do {
		err =hm2056_asus_s_ctrl.sensor_i2c_client->i2c_func_tbl->i2c_write(hm2056_asus_s_ctrl.sensor_i2c_client,addr,val,MSM_CAMERA_I2C_BYTE_ADDR);		

		if (err == 0)
			return 0;
		retry++;
		pr_err("yuv_sensor : i2c transfer failed, retrying %x %x\n", addr, val);
		msleep(1); //LiJen: increate i2c retry delay to avoid ISP i2c fail issue
	} while (retry <= SENSOR_MAX_RETRIES);

	if(err == 0) {
		printk("%s(%d): i2c_transfer error, but return 0!?\n", __FUNCTION__, __LINE__);
		err = 0xAAAA;
	}


	return err;
}
static ssize_t i2c_get_write(struct file *file, const char __user *buf, size_t count,
				loff_t *ppos)
{
  int len=0;
  int arg = 0;


	if (*ppos)
		return 0;	/* the end */
	 len=(count > DBG_TXT_BUF_SIZE-1)?(DBG_TXT_BUF_SIZE-1):(count);
	 if (copy_from_user(debugTxtBuf,buf,len))
		return -EFAULT;

	 debugTxtBuf[len]=0; //add string end

	sscanf(debugTxtBuf, "%x", &arg);
	
		
//+ parsing......


	sensor_read_reg(hm2056_asus_s_ctrl.sensor_i2c_client->client, arg, (u16 *)&i2c_get_value);
	printk("the value is 0x%x\n", i2c_get_value);

	return len;	/* the end */
}

static ssize_t i2c_set_write(struct file *file, const char __user *buf, size_t count,
				loff_t *ppos)
{
 	int len=0;
	int arg[2];
  //int gpio, set;

  //char gpioname[8];

//  printk("%s: buf=%p, count=%d, ppos=%p\n", __FUNCTION__, buf, count, ppos);
	arg[0]=0;

	if (*ppos)
		return 0;	/* the end */
	len=(count > DBG_TXT_BUF_SIZE-1)?(DBG_TXT_BUF_SIZE-1):(count);
	if (copy_from_user(debugTxtBuf,buf,len))
		return -EFAULT;

	debugTxtBuf[len]=0; //add string end

	sscanf(debugTxtBuf, "%x %x", &arg[0], &arg[1]);



	*ppos=len;
//+ parsing......

  

	sensor_write_reg(hm2056_asus_s_ctrl.sensor_i2c_client->client, arg[0], arg[1]);
	return len;	/* the end */
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


    *ppos=len;
    
    hm2056_asus_s_ctrl.func_tbl->sensor_power_up(&hm2056_asus_s_ctrl);
    for(i=0; i<arg;i++){
		data = 0;
    	hm2056_asus_s_ctrl.sensor_i2c_client->i2c_func_tbl->i2c_read(
		hm2056_asus_s_ctrl.sensor_i2c_client,
		hm2056_asus_s_ctrl.sensordata->slave_info->sensor_id_reg_addr,
		&data, MSM_CAMERA_I2C_BYTE_DATA);
		pr_err("addr is 0x%x, data is 0x%x\n",
			hm2056_asus_s_ctrl.sensordata->slave_info->sensor_id_reg_addr,
			data);
		if(data!=0x20){
			failtime++;
			hm2056_asus_s_ctrl.func_tbl->sensor_power_down(&hm2056_asus_s_ctrl);
			msleep(8000);
			hm2056_asus_s_ctrl.func_tbl->sensor_power_up(&hm2056_asus_s_ctrl);
		}else{
			msleep(100);
		}
    }
		hm2056_asus_s_ctrl.func_tbl->sensor_power_down(&hm2056_asus_s_ctrl);
		pr_err("use cci %d fail %d\n",arg,failtime);
    return len;	/* the end */
}

static const struct file_operations cci_stress_test_fops = {
	.write = cci_stress_test_write,
};

static const struct file_operations status_fops = {
	.read		= status_read,
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
//	.read		= i2c_get_read,
	//.llseek		= seq_lseek,
	//.release	= single_release,
	.write = i2c_get_write,
};

static struct msm_sensor_power_setting hm2056_asus_power_setting[] = {
//add by sam +++
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
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_CAM_PWDN,
		.config_val = GPIO_OUT_LOW,
		.delay = 20,
	},
	/*{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_CAM_RESET,
		.config_val = GPIO_OUT_HIGH,
		.delay = 10,
	},*/
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_CAM_RESET,
		.config_val = GPIO_OUT_LOW,
		.delay = 5,
	},
	/*{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_CAM_RESET,
		.config_val = GPIO_OUT_HIGH,
		.delay = 10,
	},*/
	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VDIG,
		.config_val = 0,
		.delay = 0,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_CAM_1P8,
		.config_val = GPIO_OUT_HIGH,
		.delay = 70,
	},
/*	{
		.seq_type = SENSOR_VREG,
		.seq_val = CAM_VANA,
		.config_val = 0,
		.delay = 0,
	},	*/
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_CAM_2P8,
		.config_val = GPIO_OUT_HIGH,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_GPIO,
		.seq_val = SENSOR_GPIO_CAM_RESET,
		.config_val = GPIO_OUT_HIGH,
		.delay = 10,
	},
	{
		.seq_type = SENSOR_CLK,
		.seq_val = SENSOR_CAM_MCLK,
		.config_val = 0,
		.delay = 5,
	},
	{
		.seq_type = SENSOR_I2C_MUX,
		.seq_val = 0,
		.config_val = 0,
		.delay = 0,
	},
//add by sam ---
};

static struct v4l2_subdev_info hm2056_asus_subdev_info[] = {
	{
		.code   = V4L2_MBUS_FMT_SBGGR10_1X10,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.fmt    = 1,
		.order    = 0,
	},
};

static const struct i2c_device_id hm2056_asus_i2c_id[] = {
	{HM2056_ASUS_SENSOR_NAME, (kernel_ulong_t)&hm2056_asus_s_ctrl},
	{ }
};

static int32_t msm_hm2056_asus_i2c_probe(struct i2c_client *client,
	const struct i2c_device_id *id)
{
	return msm_sensor_i2c_probe(client, id, &hm2056_asus_s_ctrl);
}

static struct i2c_driver hm2056_asus_i2c_driver = {
	.id_table = hm2056_asus_i2c_id,
	.probe  = msm_hm2056_asus_i2c_probe,
	.driver = {
		.name = HM2056_ASUS_SENSOR_NAME,
	},
};

static struct msm_camera_i2c_client hm2056_asus_sensor_i2c_client = {
	.addr_type = MSM_CAMERA_I2C_WORD_ADDR,
};

static const struct of_device_id hm2056_asus_dt_match[] = {
	{.compatible = "qcom,hm2056_asus", .data = &hm2056_asus_s_ctrl},
	{}
};

MODULE_DEVICE_TABLE(of, hm2056_asus_dt_match);

static struct platform_driver hm2056_asus_platform_driver = {
	.driver = {
		.name = "qcom,hm2056_asus",
		.owner = THIS_MODULE,
		.of_match_table = hm2056_asus_dt_match,
	},
};

static int32_t hm2056_asus_platform_probe(struct platform_device *pdev)
{
	int32_t rc = 0;
	const struct of_device_id *match;
       int i;
	unsigned int gpio_camid, value_camid;
	struct msm_sensor_ctrl_t * info = NULL;
       for(i=0; i<ARRAY_SIZE(hm2056_asus_power_setting); i++){
               if((hm2056_asus_power_setting[i].seq_val == SENSOR_GPIO_CAM_1P8 )&&
                       (hm2056_asus_power_setting[i].config_val == GPIO_OUT_HIGH)){
       			if(g_ASUS_hwID < A500KL_SR2){
               			//After SR2, EE change Capacitance
              			 hm2056_asus_power_setting[i].delay = 70;
       			}else{
               			hm2056_asus_power_setting[i].delay = 10;
       			}
			pr_err("%dth is 1P8 PULL HIGH and delay %dms\n",
				i,hm2056_asus_power_setting[i].delay);
                     break;
               }
       }
	match = of_match_device(hm2056_asus_dt_match, &pdev->dev);
	rc = msm_sensor_platform_probe(pdev, match->data);
	info = (struct msm_sensor_ctrl_t *)match->data;
	gpio_camid= info->sensordata->gpio_conf->gpio_num_info->gpio_num[SENSOR_GPIO_CAM_CAMID];
	value_camid = __gpio_get_value(gpio_camid);
	pr_err("%s: gpio_camid is %d and value_camid is %d\n",__func__,gpio_camid,value_camid);
	return rc;
}

static int __init hm2056_asus_init_module(void)
{
	int32_t rc = 0;
	struct dentry *dent ;
	pr_info("%s:%d\n", __func__, __LINE__);
	rc = platform_driver_probe(&hm2056_asus_platform_driver,
		hm2056_asus_platform_probe);
	dent= debugfs_create_dir("hm2056_asus", NULL);	\
	(void) debugfs_create_file("status", S_IRWXUGO, dent, NULL, &status_fops); 
	(void) debugfs_create_file("i2c_set", S_IRWXUGO,
					dent, NULL, &i2c_set_fops);
	(void) debugfs_create_file("i2c_get", S_IRWXUGO,
					dent, NULL, &i2c_get_fops);
	(void) debugfs_create_file("cci_stress_test", S_IRWXUGO,
					dent, NULL, &cci_stress_test_fops);
	
	if (!rc){
		hm2056_asus_status=1;
		return rc;
	}	
	pr_err("%s:%d rc %d\n", __func__, __LINE__, rc);
	return i2c_add_driver(&hm2056_asus_i2c_driver);
}

static void __exit hm2056_asus_exit_module(void)
{
	pr_info("%s:%d\n", __func__, __LINE__);
	if (hm2056_asus_s_ctrl.pdev) {
		msm_sensor_free_sensor_data(&hm2056_asus_s_ctrl);
		platform_driver_unregister(&hm2056_asus_platform_driver);
	} else
		i2c_del_driver(&hm2056_asus_i2c_driver);
	return;
}

//add by sam +++
int32_t hm2056_asus_match_id(struct msm_sensor_ctrl_t *s_ctrl)
{
	int32_t rc = 0;
	uint16_t chipid = 0;
	rc = s_ctrl->sensor_i2c_client->i2c_func_tbl->i2c_read(
			s_ctrl->sensor_i2c_client,
			s_ctrl->sensordata->slave_info->sensor_id_reg_addr,
			&chipid, MSM_CAMERA_I2C_BYTE_DATA);
	if (rc < 0) {
		pr_err("%s: %s: read id failed\n", __func__,
			s_ctrl->sensordata->sensor_name);
		return rc;
	}

	pr_err("%s: read id: %x expected id %x:\n", __func__, chipid,
		s_ctrl->sensordata->slave_info->sensor_id);
	if (chipid != s_ctrl->sensordata->slave_info->sensor_id) {
		pr_err("msm_sensor_match_id chip id doesnot match\n");
		return -ENODEV;
	}
	pr_err("hm2056_asus_match_id rc=%d\n",rc);
	return rc;
}
//add by sam ---

static struct msm_sensor_fn_t hm2056_asus_sensor_fn_t = {
	.sensor_power_up = msm_sensor_power_up,
	.sensor_power_down = msm_sensor_power_down,
	.sensor_match_id = hm2056_asus_match_id,
	.sensor_config = msm_sensor_config,
};

static struct msm_sensor_ctrl_t hm2056_asus_s_ctrl = {
	.sensor_i2c_client = &hm2056_asus_sensor_i2c_client,
	.power_setting_array.power_setting = hm2056_asus_power_setting,
	.power_setting_array.size = ARRAY_SIZE(hm2056_asus_power_setting),
	.msm_sensor_mutex = &hm2056_asus_mut,
	.sensor_v4l2_subdev_info = hm2056_asus_subdev_info,
	.sensor_v4l2_subdev_info_size = ARRAY_SIZE(hm2056_asus_subdev_info),
	.func_tbl = &hm2056_asus_sensor_fn_t,
};

module_init(hm2056_asus_init_module);
module_exit(hm2056_asus_exit_module);
MODULE_DESCRIPTION("hm2056_asus");
MODULE_LICENSE("GPL v2");
