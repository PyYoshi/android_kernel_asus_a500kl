/*
        Smb 346 Charger IC Implementation

*/
/*
	Definition on TD
	GPIO57	GPIO58	N/A
	PEN1	PEN2	USUS
	H	x	x	3000/Rpset
	L	H	L	475mA
	L	L	L	95mA
	L	x	H	0mA(UsbSuspend)
*/
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/gpio.h>

#include "axc_Smb346Charger.h"
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/mfd/pm8xxx/pm8921-charger.h>
#ifdef CONFIG_EEPROM_PADSTATION
#include <linux/microp_notify.h>
#include <linux/microp_api.h>
#endif /*CONFIG_EEPROM_PADSTATION */
#include <linux/platform_device.h>//ASUS_BSP Eason_Chang 1120 porting
#include <linux/i2c.h>//ASUS BSP Eason_Chang smb346
#include <linux/module.h>//ASUS BSP Eason_Chang A68101032 porting
#include "axc_chargerfactory.h"


#if defined(ASUS_CHARGING_MODE) && !defined(ASUS_FACTORY_BUILD)
extern int g_chg_present;
extern char g_CHG_mode;
#endif

struct AXI_Charger *lpcharger = NULL;
#define TIME_FOR_NOTIFY_AFTER_PLUGIN_CABLE (1*HZ)



//Eason takeoff Battery shutdown +++
extern bool g_AcUsbOnline_Change0;
//Eason takeoff Battery shutdown ---
//Eason : when thermal too hot, limit charging current +++ 
//thermal limit level 0 : no limit
//thermal limit level 1: hot and low battery
//thermal limit level 2: hot but not very low battery
//thermal limit level 3: hot but no low battery
int g_thermal_limit=0;
bool g_audio_limit = false;
bool g_padMic_On = false;
//Eason : when thermal too hot, limit charging current ---

//Eason: AICL work around +++
extern int smb346_getCapacity(void);
static struct delayed_work AICLWorker;
extern bool g_alreadyCalFirstCap;
#ifndef ASUS_FACTORY_BUILD
static bool g_chgTypeBeenSet = false;//Eason : prevent setChgDrawCurrent before get chgType
static bool g_AICLlimit = false;
static bool g_AICLSuccess = false;
static unsigned long AICL_success_jiffies;
#endif//#ifndef ASUS_FACTORY_BUILD
#include <linux/jiffies.h>
AXE_Charger_Type lastTimeCableType = NO_CHARGER_TYPE;
#define ADAPTER_PROTECT_DELAY (7*HZ)

//Eason: AICL work around ---

//ASUS BSP Eason_Chang smb346+++
extern void msleep(unsigned int msecs);
static u32 g_smb346_slave_addr=0;
static int g_smb346_reg_value=0;
static int g_smb346_reg_address=0;
static struct smb346_info *g_smb346_info=NULL;
struct smb346_platform_data{
        int intr_gpio;
};

struct smb346_info {
       struct i2c_client *i2c_client;
       struct smb346_platform_data *pdata;
};
//ASUS BSP Eason_Chang smb346---

//Eason: in Pad AC powered, judge AC powered true+++
extern int InP03JudgeACpowered(void);
//Eason: in Pad AC powered, judge AC powered true---
//frank_tao add i2c stress test support+++
#ifdef CONFIG_I2C_STRESS_TEST
#include <linux/i2c_testcase.h>
#define I2C_TEST_SMB346_FAIL (-1)
#endif
//frank_tao add i2c stress test support---

#ifdef CONFIG_SMB_346_CHARGER
AXC_SMB346Charger *gpCharger = NULL;

//ASUS BSP Eason_Chang smb346 +++
struct smb346_microp_command{
    char *name;;
    u8 addr;
    u8 len;
    enum readwrite{
		E_READ=0,
		E_WRITE=1,
		E_READWRITE=2,
		E_NOUSE=3,

    }rw;

};

struct smb346_microp_command smb346_CMD_Table[]={
        {"chg_cur",                   0x00,  1,   E_READWRITE},         //SMB346_CHG_CUR
        {"cur_lim",                   0x01,  1,   E_READWRITE},         //SMB346_INPUT_CUR_LIM
        {"var_fun",                   0x02,  1,   E_READWRITE},         //SMB346_VARIOUS_FUNCTIONS
        {"float_vol",                 0x03,  1,   E_READWRITE},         //SMB346_FLOAT_VOLTAGE
        {"chg_ctrl",                  0x04,  1,   E_READWRITE},         //SMB346_CHARGE_CONTROL
        {"stat_timers_ctrl",          0x05,  1,   E_READWRITE},         //SMB346_STAT_TIMER_CONTROL
        {"pin_enable_ctrl",           0x06,  1,   E_READWRITE},         //SMB346_PIN_ENABLE_CONTROL
        {"therm_system",              0x07,  1,   E_READWRITE},         //SMB346_THERM_SYSTEM_CONTROL
        {"sysok_usb3.0",              0x08,  1,   E_READWRITE},         //SMB346_SYSOK_USB3p0
        {"other_ctrl",                0x09,  1,   E_READWRITE},         //SMB346_OTHER_CONTROL_A
        {"otg_tlim_therm",            0x0A,  1,   E_READWRITE},         //SMB346_TLIM_THERM_CONTROL
        {"hw_sw_lim_cell_temp",       0x0B,  1,   E_READWRITE},         //SMB346_HW_SW_LIMIT_CELL_TEMP
        {"fault interrupt",           0x0C,  1,   E_READ},              //SMB346_FAULT_INTERRUPT
        {"status interrupt",          0x0D,  1,   E_READ},              //SMB346_STATUS_INTERRUPT
        {"i2c_bus_slave_add",         0x0E,  1,   E_READ},              //SMB346_I2C_BUS_SLAVE
        {"command_reg_a",             0x30,  1,   E_READWRITE},         //SMB346_COMMAND_REG_A
        {"command_reg_b",             0x31,  1,   E_READWRITE},         //SMB346_COMMAND_REG_B
        {"command_reg_c",             0x33,  1,   E_READ},              //SMB346_COMMAND_REG_C
        {"interrupt_reg_a",           0x35,  1,   E_READ},              //SMB346_INTERRUPT_REG_A
        {"interrupt_reg_b",           0x36,  1,   E_READ},              //SMB346_INTERRUPT_REG_B
        {"interrupt_reg_c",           0x37,  1,   E_READ},              //SMB346_INTERRUPT_REG_C
        {"interrupt_reg_d",           0x38,  1,   E_READ},              //SMB346_INTERRUPT_REG_D
        {"interrupt_reg_e",           0x39,  1,   E_READ},              //SMB346_INTERRUPT_REG_E
        {"interrupt_reg_f",           0x3A,  1,   E_READ},              //SMB346_INTERRUPT_REG_F
        {"status_reg_a",              0x3B,  1,   E_READ},              //SMB346_STATUS_REG_A
        {"status_reg_b",              0x3C,  1,   E_READ},              //SMB346_STATUS_REG_B
        {"status_reg_c",              0x3D,  1,   E_READ},              //SMB346_STATUS_REG_C
        {"status_reg_d",              0x3E,  1,   E_READ},              //SMB346_STATUS_REG_D
        {"status_reg_e",              0x3F,  1,   E_READ},              //SMB346_STATUS_REG_E                
};

//ASUS BSP Eason add A68 charge mode +++
#define SMB346_R03_defaultValue 232
#define SMB346_R03_noFloatVoltage 192
#define OCV_TBL_SIZE 101

int NVT_FloatV_Tbl[OCV_TBL_SIZE]={
	3500, 3500, 3560, 3620, 3660, 3680, 3680, 3700, 3700, 3700,
	3700, 3700, 3700, 3700, 3720, 3720, 3740, 3740, 3760, 3760,
	3760, 3760, 3780, 3780, 3780, 3780, 3780, 3800, 3800, 3800,
	3800, 3800, 3800, 3820, 3820, 3820, 3820, 3820, 3820, 3820,
	3820, 3820, 3840, 3840, 3840, 3840, 3840, 3840, 3860, 3860,

	3860, 3860, 3860, 3880, 3880, 3880, 3880, 3900, 3900, 3900,
	3920, 3920, 3940, 3940, 3960, 3960, 3960, 3980, 3980, 3980,
	4000, 4000, 4000, 4020, 4020, 4020, 4040, 4040, 4060, 4060,
	4080, 4080, 4080, 4100, 4100, 4120, 4120, 4140, 4140, 4160,
	4160, 4180, 4180, 4200, 4200, 4220, 4220, 4240, 4240, 4260,
	4260
};
//ASUS BSP Eason add A68 charge mode ---



bool smb346_IsFull(void);

int smb346_get_charging_state(void);


static int smb346_i2c_read(u8 addr, int len, void *data)
{
        int i=0;
        int retries=6;
        int status=0;

	struct i2c_msg msg[] = {
		{
			.addr = g_smb346_slave_addr,
			.flags = 0,
			.len = 1,
			.buf = &addr,
		},
		{
			.addr = g_smb346_slave_addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = data,
		},
	};

    pr_debug("[BAT]smb346_i2c_read+++\n");
    
        if(g_smb346_info){
            do{    
                pr_debug("[BAT]before smb346_i2c_transfer\n");
        		status = i2c_transfer(g_smb346_info->i2c_client->adapter,
        			msg, ARRAY_SIZE(msg));
                
        		if ((status < 0) && (i < retries)){
        			    msleep(5);
                      
                                printk("%s retry %d\r\n", __FUNCTION__, i);                                
                                i++;
                     }
        	    } while ((status < 0) && (i < retries));
        }
        if(status < 0){
            printk("smb346: i2c read error %d \n", status);
        }
    pr_debug("[BAT]smb346_i2c_read---\n");
    

        return status;
        
}

int smb346_read_reg(int cmd, void *data)
{
    int status=0;
    if(cmd>=0 && cmd < ARRAY_SIZE(smb346_CMD_Table)){
        if(E_WRITE==smb346_CMD_Table[cmd].rw || E_NOUSE==smb346_CMD_Table[cmd].rw){ // skip read for these command
            printk("smb346: read ignore cmd\r\n");      
        }
        else{   
            pr_debug("[BAT]smb346_read_reg\n");
            status=smb346_i2c_read(smb346_CMD_Table[cmd].addr, smb346_CMD_Table[cmd].len, data);
        }    
    }
    else
        printk("smb346: unknown read cmd\r\n");
            
    return status;
}

