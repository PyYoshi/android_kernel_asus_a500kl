#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/io.h>

static const unsigned long MIN_ADDR = 0xFC000000;
static const unsigned long MAX_ADDR = 0xFDFFFFFF;

static unsigned long address = 0;
static unsigned int value = 0;
unsigned long volatile *virt_addr = 0;

// read register value
static ssize_t register_address_show(struct device *dev,
		 	             struct device_attribute *devattr,
			             char *buf)
{
	// TODO: read register value from its address
	printk("register_address_show.\n");
	if(address < MIN_ADDR || address > MAX_ADDR){
		return sprintf(buf, "cat failed: No valid address provided.\n");
	}	
	
	virt_addr = (unsigned long volatile *)ioremap(address,4);
	//printk("phys addr to virt addr. virt addr: %lu\n",virt_addr);
	printk("phys addr to virt addr succeed.\n");
	value = readl(virt_addr);
	printk("read value succeed.value:%x\n",value);
	return sprintf(buf, "Register Value = %x\n", value);
	
}

// write register address 
static ssize_t register_address_store(struct device *dev, 
				      struct device_attribute *devattr, 
				      const char *buf,
				      size_t count)
{
	// TODO: save register address
	unsigned long phys_addr = 0;
	printk("register_address_store.\n");
	phys_addr = simple_strtoul(buf, NULL, 16);
	printk("phys_addr = %lu.\n", phys_addr);
	
	// should verify it is valid in userspace ap
	if(phys_addr < MIN_ADDR || phys_addr > MAX_ADDR)  {
		printk("echo failed: Invalid userspace address entered: %lu. Please enter a valid one.\n", phys_addr);
		return count;
		}
	
	address = phys_addr;
	printk("save address done.address:%lu\n",address);
	
	// return successed
	return count;
}

static DEVICE_ATTR(
	register_address,	//register_address, 
	S_IROTH|S_IWOTH, 	// set accessiblity,
	register_address_show,	// show function, such as register_address_read,
	register_address_store  //store function, such as register_address_write
);


static int __devinit register_reader_probe(struct platform_device * pdev) 
{
	// TODO
	int err;
	// create device attribute in sysfs
	printk("register_reader_probe.\n");
 	err = sysfs_create_file(&pdev->dev.kobj, &dev_attr_register_address.attr);
	if(err<0){
		printk("sysfs_create_file failed.\n");
		kobject_del(&pdev->dev.kobj);
		return err;
	}
	printk("sysfs_create_file succeed.\n");

	return err;
}

static int __devexit register_reader_remove(struct platform_device * pdev)
{
	// TODO
	// free device attribute in sysfs
	sysfs_remove_file(&pdev->dev.kobj, &dev_attr_register_address.attr);
	printk("register_reader_remove.\n"); 
	return 0;  
}

static struct of_device_id register_reader_of_match[] = {
	{ .compatible = "asus,register_reader", },
	{ },
};
MODULE_DEVICE_TABLE(of, register_reader_of_match);

static struct platform_driver register_reader_driver = {
	.probe		= register_reader_probe,
	.remove		= __devexit_p(register_reader_remove),
	.driver		= {
		.name	= "register_reader",
		.owner	= THIS_MODULE,
		.of_match_table = register_reader_of_match,
	}
};


static int __devinit register_reader_init(void)
{
	// TODO
	printk("register_reader_init.\n");
	return platform_driver_register(&register_reader_driver);
}

static void __exit register_reader_exit(void) 
{
	// TODO
	printk("register_reader_exit.\n");
	return platform_driver_unregister(&register_reader_driver); 
}

EXPORT_COMPAT("asus,register_reader");

module_init(register_reader_init);
module_exit(register_reader_exit);
