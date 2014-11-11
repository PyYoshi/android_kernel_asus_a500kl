/*                                                                                                                                                       
        PM8921 Charge IC include file

*/
#ifndef __AXC_PM8226CHARGER_H__
#define __AXC_PM8226CHARGER_H__
#include <linux/types.h>
#include "axi_charger.h"         
#include <linux/workqueue.h>
#include <linux/param.h>
#include <linux/wakelock.h>

//#define STAND_ALONE_WITHOUT_USB_DRIVER

#define TIME_FOR_NOTIFY_AFTER_PLUGIN_CABLE (60*HZ) //5s

typedef struct AXC_PM8226Charger {
	bool mbInited;
	int mnType;
	AXE_Charger_Type type;
	unsigned int usb_current_max;
	
	AXI_ChargerStateChangeNotifier *mpNotifier;
	AXI_Charger msParentCharger;

	struct delayed_work asus_chg_work;    
	//frank_tao: set Chg Limit In Pad When Charger Reset +++
	#ifdef CONFIG_EEPROM_PADSTATION
	struct delayed_work setChgLimitInPadWhenChgResetWorker;
	#endif
	//frank_tao: set Chg Limit In Pad When Charger Reset ---
	struct wake_lock cable_in_out_wakelock;
	struct timer_list charger_in_out_timer;
	//struct delayed_work msNotifierWorker;
	//++++ frank_tao add check_cable_in_out_delay_1sec
	struct delayed_work check_cable_in_out_delay;
	//---- frank_tao add check_cable_in_out_delay_1sec
#ifdef CHARGER_SELF_TEST_ENABLE
	AXI_SelfTest msParentSelfTest; 
#endif //CHARGER_SELF_TEST_ENABLE
}AXC_PM8226Charger;

extern void AXC_PM8226Charger_Binding(AXI_Charger *apCharger,int anType);
#endif //__AXC_PM8921CHARGER_H__