void smb346_proc_read(void)
{    
	int status;
	int reg_value=0;
	printk("%s \r\n", __FUNCTION__);
	//full = 5 , charging = 2 , discharging = 3 
	if(smb346_IsFull())
	 g_smb346_reg_value = 5;
	else
	 if(smb346_get_charging_state()>0)
		 g_smb346_reg_value = 2;
	 else
		 g_smb346_reg_value = 3;
	
	status=smb346_read_reg(g_smb346_reg_address,&reg_value);
	//g_smb346_reg_value = reg_value;
	
	if(status > 0 && reg_value >= 0){
		 printk("[BAT][Chg][smb346] found! Charge Current=%d\r\n",reg_value);
	}

}

static int smb346_i2c_write(u8 addr, int len, void *data)
{
    int i=0;
	int status=0;
	u8 buf[len + 1];
	struct i2c_msg msg[] = {
		{
			.addr = g_smb346_slave_addr,
			.flags = 0,
			.len = len + 1,
			.buf = buf,
		},
	};
	int retries = 6;

	buf[0] = addr;
	memcpy(buf + 1, data, len);

	do {
		status = i2c_transfer(g_smb346_info->i2c_client->adapter,
			msg, ARRAY_SIZE(msg));
        	if ((status < 0) && (i < retries)){
                    msleep(5);
           
                    printk("%s retry %d\r\n", __FUNCTION__, i);
                    i++;
              }
       } while ((status < 0) && (i < retries));

	if (status < 0) {
                    printk("smb346: i2c write error %d \n", status);
	}

	return status;
}

int smb346_write_reg(int cmd, void *data){
    int status=0;
    if(cmd>=0 && cmd < ARRAY_SIZE(smb346_CMD_Table)){
        if(E_READ==smb346_CMD_Table[cmd].rw  || E_NOUSE==smb346_CMD_Table[cmd].rw){ // skip read for these command               
        }
        else
            status=smb346_i2c_write(smb346_CMD_Table[cmd].addr, smb346_CMD_Table[cmd].len, data);
    }
    else
        printk("smb346: unknown write cmd\r\n");
            
    return status;
}

void smb346_proc_write(void)
{    
       int status;
       uint8_t i2cdata[32]={0};

       i2cdata[0] = g_smb346_reg_value;
       printk("%s:%d \r\n", __FUNCTION__,g_smb346_reg_value);
       status=smb346_write_reg(g_smb346_reg_address,i2cdata);

       if(status > 0 ){
            printk("[BAT][Chg][smb346] proc write\r\n");
       }
}

void smb346_write_enable(void)
{    
       int status;
       uint8_t i2cdata[32]={0};

	//ASUS BSP Eason_Chang OTG +++
	status=smb346_read_reg(SMB346_COMMAND_REG_A,i2cdata);
	i2cdata[0] |= 0x80;
	//ASUS BSP Eason_Chang OTG ---

	pr_debug("%s \r\n", __FUNCTION__);
	status=smb346_write_reg(SMB346_COMMAND_REG_A,i2cdata);

	if(status > 0 ){
		pr_debug("[BAT][Chg][smb346] write enable\r\n");
	}
}

//ASUS BSP Eason_Chang OTG +++
static void smb346OtgEnableBit(bool switchOtgBit)
{
     int status;
     uint8_t i2cdata[32]={0};

     if(true == switchOtgBit)
     {
		i2cdata[0] = 0x90;//cause smb346_write_enable SMB346_COMMAND_REG_A(R30h) will be 0x80
		status=smb346_write_reg(SMB346_COMMAND_REG_A,i2cdata);
		if(status > 0 ){
			printk("[BAT][Chg][smb346] otg enableBit true\r\n");
		}
     }else{
		i2cdata[0] = 0x80;//cause smb346_write_enable SMB346_COMMAND_REG_A(R30h) will be 0x80
		status=smb346_write_reg(SMB346_COMMAND_REG_A,i2cdata);
		if(status > 0 ){
			printk("[BAT][Chg][smb346] otg enableBit false\r\n");
		}
     }
}

static void smb346Otg500Ma(bool switchOtg500)
{
     int status;
     uint8_t i2cdata[32]={0};

     if(true == switchOtg500)
     {
		i2cdata[0] = 0x0A;//set otg 500 mA & OTG_uvlo_3p1V
		status=smb346_write_reg(SMB346_TLIM_THERM_CONTROL,i2cdata);
		if(status > 0 ){
			printk("[BAT][Chg][smb346] otg 500Ma true\r\n");
		}
     }else{
		i2cdata[0] = 0x04;//usb default 250 mA
		status=smb346_write_reg(SMB346_TLIM_THERM_CONTROL,i2cdata);
		if(status > 0 ){
				printk("[BAT][Chg][smb346] otg 500Ma false\r\n");
		}
     }
}

static void smb346_otg_enable(bool switchOtg)
{
	if(true == switchOtg)
	{
		smb346Otg500Ma(false);
		smb346OtgEnableBit(true);
		smb346Otg500Ma(true);
		printk("[BAT][Chg][smb346] otg enable true\r\n");
	}else{
		smb346OtgEnableBit(false);
		smb346Otg500Ma(false);
		printk("[BAT][Chg][smb346] otg enable false\r\n");
	}
}

void UsbSetOtgSwitch(bool switchOtg)
{
	if(true==switchOtg)
	{
		smb346_write_enable();
		smb346_otg_enable(true);
	}else{
		smb346_write_enable();
		smb346_otg_enable(false);
	}
}
//ASUS BSP Eason_Chang OTG ---
//ASUS BSP Eason_Chang smb346 ---

//ASUS BSP Eason add A68 charge mode +++
static void setSmb346FloatVoltage(int reg_value)
{
     uint8_t i2cdata[32]={0};
     i2cdata[0] = reg_value;

     smb346_write_enable();
     smb346_write_reg(SMB346_FLOAT_VOLTAGE,i2cdata);	
}

void setFloatVoltage(int StopPercent)
{
	int reg_add_value = 0;
        int reg_value = SMB346_R03_defaultValue;//R03 default value

	reg_add_value = (NVT_FloatV_Tbl[StopPercent]-3500)/20;
        reg_value = SMB346_R03_noFloatVoltage + reg_add_value; 
	printk("[BAT][smb346]StopVoltage:%d,R03reg_value: %d\n",NVT_FloatV_Tbl[StopPercent],reg_value);

        setSmb346FloatVoltage(reg_value);
}
//ASUS BSP Eason add A68 charge mode ---
void setUSBin_MAX_1200mA(void)
{
	 uint8_t i2cdata[32]={0};
     i2cdata[0] = 4;

     smb346_write_enable();
     smb346_write_reg(SMB346_INPUT_CUR_LIM,i2cdata);
	 smb346_read_reg(SMB346_INPUT_CUR_LIM,i2cdata);
	 printk("[BAT][Chg][smb346] SMB346_INPUT_CUR_LIM = %d \n",i2cdata[0]);
}
void setHC_mode(bool flag)
{
	int status;
	uint8_t i2cdata[32]={0};

	//ASUS BSP Eason_Chang OTG +++
	status=smb346_read_reg(SMB346_COMMAND_REG_B,i2cdata);
	printk("[BAT][Chg][smb346] pre-setHC_mode %d %d \n",flag,i2cdata[0]);
	if(flag == true)
		i2cdata[0] |= 0x03;
	else
		i2cdata[0] &= 0xfe;
	//ASUS BSP Eason_Chang OTG ---

	pr_debug("%s \r\n", __FUNCTION__);
	status=smb346_write_reg(SMB346_COMMAND_REG_B,i2cdata);

	if(status > 0 ){
		printk("[BAT][Chg][smb346] set HC_mode success\r\n");
		status=smb346_read_reg(SMB346_COMMAND_REG_B,i2cdata);
		printk("[BAT][Chg][smb346] setHC_mode %d %d \n",flag,i2cdata[0]);
	}
	else
		printk("[BAT][Chg][smb346] set HC_mode fail\r\n");
}
void set_register_or_pin_control(int value)
{
	int status;
    uint8_t i2cdata[32]={0};

	//ASUS BSP Eason_Chang OTG +++
	status=smb346_read_reg(SMB346_PIN_ENABLE_CONTROL,i2cdata);
	printk("[BAT][Chg][smb346] pre-set_register_or_pin_control %d %d \n",value,i2cdata[0]);
	if(value == 0)
		i2cdata[0] &= 0xef;
	else
		i2cdata[0] |= 0x10;
	//ASUS BSP Eason_Chang OTG ---

	pr_debug("%s \r\n", __FUNCTION__);
	status=smb346_write_reg(SMB346_PIN_ENABLE_CONTROL,i2cdata);

	if(status > 0 ){
		printk("[BAT][Chg][smb346] set_register_or_pin_control success\r\n");
		status=smb346_read_reg(SMB346_PIN_ENABLE_CONTROL,i2cdata);
		printk("[BAT][Chg][smb346] set_register_or_pin_control %d %d \n",value,i2cdata[0]);
	}
}
// For checking initializing or not
static bool AXC_Smb346_Charger_GetGPIO(int anPin)
{
	return gpio_get_value(anPin);
}

static void AXC_Smb346_Charger_SetGPIO(int anPin, bool abHigh)
{
	gpio_set_value(anPin,abHigh);
	printk( "[BAT][CHG]SetGPIO pin%d,%d:%d\n",anPin,abHigh,AXC_Smb346_Charger_GetGPIO(anPin));
}
static void AXC_Smb346_Charger_SetPIN1(AXC_SMB346Charger *this,bool abHigh)
{
#ifdef CONFIG_ENABLE_PIN1   
	AXC_Smb346_Charger_SetGPIO(this->mpGpio_pin[Smb346_PIN1].gpio,abHigh);
#endif
}
#ifdef CONFIG_ENABLE_PIN2
static void AXC_Smb346_Charger_SetPIN2(AXC_SMB346Charger *this,bool abHigh)
{
	AXC_Smb346_Charger_SetGPIO(this->mpGpio_pin[Smb346_PIN2].gpio,abHigh);
}
#endif

static void AXC_Smb346_Charger_SetChargrDisbalePin(AXC_SMB346Charger *this,bool abDisabled)
{
	
	AXC_Smb346_Charger_SetGPIO(this->mpGpio_pin[Smb346_CHARGING_DISABLE].gpio,abDisabled);

}
static void EnableCharging(struct AXI_Charger *apCharger, bool enabled)
{
    AXC_SMB346Charger *this = container_of(apCharger,
                                            AXC_SMB346Charger,
                                            msParentCharger);
	if(enabled == true)
		printk("[BAT][Chg][smb346] open charger! \n");
	else
		printk("[BAT][Chg][smb346] close charger! \n");
    AXC_Smb346_Charger_SetChargrDisbalePin(this,!enabled);
}

void Smb346_Enable_Charging(bool enabled)
{
	
	/*if(NULL == lpcharger)
    	{
		AXC_ChargerFactory_GetCharger(E_SMB346_CHARGER_TYPE ,&lpcharger);
		lpcharger->Init(lpcharger);
		lpcharger->EnableCharging(lpcharger,false);
	}*/
	lpcharger->EnableCharging(lpcharger,enabled);

}

//Eason: Do VF with Cable when boot up+++
int getIfonline(void)
{
    return !gpio_get_value(gpCharger->mpGpio_pin[Smb346_DC_IN].gpio);
}
//Eason: Do VF with Cable when boot up---

static int AXC_Smb346_Charger_InitGPIO(AXC_SMB346Charger *this)
{
    Smb346_PIN_DEF i;

    int err = 0;

    for(i = 0; i<Smb346_PIN_COUNT;i++){
        //request
        err  = gpio_request(this->mpGpio_pin[i].gpio, this->mpGpio_pin[i].name);
        if (err < 0) {
            printk( "[BAT][CHG]gpio_request %s failed, err = %d\n",this->mpGpio_pin[i].name, err);
            goto err_exit;
        }

        //input
        if(this->mpGpio_pin[i].in_out_flag == 0){

            err = gpio_direction_input(this->mpGpio_pin[i].gpio);
            
            if (err  < 0) {
                printk( "[BAT][CHG]gpio_direction_input %s failed, err = %d\n", this->mpGpio_pin[i].name,err);
                goto err_exit;
            }

            if(this->mpGpio_pin[i].handler != NULL){

                this->mpGpio_pin[i].irq = gpio_to_irq(this->mpGpio_pin[i].gpio);

                if(true == this->mpGpio_pin[i].irq_enabled){
                    
                    enable_irq_wake(this->mpGpio_pin[i].irq);

                }

                err = request_irq(this->mpGpio_pin[i].irq , 
                    this->mpGpio_pin[i].handler, 
                    this->mpGpio_pin[i].trigger_flag,
                    this->mpGpio_pin[i].name, 
                    this);

                if (err  < 0) {
                    printk( "[BAT][CHG]request_irq %s failed, err = %d\n", this->mpGpio_pin[i].name,err);
                    goto err_exit;
                }

            }

        }else{//output

            gpio_direction_output(this->mpGpio_pin[i].gpio, 
                this->mpGpio_pin[i].init_value);            
        }
    }

    return 0;
    
err_exit:

    for(i = 0; i<Smb346_PIN_COUNT;i++){
        
        gpio_free(this->mpGpio_pin[i].gpio);
        
    }
    
    return err;
}

static void AXC_Smb346_Charger_DeInitGPIO(AXC_SMB346Charger *this)
{
    Smb346_PIN_DEF i;
    
    for(i = 0; i<Smb346_PIN_COUNT;i++){
        
        gpio_free(this->mpGpio_pin[i].gpio);
        
    }
}


static void AXC_Smb346_Charger_NotifyClientForStablePlugIn(struct work_struct *dat)
{
	AXC_SMB346Charger *this = container_of(dat,
                                                AXC_SMB346Charger,
                                                msNotifierWorker.work);
	printk("[BAT][CHG]%s chargetype = %d\n",__FUNCTION__,lastTimeCableType);
	this->msParentCharger.SetCharger(&this->msParentCharger, lastTimeCableType);
}

//ASUS_BSP +++ frank_tao "suspend for fastboot mode"
#ifdef CONFIG_FASTBOOT
#include <linux/fastboot.h>
#endif //#ifdef CONFIG_FASTBOOT
//ASUS_BSP --- frank_tao "suspend for fastboot mode"
static void (*notify_charger_in_out_func_ptr)(int) = NULL;
//Eason : support OTG mode+++
static void (*notify_charger_i2c_ready_func_ptr)(void) = NULL;
//Eason : support OTG mode---
static DEFINE_SPINLOCK(charger_in_out_debounce_lock);
static void charger_in_out_debounce_time_expired(unsigned long _data)
{
    unsigned long flags;

    int online;

    struct AXC_SMB346Charger *this = (struct AXC_SMB346Charger *)_data;
    
    spin_lock_irqsave(&charger_in_out_debounce_lock, flags);

    online = !gpio_get_value(this->mpGpio_pin[Smb346_DC_IN].gpio);

    printk("[BAT][CHG]%s,%d\n",__FUNCTION__,online);

    wake_lock_timeout(&this->cable_in_out_wakelock, 3 * HZ);

    //ASUS_BSP +++ frank_tao "suspend for fastboot mode"
#ifdef CONFIG_FASTBOOT
      if(is_fastboot_enable()){

        if(online){
            ready_to_wake_up_and_send_power_key_press_event_in_fastboot();
        }
      }
#endif //#ifdef CONFIG_FASTBOOT
    //ASUS_BSP --- frank_tao "suspend for fastboot mode"

#if defined(ASUS_CHARGING_MODE) && !defined(ASUS_FACTORY_BUILD)
    g_chg_present = online;
#endif

    if(NULL != notify_charger_in_out_func_ptr){
    
         (*notify_charger_in_out_func_ptr) (online);
    
    }else{
    
         printk("Nobody registed..\n");
    }
    
    enable_irq_wake(this->mpGpio_pin[Smb346_DC_IN].irq);

    spin_unlock_irqrestore(&charger_in_out_debounce_lock, flags);

}

static irqreturn_t charger_in_out_handler(int irq, void *dev_id)
{
    unsigned long flags;
    
    AXC_SMB346Charger *this = (AXC_SMB346Charger *)dev_id;

    if(!timer_pending(&this->charger_in_out_timer)){

        spin_lock_irqsave(&charger_in_out_debounce_lock, flags);

        disable_irq_wake(irq);

        mod_timer(&this->charger_in_out_timer, jiffies + msecs_to_jiffies(20));

        spin_unlock_irqrestore(&charger_in_out_debounce_lock, flags);

    }
        
    return IRQ_HANDLED;

}
#ifdef ENABLE_WATCHING_STATUS_PIN_IN_IRQ
static void status_handle_work(struct work_struct *work)
{

    if(NULL == gpCharger){
        return;
    }

    printk("[BAT][CHG]%s,%d\n",__FUNCTION__,gpCharger->msParentCharger.IsCharging(&gpCharger->msParentCharger));

   if(true == gpCharger->mpGpio_pin[Smb346_CHARGING_STATUS].irq_enabled){
        disable_irq_wake(gpCharger->mpGpio_pin[Smb346_CHARGING_STATUS].irq);
        gpCharger->mpGpio_pin[Smb346_CHARGING_STATUS].irq_enabled = false;
    }    

    if(NULL != gpCharger->mpNotifier){

        gpCharger->mpNotifier->onChargingStart(&gpCharger->msParentCharger, gpCharger->msParentCharger.IsCharging(&gpCharger->msParentCharger));
    }    

}

static irqreturn_t charging_status_changed_handler(int irq, void *dev_id)
{
    
    AXC_SMB346Charger *this = (AXC_SMB346Charger *)dev_id;

    schedule_delayed_work(&this->asus_chg_work, (2000 * HZ/1000));
    
    return IRQ_HANDLED;

}
#endif

static int smb346_read_table(int tableNumber)
{    
       int status;
       int reg_value=0;

       status=smb346_read_reg(tableNumber,&reg_value);

       if(status > 0 && reg_value >= 0){
            pr_debug("[BAT][Chg][smb346][table] found! Charge Current=%d\r\n",reg_value);
       }
       return reg_value;
}

//Eason takeoff smb346 IRQ handler+++   
/*
static void smb346_readIRQSignal(void)
{

    int HardLimitIrq = 0;
    int AICLDoneIrq = 0;
    int BatOvpIrq = 0;
    int ReChgIrq = 0;
    int reg35_value = 0;
    int reg38_value = 0;
    int reg36_value = 0;
    int reg37_value = 0;

    printk("[BAT][smb346][IRQ]:read signal\n");

	reg35_value = smb346_read_table(18);//S35[6] Hot/Cold Hard Limit
        HardLimitIrq = reg35_value & 64;
        if(64==HardLimitIrq)
        {
		printk("[BAT][smb346][IRQ]:Hard Limit IRQ\n");
	}

	reg38_value = smb346_read_table(21);//S38[4] AICL Done
	AICLDoneIrq = reg38_value & 16;
	if(16==AICLDoneIrq)
	{
		printk("[BAT][smb346][IRQ]: AICL Done IRQ\n");		
	}

	reg36_value = smb346_read_table(19);//S36[6] Battery OVP
	BatOvpIrq = reg36_value & 64;
	if(64==AICLDoneIrq)
	{
		printk("[BAT][smb346][IRQ]: Bat OVP IRQ\n");		
	}

	reg37_value = smb346_read_table(20);//S37[4] Recharge
	ReChgIrq = reg37_value & 16;
	if(16==ReChgIrq)
	{
		printk("[BAT][smb346][IRQ]: Recharge IRQ\n");		
	}
}

static void IrqHandleWork(struct work_struct *work)
{
         smb346_readIRQSignal();
}

static irqreturn_t smb346_irq_handler(int irq, void *dev_id)
{ 
    AXC_SMB346Charger *this = (AXC_SMB346Charger *)dev_id;

	IsChgIRQ = gpio_get_value(this->mpGpio_pin[Smb346_CHARGING_IRQ].gpio);
    pr_debug("[BAT][smb346][IRQ]:%d\n",IsChgIRQ);
    schedule_delayed_work(&this->smb346_irq_worker, 0*HZ);
    return IRQ_HANDLED;  
}
*/
// Eason takeoff smb346 IRQ handler---  

//static void create_charger_proc_file(void); //ASUS BSP Eason_Chang smb346
static Smb346_PIN gGpio_pin[]=
{
    {//346_DC_IN
        .gpio = 27,
        .name = "346_DCIN",
        .in_out_flag = 0,
        .handler = charger_in_out_handler,
        .trigger_flag= IRQF_TRIGGER_FALLING|IRQF_TRIGGER_RISING, 
        .irq_enabled = false,        
    },
#ifdef CONFIG_ENABLE_PIN1       
    {//346_PIN1
        .gpio = 97,
        .name = "346_Pin1",
        .in_out_flag = 1,
        .init_value = 0,
    },
#endif    
    {//346_CHARGING_DISABLE
        .gpio = 109,
        .name = "346_Disable",
        .in_out_flag = 1,
        .init_value = 0,
    },
    {//346_CHARGING_STATUS
        .gpio = 31,
        .name = "346_IRQ",
        .in_out_flag = 0,
        //.handler = smb346_irq_handler, // Eason takeoff smb346 IRQ handler   
        .trigger_flag= IRQF_TRIGGER_FALLING,
        .irq_enabled = false,
    },
};

//ASUS BSP Eason_Chang smb346 +++
static ssize_t smb346charger_read_proc(char *page, char **start, off_t off, int count, 
            	int *eof, void *data)
{
	smb346_proc_read();
	return sprintf(page, "%d\n", g_smb346_reg_value);
}
static ssize_t smb346charger_write_proc(struct file *filp, const char __user *buff, 
	            unsigned long len, void *data)
{
	int val;

	char messages[256];

	if (len > 256) {
		len = 256;
	}

	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	
	val = (int)simple_strtol(messages, NULL, 10);


	g_smb346_reg_value = val;

        smb346_write_enable();
        smb346_proc_write();
    
    printk("[BAT][smb346]mode:%d\n",g_smb346_reg_value);
	
	return len;
}

void static create_smb346_proc_file(void)
{
	struct proc_dir_entry *smb346_proc_file = create_proc_entry("driver/smb346chg", 0644, NULL);

	if (smb346_proc_file) {
		smb346_proc_file->read_proc = smb346charger_read_proc;
		smb346_proc_file->write_proc = smb346charger_write_proc;
	}
    else {
		printk("[BAT][Bal]proc file create failed!\n");
    }

	return;
}

static ssize_t smb346ChgAddress_read_proc(char *page, char **start, off_t off, int count, 
            	int *eof, void *data)
{
	return sprintf(page, "%d\n", g_smb346_reg_address);
}
static ssize_t smb346ChgAddress_write_proc(struct file *filp, const char __user *buff, 
	            unsigned long len, void *data)
{
	int val;

	char messages[256];

	if (len > 256) {
		len = 256;
	}

	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	
	val = (int)simple_strtol(messages, NULL, 10);


	g_smb346_reg_address = val;  
    
    printk("[BAT][smb346]ChgAddress:%d\n",val);
	
	return len;
}

void static create_smb346ChgAddress_proc_file(void)
{
	struct proc_dir_entry *smb346ChgAddress_proc_file = create_proc_entry("driver/smb346ChgAddr", 0644, NULL);

	if (smb346ChgAddress_proc_file) {
		smb346ChgAddress_proc_file->read_proc = smb346ChgAddress_read_proc;
		smb346ChgAddress_proc_file->write_proc = smb346ChgAddress_write_proc;
	}
    else {
		printk("[BAT][Bal]proc file create failed!\n");
    }

	return;
}

//ASUS BSP Eason_Chang smb346 ---

static ssize_t smb346ChargerStatus_read_proc(char *page, char **start, off_t off, int count, 
            	int *eof, void *data)
{
	int status,chgstatus;
	int reg_value = 0;
	status = smb346_read_reg(0,&reg_value);
	printk("[BAT][Chg][smb346] status = %d \n",status);
	if(status > 0)
		chgstatus = 1;
	else
		chgstatus = 0;
	return sprintf(page, "%d\n", chgstatus);
}

static ssize_t smb346ChargerStatus_write_proc(struct file *filp, const char __user *buff, 
	            unsigned long len, void *data)
{
	return -1;
}

void static create_smb346ChargerStatus_proc_file(void)
{
	struct proc_dir_entry *smb346ChargerStatus_proc_file = create_proc_entry("driver/smb346ChargerStatus", 0644, NULL);

	if (smb346ChargerStatus_proc_file) {
		smb346ChargerStatus_proc_file->read_proc = smb346ChargerStatus_read_proc;
		smb346ChargerStatus_proc_file->write_proc = smb346ChargerStatus_write_proc;
	}
    else {
		printk("[BAT][Bal]proc file create failed!\n");
    }

	return;
}


//Function implementation for AXC_Smb346_Charger
static void AXC_Smb346_Charger_Init(AXI_Charger *apCharger)
{
    AXC_SMB346Charger *this = container_of(apCharger,
                                            AXC_SMB346Charger,
                                            msParentCharger);

    if(false == this->mbInited)
    {
		printk( "[BAT][CHG]Init++\n");
		this->type = NO_CHARGER_TYPE;
		this->mpNotifier = NULL;
		INIT_DELAYED_WORK(&this->msNotifierWorker, AXC_Smb346_Charger_NotifyClientForStablePlugIn) ;

#ifdef ENABLE_WATCHING_STATUS_PIN_IN_IRQ

        INIT_DELAYED_WORK(&this->asus_chg_work, status_handle_work);
#endif
             this->mpGpio_pin = gGpio_pin;



        wake_lock_init(&this->cable_in_out_wakelock, WAKE_LOCK_SUSPEND, "cable in out");

        setup_timer(&this->charger_in_out_timer, charger_in_out_debounce_time_expired,(unsigned long)this);

		if (0 == AXC_Smb346_Charger_InitGPIO(this)) {
			this->mbInited = true;
		} else {
			printk( "[BAT][CHG]Charger can't init\n");
        }
        
        charger_in_out_debounce_time_expired((unsigned long)this);
        
        //charger_in_out_handler(this->mpGpio_pin[Smb346_DC_IN].irq, this);

    //ASUS BSP Eason_Chang smb346 +++
    //	create_charger_proc_file();
    create_smb346_proc_file();
    create_smb346ChgAddress_proc_file(); 
    create_smb346ChargerStatus_proc_file();
    //INIT_DELAYED_WORK(&this->smb346_irq_worker,IrqHandleWork);//Eason takeoff smb346 IRQ handler
    //ASUS BSP Eason_Chang smb346 ---
             gpCharger = this;
		printk( "[BAT][CHG]Init--\n");
    }
}

static void AXC_Smb346_Charger_DeInit(AXI_Charger *apCharger)
{
    AXC_SMB346Charger *this = container_of(apCharger,
                                            AXC_SMB346Charger,
                                            msParentCharger);

    if(true == this->mbInited) {
	AXC_Smb346_Charger_DeInitGPIO(this);
        this->mbInited = false;
    }
}

int AXC_Smb346_Charger_GetType(AXI_Charger *apCharger)
{
    AXC_SMB346Charger *this = container_of(apCharger,
                                            AXC_SMB346Charger,
                                            msParentCharger);

    return this->mnType;
}

void AXC_Smb346_Charger_SetType(AXI_Charger *apCharger ,int anType)
{
    AXC_SMB346Charger *this = container_of(apCharger,
                                            AXC_SMB346Charger,
                                            msParentCharger);

    this->mnType = anType;
}

static AXE_Charger_Type AXC_Smb346_Charger_GetChargerStatus(AXI_Charger *apCharger)
{
	AXC_SMB346Charger *this = container_of(apCharger,
                                                AXC_SMB346Charger,
                                                msParentCharger);

	return this->type;
}

// joshtest
/*

static struct timespec g_charger_update_time = {0};

static void chg_set_charger_update_time(void)
{
	g_charger_update_time = current_kernel_time();
	printk( "[BAT][Chg] %s(), tv_sec:%ld\n",
		__func__,
		g_charger_update_time.tv_sec);
	return;
}


void chg_get_charger_update_time(struct timespec *charger_update_time)
{
	BUG_ON(NULL != charger_update_time);
	*charger_update_time = g_charger_update_time;
	printk( "[BAT][Chg] %s(), tv_sec:%ld\n",
		__func__,
		charger_update_time->tv_sec);
	return;
}
*/

// joshtest
static char *pm_power_supplied_to[] = {
	"battery",
};

static enum power_supply_property pm_power_props[] = {
    POWER_SUPPLY_PROP_PRESENT,
    POWER_SUPPLY_PROP_ONLINE,
};

static int pm_power_get_property(struct power_supply *psy,
				  enum power_supply_property psp,
				  union power_supply_propval *val)
{
        AXE_Charger_Type type;


       if(NULL == gpCharger){

            val->intval = 0;
            return 0;
       }

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	case POWER_SUPPLY_PROP_ONLINE:
             val->intval =  0;

            type =gpCharger->msParentCharger.GetChargerStatus(&gpCharger->msParentCharger);

        
             if(NO_CHARGER_TYPE < type){

		   //Eason: in Pad AC powered, judge AC powered true+++
		   #if 0
                if(psy->type ==POWER_SUPPLY_TYPE_MAINS && 
                    (type == HIGH_CURRENT_CHARGER_TYPE || 
                    type == ILLEGAL_CHARGER_TYPE|| 
                    type == NORMAL_CURRENT_CHARGER_TYPE)){            

                    val->intval =  1;

                }
		   #endif
		   
               if(psy->type ==POWER_SUPPLY_TYPE_MAINS && 
                    (type == HIGH_CURRENT_CHARGER_TYPE || 
                    type == ILLEGAL_CHARGER_TYPE))
                {            

                    val->intval =  1;

                }else if((psy->type ==POWER_SUPPLY_TYPE_MAINS) && (type == NORMAL_CURRENT_CHARGER_TYPE))
                {
			if(1==InP03JudgeACpowered())
				val->intval =  1;
                
		   }
	         //Eason: in Pad AC powered, judge AC powered true---	

		   

                 if(psy->type ==POWER_SUPPLY_TYPE_USB && type == LOW_CURRENT_CHARGER_TYPE){            
                 
                     val->intval =  1;
                 
                 }

             }

             //if(true == g_AcUsbOnline_Change0)
             //{
                     //val->intval = 0;
                     //printk("[BAT][Chg]: set online 0 to shutdown device\n");   
             //}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}



static struct power_supply usb_psy = {
	.name		= "usb",
	.type		= POWER_SUPPLY_TYPE_USB,
	.supplied_to = pm_power_supplied_to,
       .num_supplicants = ARRAY_SIZE(pm_power_supplied_to),
	.properties	= pm_power_props,
	.num_properties	= ARRAY_SIZE(pm_power_props),
	.get_property	= pm_power_get_property,
};

static struct power_supply main_psy = {
	.name		= "ac",
	.type		= POWER_SUPPLY_TYPE_MAINS,
	.supplied_to = pm_power_supplied_to,
       .num_supplicants = ARRAY_SIZE(pm_power_supplied_to),
	.properties	= pm_power_props,
	.num_properties	= ARRAY_SIZE(pm_power_props),
	.get_property	= pm_power_get_property,
};


//Eason: set default Float Voltage  +++
static void setSmb346FloatVoltageDefault(void)
{
     uint8_t i2cdata[32]={0};

     i2cdata[0] = 232;
	 
	smb346_write_enable();
	smb346_write_reg(SMB346_FLOAT_VOLTAGE,i2cdata);	
	printk("[BAT][CHG]Default Float Voltage\n");	 
}
//Eason: set default Float Voltage ---

//Eason: A68 charging in P03 limit 900mA, when recording limit 500mA ++++
#ifndef ASUS_FACTORY_BUILD
static void enableAICL(void)
{
     uint8_t i2cdata[32]={0};

     i2cdata[0] = 151;

     smb346_write_enable();
     smb346_write_reg(2,i2cdata);
}

static void disableAICL(void)
{
     uint8_t i2cdata[32]={0};

     i2cdata[0] = 135;

     smb346_write_enable();
     smb346_write_reg(2,i2cdata);
}

static void limitSmb346chg1200(void)
{
     uint8_t i2cdata[32]={0};

     i2cdata[0] = 68;// 1200mA

     smb346_write_enable();
     smb346_write_reg(1,i2cdata);	
}

static void limitSmb346chg900(void)
{
     uint8_t i2cdata[32]={0};

     i2cdata[0] = 51;// 900mA

     smb346_write_enable();
     smb346_write_reg(1,i2cdata);	
}
static void limitSmb346chg700(void)
{
     uint8_t i2cdata[32]={0};

     i2cdata[0] = 0x22;// 700mA

     smb346_write_enable();
     smb346_write_reg(1,i2cdata);	
}

static void limitSmb346chg500(void)
{
     uint8_t i2cdata[32]={0};

     i2cdata[0] = 17;// 500mA

     smb346_write_enable();
     smb346_write_reg(1,i2cdata);	
}
//Eason: Pad draw rule compare thermal  +++
//static void limitSmb346chg300(void)
//{
     //uint8_t i2cdata[32]={0};

     //i2cdata[0] = 0x00;// 300mA

     //smb346_write_enable();
     //smb346_write_reg(1,i2cdata);	
//}
//Eason: Pad draw rule compare thermal ---

static void defaultSmb346chgSetting(void)
{
	uint8_t i2cdata[32]={0};

	i2cdata[0] = 0x21;// default:DC_IN=700mA USB_IN=500

	smb346_write_enable();
	smb346_write_reg(1,i2cdata);
	set_register_or_pin_control(1);
}
#endif//#ifndef ASUS_FACTORY_BUILD
//Eason: A68 charging in P03 limit 900mA, when recording limit 500mA ---
//Eason:  Pad draw rule compare thermal +++
#ifndef ASUS_FACTORY_BUILD
PadDrawLimitCurrent_Type JudgePadThermalDrawLimitCurrent(void)
{
	if( (3==g_thermal_limit)||(true==g_audio_limit) )
	{
		return PadDraw500;
   	}else if(true==g_padMic_On){

		return PadDraw500;
	}else if( 2==g_thermal_limit )
	{
		return PadDraw700;
	}else if(1 == g_thermal_limit)
	{
		return PadDraw900;
	}else
		return PadDraw900;
}
void setChgLimitThermalRuleDrawCurrent(void)
{
	//PadDrawLimitCurrent_Type PadThermalDraw_limit;
	//PadDrawLimitCurrent_Type PadRuleDraw_limit;
	//PadDrawLimitCurrent_Type MinThermalRule_limit;

	//PadThermalDraw_limit=JudgePadThermalDrawLimitCurrent();
	//PadRuleDraw_limit=JudgePadRuleDrawLimitCurrent();
	//MinThermalRule_limit = min(PadThermalDraw_limit,PadRuleDraw_limit);
	//printk("[BAT][CHG][P03]:Thermal:%d,Rule:%d,Min:%d\n",PadThermalDraw_limit,PadRuleDraw_limit,MinThermalRule_limit);

	//disableAICL();

	////ForcePowerBankMode draw 900, but  still compare thermal result unless PhoneCap<=8
	//if( (PadDraw900==PadRuleDraw_limit)&&(smb346_getCapacity()<= 8))
	//{
		//limitSmb346chg900();
		//printk("[BAT][CHG][P03]:draw 900\n");
	//}else{
		//switch(MinThermalRule_limit)
		//{
			//case PadDraw300:
				//limitSmb346chg300();
				//printk("[BAT][CHG][P03]:draw 300\n");
				//break;
			//case PadDraw500:
				//limitSmb346chg500();
				//printk("[BAT][CHG][P03]:draw 500\n");
				//break;
			//case PadDraw700:
				//limitSmb346chg700();
				//printk("[BAT][CHG][P03]:draw 700\n");
				//break;
			//case PadDraw900:
				//limitSmb346chg900();
				//printk("[BAT][CHG][P03]:draw 900\n");
				//break;
			//default:
				//limitSmb346chg700();
				//printk("[BAT][CHG][P03]:draw 700\n");
				//break;
		//}
	//}
	
	//enableAICL();

}
#endif//#ifndef ASUS_FACTORY_BUILD
//Eason:  Pad draw rule compare thermal ---
//Eason : when thermal too hot, limit charging current +++ 
void setChgDrawCurrent(void)
{
#ifndef ASUS_FACTORY_BUILD   
  if(true == g_chgTypeBeenSet)//Eason : prevent setChgDrawCurrent before get chgType
  {
	if(NORMAL_CURRENT_CHARGER_TYPE==gpCharger->type)
	{
		setChgLimitThermalRuleDrawCurrent();
	}else if(NOTDEFINE_TYPE==gpCharger->type
			||NO_CHARGER_TYPE==gpCharger->type)
	{
		disableAICL();
		defaultSmb346chgSetting();
		enableAICL();
		printk("[BAT][CHG]:default USB_IN = 500mA  DC_IN = 700MA\n");
	}else if( (3==g_thermal_limit)||(true==g_audio_limit) )
	{
		disableAICL();
		limitSmb346chg500();
		enableAICL();
		printk("[BAT][CHG]:limit charging current 500mA\n");
	}else if(true==g_padMic_On){
		disableAICL();
		limitSmb346chg500();
		enableAICL();	
		printk("[BAT][CHG]:InPad onCall limit cur500mA\n");

	}else if( 2==g_thermal_limit )
	{
		disableAICL();
		limitSmb346chg700();
		enableAICL();
		printk("[BAT][CHG]:limit charging current 500mA\n");

	}else if(1 == g_thermal_limit){
		disableAICL();
		limitSmb346chg900();
		enableAICL();
		printk("[BAT][CHG]:limit charging current 900mA\n");
	}else{

		if(LOW_CURRENT_CHARGER_TYPE==gpCharger->type
				||ILLEGAL_CHARGER_TYPE==gpCharger->type)
		{   
			disableAICL();
			limitSmb346chg500();
			enableAICL();
			printk("[BAT][CHG][LowIllegal]: limit chgCur,  darw 500\n");

		}else if(HIGH_CURRENT_CHARGER_TYPE==gpCharger->type)
		{
			//Eason: AICL work around +++
			if(true==g_AICLlimit)
			{
				disableAICL();
				limitSmb346chg900();
				enableAICL();
				printk("[BAT][CHG][AICL]: g_AICLlimit = true\n");
			}
			//Eason: AICL work around ---
			else{
				disableAICL();
				limitSmb346chg1200();
				enableAICL();
				printk("[BAT][CHG][AC]: dont limit chgCur, use default setting\n");
			}
		}
	}
  }
#endif//#ifndef ASUS_FACTORY_BUILD    
}
//Eason : when thermal too hot, limit charging current ---

//Eason: AICL work around +++
void setChgDrawACTypeCurrent_withCheckAICL(void)
{
#ifndef ASUS_FACTORY_BUILD   

	if(NORMAL_CURRENT_CHARGER_TYPE==gpCharger->type)
	{
		setChgLimitThermalRuleDrawCurrent();
	}else if( (3==g_thermal_limit)||(true==g_audio_limit) )
	{
		disableAICL();
		limitSmb346chg500();
		enableAICL();
		printk("[BAT][CHG]:limit charging current 500mA\n");
	}else if(true==g_padMic_On){
		disableAICL();
		limitSmb346chg500();
		enableAICL();
		printk("[BAT][CHG]:InPad onCall limit cur500mA\n");

	}else if( 2 == g_thermal_limit){
		disableAICL();
		limitSmb346chg700();
		enableAICL();
		printk("[BAT][CHG]:limit charging current 900mA\n");

	}else if( 1 == g_thermal_limit){
		disableAICL();
		limitSmb346chg900();
		enableAICL();
		printk("[BAT][CHG]:limit charging current 900mA\n");

	}else{
		if(HIGH_CURRENT_CHARGER_TYPE==gpCharger->type)
		{
			if( (time_after(AICL_success_jiffies+ADAPTER_PROTECT_DELAY , jiffies))
				&& (HIGH_CURRENT_CHARGER_TYPE==lastTimeCableType) 
				&& (true == g_AICLSuccess) )
			{
				disableAICL();
				limitSmb346chg900();
				enableAICL();
				g_AICLlimit = true;
				g_AICLSuccess = false;
				printk("[BAT][CHG][AICL]:AICL fail, always limit 900mA charge \n");
				
			}
			//else if( (smb346_getCapacity()>5) &&(true== g_alreadyCalFirstCap))
			//{
				//disableAICL();
				//limitSmb346chg900();
				//enableAICL();
				//g_AICLlimit = true;
				//schedule_delayed_work(&AICLWorker, 60*HZ);
				//printk("[BAT][CHG][AICL]: check AICL\n");
			//}
			else{
				disableAICL();
				limitSmb346chg1200();
				enableAICL();
				printk("[BAT][CHG][AICL]: limit charging current 1200mA\n");
			}	
		}
	}
#endif//#ifndef ASUS_FACTORY_BUILD    
}
//Eason: AICL work around ---

//Eason: A68 charging in P03 limit 900mA, when recording limit 500mA ++++
void setChgDrawPadCurrent(bool audioOn)
{
   g_padMic_On = audioOn;
   printk("[BAT][CHG]g_padMic_On:%d\n",g_padMic_On);
   
#ifndef ASUS_FACTORY_BUILD
    setChgDrawCurrent();
/*
   if( (false == audioOn)&&(false == g_thermal_limit)&&(false == g_audio_limit) )
   {
     disableAICL();
     limitSmb346chg900(); 
	enableAICL();	 
     printk("[BAT][CHG]:audio off : draw P03 900mA\n");
   }else{
     disableAICL();
     limitSmb346chg500();
	 enableAICL();
     printk("[BAT][CHG]:audio on : draw P03 500mA\n");
   }
   */
#endif//#ifndef ASUS_FACTORY_BUILD
}
//Eason: A68 charging in P03 limit 900mA, when recording limit 500mA ---

//Eason: AICL work around +++
void checkIfAICLSuccess(void)
{
#ifndef ASUS_FACTORY_BUILD  
	if ( 0==!gpio_get_value(gpCharger->mpGpio_pin[Smb346_DC_IN].gpio)){
		disableAICL();
		limitSmb346chg900();
		enableAICL();
		g_AICLlimit = true;
		g_AICLSuccess = false;
		printk("[BAT][CHG][AICL]:AICL fail, limit 900mA charge \n");
	}else{
		disableAICL();
		limitSmb346chg1200(); 
		enableAICL();
		g_AICLlimit =false;
		g_AICLSuccess = true;
		//Eason: AICL work around +++
		AICL_success_jiffies = jiffies;
		//Eason: AICL work around ---
		printk("[BAT][CHG][AICL]:AICL success, use default charge \n");
	}
#endif//#ifndef ASUS_FACTORY_BUILD	
}

static void checkAICL(struct work_struct *dat)
{
        checkIfAICLSuccess();
}
//Eason: AICL work around ---
void set_JEITA_function(void);
static void AXC_Smb346_Charger_SetCharger(AXI_Charger *apCharger , AXE_Charger_Type aeChargerType)
{
	static bool first_call = true;

	AXC_SMB346Charger *this = container_of(apCharger,
                                            AXC_SMB346Charger,
                                            msParentCharger);
	set_JEITA_function();
	if(false == this->mbInited)
		return;
/*
	if (!first_call && !this->m_is_bat_valid) {
		printk(KERN_INFO "[BAT][Chg] %s(), battery is invalid and cannot charging\n", __func__);
		aeChargerType = NO_CHARGER_TYPE;
	}
*/
	printk( "[BAT][CHG]CharegeModeSet:%d\n",aeChargerType);

//ASUS BSP Eason_Chang prevent P02 be set as illegal charger +++ 
    if(NORMAL_CURRENT_CHARGER_TYPE==this->type && ILLEGAL_CHARGER_TYPE==aeChargerType)
    {
        printk("[BAT][CHG]prevent P02 be set as illegal charger\n");
        return;
    }
//ASUS BSP Eason_Chang prevent P02 be set as illegal charger ---

//A68 set smb346 default charging setting+++
#ifndef ASUS_FACTORY_BUILD
	//Eason: when AC dont set default current. when phone Cap low can always draw 1200mA from boot to kernel+++ 
	if(HIGH_CURRENT_CHARGER_TYPE!=aeChargerType)
	{
            disableAICL();
            defaultSmb346chgSetting();
		enableAICL();
		printk("[BAT][CHG]default setting\n");
	}
	//Eason: when AC dont set default current. when phone Cap low can always draw 1200mA from boot to kernel---
		g_AICLlimit = false;
		g_chgTypeBeenSet = true;//Eason : prevent setChgDrawCurrent before get chgType
#endif//#ifndef ASUS_FACTORY_BUILD
//A68 set smb346 default charging setting---            

	switch(aeChargerType)
    {
		case NO_CHARGER_TYPE:
			set_register_or_pin_control(1);
			AXC_Smb346_Charger_SetPIN1(this,0);
#ifdef CONFIG_ENABLE_PIN2
			AXC_Smb346_Charger_SetPIN2(this,0);
#endif
			AXC_Smb346_Charger_SetChargrDisbalePin(this,1);
			this->type = aeChargerType;
 #ifdef ENABLE_WATCHING_STATUS_PIN_IN_IRQ          
            if(true == this->mpGpio_pin[Smb346_CHARGING_STATUS].irq_enabled){
                disable_irq_wake(this->mpGpio_pin[Smb346_CHARGING_STATUS].irq);
                this->mpGpio_pin[Smb346_CHARGING_STATUS].irq_enabled = false;
            }    
#endif

			//cancel_delayed_work(&this->msNotifierWorker);
			if(NULL != this->mpNotifier)
				this->mpNotifier->Notify(&this->msParentCharger,this->type);
			break;

        	
		case LOW_CURRENT_CHARGER_TYPE:

			
			this->type = aeChargerType;
			//Eason: AICL work around +++
			lastTimeCableType = aeChargerType;
			//Eason: AICL work around ---
			setChgDrawCurrent();
			set_register_or_pin_control(1);

			AXC_Smb346_Charger_SetChargrDisbalePin(this,0);
#ifdef ENABLE_WATCHING_STATUS_PIN_IN_IRQ
            if(false== this->mpGpio_pin[Smb346_CHARGING_STATUS].irq_enabled){
                enable_irq_wake(this->mpGpio_pin[Smb346_CHARGING_STATUS].irq);
                this->mpGpio_pin[Smb346_CHARGING_STATUS].irq_enabled = true;
            }    
#endif
			if(NULL != this->mpNotifier)
				this->mpNotifier->Notify(&this->msParentCharger,this->type);
			//cancel_delayed_work(&this->msNotifierWorker);
			else
				schedule_delayed_work(&this->msNotifierWorker , round_jiffies_relative(TIME_FOR_NOTIFY_AFTER_PLUGIN_CABLE));
			break;
        case NORMAL_CURRENT_CHARGER_TYPE:
		case ILLEGAL_CHARGER_TYPE:	
		case HIGH_CURRENT_CHARGER_TYPE:
           //Eason : when thermal too hot, limit charging current +++ 
		this->type = aeChargerType;
		//Eason: AICL work around +++
		lastTimeCableType = aeChargerType;
		setChgDrawACTypeCurrent_withCheckAICL();
		//Eason: AICL work around ---
		set_register_or_pin_control(0);
		setHC_mode(true);
		//Eason : when thermal too hot, limit charging current --- 
		AXC_Smb346_Charger_SetChargrDisbalePin(this,0);
#ifdef ENABLE_WATCHING_STATUS_PIN_IN_IRQ   
            if(false== this->mpGpio_pin[Smb346_CHARGING_STATUS].irq_enabled){
                enable_irq_wake(this->mpGpio_pin[Smb346_CHARGING_STATUS].irq);
                this->mpGpio_pin[Smb346_CHARGING_STATUS].irq_enabled = true;
            }    
#endif

			if(NULL != this->mpNotifier)
				this->mpNotifier->Notify(&this->msParentCharger,this->type);
			//cancel_delayed_work(&this->msNotifierWorker);
			else
				schedule_delayed_work(&this->msNotifierWorker , round_jiffies_relative(TIME_FOR_NOTIFY_AFTER_PLUGIN_CABLE));
			break;
/*
		case TEMP_NO_CHARGER_TYPE:
				AXC_Smb346_Charger_SetPIN1(this,0);
#ifdef CONFIG_ENABLE_PIN2
				AXC_Smb346_Charger_SetPIN2(this,0);
#endif
				AXC_Smb346_Charger_SetChargrDisbalePin(this,1);
			break;
*/
		/*
		case STABLE_CHARGER:
			this->type = aeChargerType;
			if(true == this->msParentCharger.IsCharegrPlugin(&this->msParentCharger))
            {
				if(NULL != this->mpNotifier)
					this->mpNotifier->Notify(&this->msParentCharger,this->type);
			}
            else
				this->msParentCharger.SetCharger(&this->msParentCharger, NO_CHARGER_TYPE);
		    break;
		*/

		default:
			printk( "[BAT][CHG]Wrong ChargerMode:%d\n",aeChargerType);
			break;
	}

	if (first_call) {
		first_call = false;
	}

    	power_supply_changed(&usb_psy);
    	power_supply_changed(&main_psy);

}

void AcUsbPowerSupplyChange(void)
{
        power_supply_changed(&usb_psy);
    	power_supply_changed(&main_psy);
        printk("[BAT][Chg]:Ac Usb PowerSupply Change\n");
}

static void AXC_Smb346_Charger_SetBatConfig(AXI_Charger *apCharger , bool is_bat_valid)
{
//	AXC_SMB346Charger *this = container_of(apCharger,
//                                                AXC_SMB346Charger,
//                                                msParentCharger);
	if (is_bat_valid) {
		printk(KERN_INFO "[BAT][CHG]%s, bat is valid\n", __func__);
	}

	//this->m_is_bat_valid = is_bat_valid;

	return;
}

static bool AXC_Smb346_Charger_IsCharegrPlugin(AXI_Charger *apCharger)
{
	AXC_SMB346Charger *this = container_of(apCharger,
                                                AXC_SMB346Charger,
                                                msParentCharger);

#ifdef STAND_ALONE_WITHOUT_USB_DRIVER
	//Should be configured by usb driver...
	return (!AXC_Smb346_Charger_GetGPIO(this->mnChargePlugInPin));
#else
	return (this->type != NO_CHARGER_TYPE);
#endif
}

//Eason judge smb346 full +++
bool smb346_IsFull(void)
{
    int reg3D_value;
    int reg37_value;	
    int ChgCycleTerminated;
    //At least one charging cycle has terminated since Charging first enabled
    int TermChgCycleHitStatus;//Termination Charging Current Hit status


    reg3D_value = smb346_read_table(26);//S3D[5] 
    ChgCycleTerminated = reg3D_value & 32;
    
    reg37_value = smb346_read_table(20);//S37[0]
    TermChgCycleHitStatus = reg37_value & 1;


    if(32==ChgCycleTerminated && 1==TermChgCycleHitStatus)
    {	
        pr_debug("[BAT][smb346]:IsFull:true\n");
    	return true;
    }else{
        pr_debug("[BAT][smb346]:IsFull:false\n");
    	return false;
    }
}
EXPORT_SYMBOL(smb346_IsFull);

//Eason judge smb346 full ---
//frank +++ 
int smb346_get_charging_state(void)
{
	int reg3D_value;
	int PreChargingStatus;
	int FastChargingStatus;
	int TaperChargingStatus;
	int online;
	online = !gpio_get_value(gGpio_pin[Smb346_DC_IN].gpio);
	pr_debug("[BAT][Chg][smb346]online = %d \n",online);
	reg3D_value = smb346_read_table(26);//S3D[2:1] 
	pr_debug("[BAT][Chg][smb346] smb346_read_table 0x3D = %d \n",reg3D_value);
	PreChargingStatus = reg3D_value & 6;
	FastChargingStatus = reg3D_value & 6;
	TaperChargingStatus = reg3D_value & 6;

	if(2==PreChargingStatus)
	{
		pr_debug("[BAT][smb346]:Pre charging\n");
		return PreChargingStatus;
	}else if(4==FastChargingStatus){
		pr_debug("[BAT][smb346]:Fast charging\n");
		return FastChargingStatus;
	}else if(6==TaperChargingStatus){
		pr_debug("[BAT][smb346]:Taper charging\n");
		return TaperChargingStatus;
	}else{
		return 0;
	}
}
EXPORT_SYMBOL(smb346_get_charging_state);
//frank ---

static bool AXC_Smb346_Charger_IsCharging(AXI_Charger *apCharger)
{
    int reg3D_value;
    int PreChargingStatus;
    int FastChargingStatus;
    int TaperChargingStatus;

    reg3D_value = smb346_read_table(26);//S3D[2:1] 
    PreChargingStatus = reg3D_value & 2;
    FastChargingStatus = reg3D_value & 4;
    TaperChargingStatus = reg3D_value & 6;

    if(2==PreChargingStatus)
    {
    	pr_debug("[BAT][CHG][SMB]:Pre charging\n");
    	return true;
    }else if(4==FastChargingStatus){
    	pr_debug("[BAT][CHG][SMB]:Fast charging\n");
    	return true;
    }else if(6==TaperChargingStatus){
    	pr_debug("[BAT][CHG][SMB]:Taper charging\n");
    	return true;
    }else{
    	return false;
    }
}
/*
static bool AXC_Smb346_Charger_Test(void *apTestObject)
{
	return true;
}
*/

#if defined(ASUS_CHARGING_MODE) && !defined(ASUS_FACTORY_BUILD) //ASUS_BSP Eason_Chang 1120 porting +++
static void ChgModeIfCableInSetAsUsbDefault(struct AXI_Charger *apCharger, AXI_ChargerStateChangeNotifier *apNotifier
                                                ,AXE_Charger_Type chargerType)
{
        AXC_SMB346Charger *this = container_of(apCharger,
                                            AXC_SMB346Charger,
                                            msParentCharger);
        int online;

        if( 1==g_CHG_mode ){

            online = !gpio_get_value(this->mpGpio_pin[Smb346_DC_IN].gpio);
            printk("[BAT][CHG][SMB]If cableIn:%d,%d\n",chargerType,online);
 
            if( (1==online) && (NOTDEFINE_TYPE== chargerType) )
            {
                this->type = NORMAL_CURRENT_CHARGER_TYPE;
                printk("[BAT][CHG][SMB]If cableIn set Pad default\n");
            
                if(NULL != notify_charger_in_out_func_ptr){
    
                     (*notify_charger_in_out_func_ptr) (online);
                     printk("[BAT][CHG][SMB]If cableIn notify online\n");
    
                }else{
    
                     printk("[BAT][CHG][SMB]Nobody registed..\n");
                }
                
            }
        }
 
}
#endif//ASUS_CHARGING_MODE//ASUS_BSP Eason_Chang 1120 porting ---

static void AXC_SMB346Charger_RegisterChargerStateChanged(struct AXI_Charger *apCharger, AXI_ChargerStateChangeNotifier *apNotifier
                                                            ,AXE_Charger_Type chargerType)
{
    AXC_SMB346Charger *this = container_of(apCharger,
                                            AXC_SMB346Charger,
                                            msParentCharger);
#if defined(ASUS_CHARGING_MODE) && !defined(ASUS_FACTORY_BUILD) //ASUS_BSP Eason_Chang 1120 porting +++
    if( 1==g_CHG_mode ){
            ChgModeIfCableInSetAsUsbDefault(apCharger, apNotifier, chargerType);
    }
#endif//ASUS_CHARGING_MODE//ASUS_BSP Eason_Chang 1120 porting ---

    this->mpNotifier = apNotifier;
}

static void AXC_SMB346Charger_DeregisterChargerStateChanged(struct AXI_Charger *apCharger,AXI_ChargerStateChangeNotifier *apNotifier)
{
    AXC_SMB346Charger *this = container_of(apCharger,
                                            AXC_SMB346Charger,
                                            msParentCharger);

	if(this->mpNotifier == apNotifier)
		this->mpNotifier = NULL;
}

void AXC_SMB346Charger_Binding(AXI_Charger *apCharger,int anType)
{
    AXC_SMB346Charger *this = container_of(apCharger,
                                            AXC_SMB346Charger,
                                            msParentCharger);

    this->msParentCharger.Init = AXC_Smb346_Charger_Init;
    this->msParentCharger.DeInit = AXC_Smb346_Charger_DeInit;
    this->msParentCharger.GetType = AXC_Smb346_Charger_GetType;
    this->msParentCharger.SetType = AXC_Smb346_Charger_SetType;
    this->msParentCharger.GetChargerStatus = AXC_Smb346_Charger_GetChargerStatus;
    this->msParentCharger.SetCharger = AXC_Smb346_Charger_SetCharger;
    this->msParentCharger.EnableCharging = EnableCharging;
    this->msParentCharger.SetBatConfig = AXC_Smb346_Charger_SetBatConfig;
    this->msParentCharger.IsCharegrPlugin = AXC_Smb346_Charger_IsCharegrPlugin;
    this->msParentCharger.IsCharging = AXC_Smb346_Charger_IsCharging;
    this->msParentCharger.RegisterChargerStateChanged= AXC_SMB346Charger_RegisterChargerStateChanged;	
    this->msParentCharger.DeregisterChargerStateChanged= AXC_SMB346Charger_DeregisterChargerStateChanged;	
    this->msParentCharger.SetType(apCharger,anType);

    //this->mbInited = false;
#ifdef CHARGER_SELF_TEST_ENABLE
    this->msParentSelfTest.Test = AXC_Smb346_Charger_Test;
#endif
}

/*
Implement Interface for USB Driver
*/

/*
static unsigned GetChargerStatus(void)
{
	static AXI_Charger *lpCharger = NULL;

	if(NULL == lpCharger)
    {
		AXC_ChargerFactory_GetCharger(E_SMB346_CHARGER_TYPE ,&lpCharger);
		lpCharger->Init(lpCharger);
	}

	return (unsigned) lpCharger->GetChargerStatus(lpCharger);
}

static void SetCharegerUSBMode(void)
{
	static AXI_Charger *lpCharger = NULL;

	if(NULL == lpCharger)
    {
		AXC_ChargerFactory_GetCharger(E_SMB346_CHARGER_TYPE ,&lpCharger);
		lpCharger->Init(lpCharger);
	}

	printk( "[BAT][Chg]SetCharegerUSBMode\n");
	lpCharger->SetCharger(lpCharger,LOW_CURRENT_CHARGER_TYPE);
}

static void SetCharegerACMode(void)
{
	static AXI_Charger *lpCharger = NULL;

	if(NULL == lpCharger)
    {
		AXC_ChargerFactory_GetCharger(E_SMB346_CHARGER_TYPE ,&lpCharger);
		lpCharger->Init(lpCharger);
	}

	printk( "[BAT][Chg]SetCharegerACMode\n");
	lpCharger->SetCharger(lpCharger,HIGH_CURRENT_CHARGER_TYPE);
}

static void SetCharegerNoPluginMode(void)
{
	static AXI_Charger *lpCharger = NULL;

	if(NULL == lpCharger)
    {
		AXC_ChargerFactory_GetCharger(E_SMB346_CHARGER_TYPE ,&lpCharger);
		lpCharger->Init(lpCharger);
	}

    printk( "[BAT][Chg]SetCharegerNoPluginMode\n");
	lpCharger->SetCharger(lpCharger,NO_CHARGER_TYPE);
}
*/

//ASUS BSP Eason_Chang smb346 +++
/*
static ssize_t charger_read_proc(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	static AXI_Charger *lpCharger = NULL;

	if(NULL == lpCharger)
    {
		AXC_ChargerFactory_GetCharger(E_SMB346_CHARGER_TYPE ,&lpCharger);
		lpCharger->Init(lpCharger);
	}

	return sprintf(page, "%d\n", lpCharger->GetChargerStatus(lpCharger));
}

static ssize_t charger_write_proc(struct file *filp, const char __user *buff, 
	unsigned long len, void *data)
{
	int val;

	char messages[256];

	if (len > 256) {
		len = 256;
	}

	if (copy_from_user(messages, buff, len)) {
		return -EFAULT;
	}
	
	val = (int)simple_strtol(messages, NULL, 10);

    {
	    static AXI_Charger *lpCharger = NULL;

	    if(NULL == lpCharger)
        {
		    AXC_ChargerFactory_GetCharger(E_SMB346_CHARGER_TYPE ,&lpCharger);
		    lpCharger->Init(lpCharger);
	    }

	    lpCharger->SetCharger(lpCharger,val);
    }
	//UpdateBatteryLifePercentage(gpGauge, val);
	
	return len;
}

static void create_charger_proc_file(void)
{
	struct proc_dir_entry *charger_proc_file = create_proc_entry("driver/asus_chg", 0644, NULL);

	if (charger_proc_file) {
		charger_proc_file->read_proc = charger_read_proc;
		charger_proc_file->write_proc = charger_write_proc;
	}
    else {
		printk( "[BAT][CHG]proc file create failed!\n");
    }

	return;
}
*/
//ASUS BSP Eason_Chang smb346 ---

int registerChargerInOutNotificaition(void (*callback)(int))
{
    printk("[BAT][CHG][SMB]%s\n",__FUNCTION__);
    
//ASUS_BSP Eason_Chang 1120 porting +++
#if 0//ASUS_BSP Eason_Chang 1120 porting
    if(g_A60K_hwID >=A66_HW_ID_ER2){

        notify_charger_in_out_func_ptr = callback;   
        
    }else{
    
       pm8921_charger_register_vbus_sn(callback); 
       
    }
#endif//ASUS_BSP Eason_Chang 1120 porting
       notify_charger_in_out_func_ptr = callback;  
//ASUS_BSP Eason_Chang 1120 porting ---    

    return 0;
}
//Eason : support OTG mode+++
int registerChargerI2CReadyNotificaition(void (*callback)(void))
{
	printk("[BAT][CHG][SMB]%s\n",__FUNCTION__);
	notify_charger_i2c_ready_func_ptr = callback;
	return 0;
}
//Eason : support OTG mode---
#ifdef CONFIG_EEPROM_PADSTATION
static int smb346_microp_event_handler(
	struct notifier_block *this,
	unsigned long event,
	void *ptr)
{
    if(gpCharger == NULL){

        return NOTIFY_DONE;
    }

	switch (event) {
	case P01_ADD:
            gpCharger->msParentCharger.SetCharger(&gpCharger->msParentCharger,NORMAL_CURRENT_CHARGER_TYPE);           
		break;	
	case P01_REMOVE: // means P01 removed
        gpCharger->msParentCharger.SetCharger(&gpCharger->msParentCharger, NO_CHARGER_TYPE);
		break;
	case P01_BATTERY_POWER_BAD: // P01 battery low
		break;
	case P01_AC_IN:
		break;
	case P01_USB_IN:
		break;
	case P01_AC_USB_OUT:
		break;
	case DOCK_INIT_READY:
		break;
	case DOCK_PLUG_OUT:
		break;
	case DOCK_EXT_POWER_PLUG_IN: // means dock charging
		break;
	case DOCK_EXT_POWER_PLUG_OUT:	// means dock discharging
		break;		
	case DOCK_BATTERY_POWER_BAD:
		break;
	default:
             break;
	}

	return NOTIFY_DONE;
}
#endif /* CONFIG_EEPROM_PADSTATION */


#ifdef CONFIG_EEPROM_PADSTATION
static struct notifier_block smb346_microp_notifier = {
        .notifier_call = smb346_microp_event_handler,
        .priority = BATTERY_MP_NOTIFY,
};
#endif /* CONFIG_EEPROM_PADSTATION */




//Hank: set higher hot temperature limit when CPU on+++
void setSmb346HigherHotTempLimit(void)
{
	uint8_t i2cdata[32]={0};
	i2cdata[0] = 51;//HLCT:10; HLHT:65; SLCT:15; SLHT:55;
	smb346_write_enable();
	smb346_write_reg(SMB346_HW_SW_LIMIT_CELL_TEMP,i2cdata);	
	printk("[BAT][CHG][SMB]Higher Hot Temperature Limit\n");	
	
}
//Hank: set higher hot temperature limit when CPU on---


//Hank: set default hot temperature limit when CPU off+++
#ifdef ASUS_A86_PROJECT
void setSmb346DefaultHotTempLimit(void)
{
	uint8_t i2cdata[32]={0};
	i2cdata[0] = 17;//HLCT:10; HLHT:55; SLCT:15; SLHT:45;
	smb346_write_enable();
	smb346_write_reg(SMB346_HW_SW_LIMIT_CELL_TEMP,i2cdata);	
	printk("[BAT][CHG][SMB]Default Hot Temperature Limit\n");	
	
}
#endif
//frank_tao; add i2c stress test support+++

#ifdef CONFIG_I2C_STRESS_TEST
static int TestSmb346ChargerReadWrite(struct i2c_client *client)
{

	int status;
	uint8_t i2cdata[32]={0};
	i2c_log_in_test_case("[BAT][CHG][SMB][Test]Test Smb346Charger +++\n");
	 
     	status=smb346_read_reg(SMB346_CHG_CUR,i2cdata);
	if (status< 0) {
		i2c_log_in_test_case("[BAT][CHG][SMB][Test]Test Smb346Charger ---\n");
		return I2C_TEST_SMB346_FAIL;
	}
	
	status = smb346_write_reg(SMB346_CHG_CUR, i2cdata);
	if (status < 0) {
		i2c_log_in_test_case("[BAT][CHG][SMB][Test]Test Smb346Charger ---\n");
		return I2C_TEST_SMB346_FAIL;
	}

	i2c_log_in_test_case("[BAT][CHG][SMB][Test]Test Smb346Charger ---\n");
	return I2C_TEST_PASS;
};

static struct i2c_test_case_info gChargerTestCaseInfo[] =
{
	__I2C_STRESS_TEST_CASE_ATTR(TestSmb346ChargerReadWrite),
};
#endif
//frank_tao; add i2c stress test support---
void set_JEITA_function(void)
{
	int status;
	uint8_t i2cdata[32]={0};
	i2cdata[0] = 0;
	smb346_write_enable();

	//0x00h bit [7:5] ->100  chg curr 900mA
	status=smb346_read_reg(SMB346_CHG_CUR,i2cdata);
	i2cdata[0] &= 0x9f;
	i2cdata[0] |= 0x80;
	status=smb346_write_reg(SMB346_CHG_CUR,i2cdata);

	//0x03h bit [5:0] -> 101010 float voltage 4.34V
	status=smb346_read_reg(SMB346_FLOAT_VOLTAGE,i2cdata);
	i2cdata[0] &= 0xea;
	i2cdata[0] |= 0x2a;
	status=smb346_write_reg(SMB346_FLOAT_VOLTAGE,i2cdata);

	//0~10C 0x0Ah bit[7:6] -> 01
	status=smb346_read_reg(SMB346_TLIM_THERM_CONTROL,i2cdata);
	i2cdata[0] &= 0x7F;
	i2cdata[0] |= 0x40;
	status=smb346_write_reg(SMB346_TLIM_THERM_CONTROL,i2cdata);

	if(status > 0 ){
		status=smb346_read_reg(SMB346_TLIM_THERM_CONTROL,i2cdata);
		printk("[BAT][Chg][smb346] SMB346_TLIM_THERM_CONTROL %d \n",i2cdata[0]);
	}
}
static int smb346_i2c_probe(struct i2c_client *client, const struct i2c_device_id *devid)
{
	int err;
	//struct resource *res;

    	struct smb346_info *info;
	


    	g_smb346_info=info = kzalloc(sizeof(struct smb346_info), GFP_KERNEL);
    	g_smb346_slave_addr=client->addr;
	info->i2c_client = client;
	i2c_set_clientdata(client, info);
	

       printk("[BAT][CHG][SMB]%s+++\n",__FUNCTION__);

#ifdef CONFIG_EEPROM_PADSTATION
       register_microp_notifier(&smb346_microp_notifier);
#endif /* CONFIG_EEPROM_PADSTATION */


    err = power_supply_register(&client->dev, &usb_psy);
    if (err < 0) {
        printk("[BAT][CHG][SMB]power_supply_register usb failed rc = %d\n", err);
    }
    
    err= power_supply_register(&client->dev, &main_psy);
    if (err < 0) {
        printk("[BAT][CHG][SMB]power_supply_register ac failed rc = %d\n", err);
    }

    //Eason: set default Float Voltage +++
    setSmb346FloatVoltageDefault();
    //Eason: set default Float Voltage ---



   //Hank: set higher hot temperature limit when CPU on+++
    //setSmb346HigherHotTempLimit();
   //Hank: set higher hot temperature limit when CPU on---	

   //Eason: AICL work around +++
   INIT_DELAYED_WORK(&AICLWorker,checkAICL); 
  //Eason: AICL work around ---

   //Eason : support OTG mode+++
    if(NULL != notify_charger_i2c_ready_func_ptr){

         (*notify_charger_i2c_ready_func_ptr) ();

    }else{

         printk("Charger i2c ready, Nobody registed..\n");
    }
//Eason : support OTG mode---
	


	

		AXC_ChargerFactory_GetCharger(E_SMB346_CHARGER_TYPE ,&lpcharger);
		lpcharger->Init(lpcharger);
		//lpcharger->EnableCharging(lpcharger,true);
// frank_tao porting: check padmode when bootup because microp probe more earlier  ++++
#ifdef CONFIG_EEPROM_PADSTATION
	if( 1 == AX_MicroP_IsP01Connected())
	{
		printk("[BAT][Chg][smb346] smb346_i2c_probe SetCharger NORMAL_CURRENT_CHARGER_TYPE\n");
		gpCharger->msParentCharger.SetCharger(&gpCharger->msParentCharger,NORMAL_CURRENT_CHARGER_TYPE);
	}
#endif
// frank_tao porting: check padmode when bootup because microp probe more earlier  ----
//frank_tao; add i2c stress test support+++
#ifdef CONFIG_I2C_STRESS_TEST

	i2c_add_test_case(client, "Smb346Charger",ARRAY_AND_SIZE(gChargerTestCaseInfo));

#endif
//frank_tao; add i2c stress test support---


	setUSBin_MAX_1200mA();

	
	
    printk("[BAT][CHG][SMB]%s---\n",__FUNCTION__);
    return err;
}

static int smb346_i2c_remove(struct i2c_client *client)
{


	
	power_supply_unregister(&usb_psy);
	power_supply_unregister(&main_psy);
	return 0;
}

//ASUS BSP Eason_Chang smb346 +++
/*
static struct platform_driver smb346_driver = {
	.probe	= smb346_probe,
	.remove	= smb346_remove,
	.driver	= {
		.name	= "asus_chg",
		.owner	= THIS_MODULE,
	},
};
*/
//ASUS BSP Eason_Chang smb346 ---

//ASUS BSP Eason_Chang +++
const struct i2c_device_id smb346_id[] = {
    {"smb346_i2c", 0},
    {}
};

MODULE_DEVICE_TABLE(i2c, smb346_id);

//Eason_Chang : A86 porting +++
static struct of_device_id smb346_match_table[] = {
	{ .compatible = "summit,smb346",},
	{ },
};
//Eason_Chang : A86 porting ---

static struct i2c_driver smb346_i2c_driver = {
    .driver = {
        .name = "smb346_i2c",
        .owner  = THIS_MODULE,
        .of_match_table = smb346_match_table,
     },
    .probe = smb346_i2c_probe,
    .remove = smb346_i2c_remove,
    //.suspend = smb346_suspend,
    //.resume = smb346_resume,
    .id_table = smb346_id,
};    
//ASUS BSP Eason_Chang ---

static int __init smb346_init(void)
{
    //ASUS BSP Eason_Chang +++
    printk("[BAT][CHG][SMB]init\n");
    return i2c_add_driver(&smb346_i2c_driver);
    //ASUS BSP Eason_Chang ---
}

static void __exit smb346_exit(void)
{
    //ASUS BSP Eason_Chang +++
    printk("[BAT][CHG][SMB]exit\n");
    i2c_del_driver(&smb346_i2c_driver);
    //ASUS BSP Eason_Chang ---

}
// be be later after usb...
module_init(smb346_init);
module_exit(smb346_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("ASUS battery virtual driver");
MODULE_VERSION("1.0");
MODULE_AUTHOR("Josh Liao <josh_liao@asus.com>");

//ASUS_BSP --- Josh_Liao "add asus battery driver"



#endif //#ifdef CONFIG_SMB_346_CHARGER

