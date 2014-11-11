#include <linux/kernel.h>
#include <linux/mutex.h>
#include "axi_cap_filter.h"
#include "axc_cap_filter_a66.h"

/* BAT_LIFE - different battery life definition */
#define BAT_LIFE_TO_SHUTDOWN 0
#define BAT_LIFE_ONE 1
#define BAT_LIFE_FIVE 5
#define BAT_LIFE_TWO  2
#define BAT_LIFE_FULL 100
#define BAT_LIFE_BEFORE_FULL 99
#define BAT_LIFE_BEFORE_FULL_ONE_UNIT 94

#define SECOND_OF_HOUR	3600

/* g_bat_life_after_dot - remain battery life after dot, used for more accuracy */
static int g_bat_life_after_dot = 0;

//Eason add fasterLeverage judge+++ 
static int g_do_fasterLeverage_count = 0; 
//Eason add fasterLeverage judge---  
//Eason: choose Capacity type SWGauge/BMS +++
extern int g_CapType;
//Eason: choose Capacity type SWGauge/BMS ---
//Eason: remember last BMS Cap to filter+++
extern int gDiff_BMS;
//Eason: remember last BMS Cap to filter---

//ASUS_BSP Lenter +++
#if defined(ASUS_CHARGING_MODE) && !defined(ASUS_FACTORY_BUILD)
extern char g_CHG_mode;
#endif
//ASUS_BSP Lenter ---

static int g_discharge_after_dot = 0;
extern int gCurr_ASUSswgauge;
#define OCV_PER_SPEEDUP_UPDATE_14P	14
#define OCV_PER_SPEEDUP_UPDATE_35P	35
#define WLAN_HOT_SPOT_CUR 350
#define DEFAULT_SUSPEND_FAST 30


//Eason: more accuracy for discharge after dot+++
int formula_of_discharge(int maxMah,int batCapMah, int interval)
{
	return  ( (maxMah*100*100/batCapMah)*interval/SECOND_OF_HOUR/100 );
}

int formula_of_discharge_dot(int maxMah,int batCapMah, int interval)
{
	return  ( 10*(maxMah*100*100/batCapMah)*interval/SECOND_OF_HOUR/100 )%10;
}

int discharge_dot_need_plus(void)
{
	int total_can_be_plus = 0;
	
	if( (g_discharge_after_dot/10)>=1)
	{
		total_can_be_plus += (g_discharge_after_dot/10);
		g_discharge_after_dot = g_discharge_after_dot%10;
	}

	return total_can_be_plus;
}
//Eason: more accuracy for discharge after dot---

/* eval_bat_life_when_discharging - Evaluate conservative battery life when discharging.
 * Use (maximum consuming current * update interval) as one factor.
 */
static int eval_bat_life_when_discharging(
	int nowCap, int lastCap, int maxMah, int interval, int batCapMah)
{
	int bat_life = 0;
	int drop_val = lastCap - nowCap;
	//Eason: more accuracy for discharge after dot+++
	int pred_discharge_after_dot = 0;  //predict discharge cap after dot 
	int fast_discharge_after_dot = 0;   //fastleverage discharger cap after dot
	//Eason: more accuracy for discharge after dot---

	pr_info( "[BAT][Fil]%s(), drop_val:%d\n", __func__, drop_val);

	if (drop_val > 0) {
		int max_predict_drop_val = 0;
		int finetune_max_predict_drop_val = 0;
		int fasterLeverage_drop_val = 0;

		if (interval > 0) {
			/* if interval is more than 108sec, max_predict_drop_val will be more than 1 */
			//Eason :when  low bat Cap draw large current  +++
			//max_predict_drop_val = (maxMah*100/batCapMah)*interval/SECOND_OF_HOUR;
			max_predict_drop_val = formula_of_discharge(maxMah, batCapMah, interval);
			if(max_predict_drop_val == 0)
				pred_discharge_after_dot = formula_of_discharge_dot(maxMah, batCapMah, interval);
			else
				pred_discharge_after_dot = 0;
			if(10 != maxMah){
#ifdef ASUS_ME175KG_PROJECT
				if(nowCap <= OCV_PER_SPEEDUP_UPDATE_35P){
					fasterLeverage_drop_val = formula_of_discharge(2800, batCapMah, interval);
					fast_discharge_after_dot = formula_of_discharge_dot(2800, batCapMah, interval);
				}
#else
				if(nowCap <= OCV_PER_SPEEDUP_UPDATE_14P){
					if(gCurr_ASUSswgauge>1400){
						fasterLeverage_drop_val = formula_of_discharge(gCurr_ASUSswgauge, batCapMah, interval);
						fast_discharge_after_dot = formula_of_discharge_dot(gCurr_ASUSswgauge, batCapMah, interval);
					}
					else{
						fasterLeverage_drop_val = formula_of_discharge(1400, batCapMah, interval);
						fast_discharge_after_dot = formula_of_discharge_dot(1400, batCapMah, interval);
					}
				}
#endif
				else{
					if(gCurr_ASUSswgauge>900){
						fasterLeverage_drop_val = formula_of_discharge(gCurr_ASUSswgauge, batCapMah, interval);
						fast_discharge_after_dot = formula_of_discharge_dot(gCurr_ASUSswgauge, batCapMah, interval);
					}
					else{
						fasterLeverage_drop_val = formula_of_discharge(900, batCapMah, interval);
						fast_discharge_after_dot = formula_of_discharge_dot(900, batCapMah, interval);
					}
				}
			}
			//Eason :when  low bat Cap draw large current  ---
			//Eason:prevent in unattend mode mass drop+++
			if(10==maxMah){
				if(gCurr_ASUSswgauge > WLAN_HOT_SPOT_CUR){
					fasterLeverage_drop_val = formula_of_discharge(WLAN_HOT_SPOT_CUR, batCapMah, interval);
					fast_discharge_after_dot = formula_of_discharge_dot(WLAN_HOT_SPOT_CUR, batCapMah, interval);
				}
				else if(gCurr_ASUSswgauge < DEFAULT_SUSPEND_FAST){
					fasterLeverage_drop_val = formula_of_discharge(DEFAULT_SUSPEND_FAST, batCapMah, interval);
					fast_discharge_after_dot = formula_of_discharge_dot(DEFAULT_SUSPEND_FAST, batCapMah, interval);
				}
				else{
					fasterLeverage_drop_val = formula_of_discharge(gCurr_ASUSswgauge, batCapMah, interval);
					fast_discharge_after_dot = formula_of_discharge_dot(gCurr_ASUSswgauge, batCapMah, interval);
				}
			}
			//Eason:prevent in unattend mode mass drop---
			
			//Eason add fasterLeverage judge+++  
			if((drop_val > max_predict_drop_val) && (g_do_fasterLeverage_count < 3)){
				g_do_fasterLeverage_count++; 
			}
			else if((drop_val <= max_predict_drop_val) && (g_do_fasterLeverage_count > 0)){    
				g_do_fasterLeverage_count--;
			}

#ifdef ASUS_ME175KG_PROJECT
			if(nowCap<=OCV_PER_SPEEDUP_UPDATE_35P){
#else
			if(nowCap<=OCV_PER_SPEEDUP_UPDATE_14P){
#endif
				finetune_max_predict_drop_val = fasterLeverage_drop_val;
				//Eason: more accuracy for discharge after dot+++
				g_discharge_after_dot += fast_discharge_after_dot;
				printk("[BAT][Fil]formula:%d.%d\n",fasterLeverage_drop_val,fast_discharge_after_dot);
				finetune_max_predict_drop_val += discharge_dot_need_plus();
				//Eason: more accuracy for discharge after dot---
			}
			else if( (2<=g_do_fasterLeverage_count)&&(nowCap<10) ){
				finetune_max_predict_drop_val = fasterLeverage_drop_val;
				//Eason: more accuracy for discharge after dot+++
				g_discharge_after_dot += fast_discharge_after_dot;
				printk("[BAT][Fil]formula:%d.%d\n",fasterLeverage_drop_val,fast_discharge_after_dot);
				finetune_max_predict_drop_val += discharge_dot_need_plus();
				//Eason: more accuracy for discharge after dot---
			}
			else if(3 == g_do_fasterLeverage_count){
				finetune_max_predict_drop_val = fasterLeverage_drop_val;
				//Eason: more accuracy for discharge after dot+++
				g_discharge_after_dot += fast_discharge_after_dot;
				printk("[BAT][Fil]formula:%d.%d\n",fasterLeverage_drop_val,fast_discharge_after_dot);
				finetune_max_predict_drop_val += discharge_dot_need_plus();
				//Eason: more accuracy for discharge after dot---
			}
			else{
				//Eason: more accuracy for discharge after dot+++
				g_discharge_after_dot += pred_discharge_after_dot;
				printk("[BAT][Fil]formula:%d.%d\n",max_predict_drop_val,pred_discharge_after_dot);
				if(max_predict_drop_val==0)
				max_predict_drop_val += discharge_dot_need_plus();
				//Eason: more accuracy for discharge after dot---
				if(drop_val > max_predict_drop_val)
				{
					//Eason: remember last BMS Cap to filter+++
	            		 	if(gDiff_BMS >=0)//when discharge BMS drop value >=0
            		 		{
						finetune_max_predict_drop_val = min(abs(gDiff_BMS),abs(max_predict_drop_val));		
            		 		}
					else
						finetune_max_predict_drop_val = max_predict_drop_val;
					//Eason: remember last BMS Cap to filter---
				}
				else
					finetune_max_predict_drop_val = max_predict_drop_val;
	            }

			if(finetune_max_predict_drop_val<0)
			{
				finetune_max_predict_drop_val = -finetune_max_predict_drop_val;
				printk("[BAT][Fil]Error: finetune_max_predict_drop_val overflow\n");
			}
			//Eason add fasterLeverage judge---
            
			bat_life = lastCap - min(drop_val, finetune_max_predict_drop_val);
		}
		else {
			//bat_life = lastCap - drop_val;
			bat_life = lastCap;
			pr_err( "[BAT][Fil]Error!!! %s(), interval < 0\n",
					__func__);
		}

		pr_info( "[BAT][Fil] interval:%d, drop_val:%d, max_predict_drop_val:%d, fasterLeverage_drop_val:%d, gDiff_BMS:%d, finetune_max_predict_drop_val:%d, count:%d \n",
			interval,
			drop_val,
			max_predict_drop_val,
			fasterLeverage_drop_val,
			gDiff_BMS,
			finetune_max_predict_drop_val,
			g_do_fasterLeverage_count);
	}
	else {
		bat_life = lastCap;

		if(g_do_fasterLeverage_count > 0){
			g_do_fasterLeverage_count--;
		}

		if (drop_val < 0) {
			pr_info( "[BAT][Fil] Error!!! %s(), drop val less than 0. count:%d\n", __func__,g_do_fasterLeverage_count);
		}
	}

	return bat_life;
}



/* eval_bat_life_when_charging - Evaluate conservative battery life when charging.
 * Use (maximum charging current * update interval) as one factor.
 */
static int eval_bat_life_when_charging(
	int nowCap, int lastCap, int maxMah, int interval, int batCapMah)
{
	int bat_life = 0;
	int rise_val = nowCap - lastCap; 
	int tmp_val_after_dot = 0;
	int tmp_interval = 0;
	printk("[BAT][Fil] enter eval_bat_life_when_charging!\n");

#ifdef ASUS_ME175KG_PROJECT
	maxMah *= 2;
#endif

	if (rise_val > 0) {
		unsigned long max_bat_life_rise_val = 0;
		tmp_interval = 0;
		if (interval > 0) {
			/* if interval is more than 108sec, max_predict_drop_val will be more than 1 */
			max_bat_life_rise_val = (maxMah*100/batCapMah)*interval/SECOND_OF_HOUR;
			/* to calculate the first number after the decimal dot for more accuracy.*/
			tmp_val_after_dot = ((10*maxMah*100/batCapMah)*interval/SECOND_OF_HOUR)%10;
			printk( "[BAT][Fil]%s(), tmp_val_after_dot:%d\n",
				__func__,
				tmp_val_after_dot);	
			g_bat_life_after_dot += tmp_val_after_dot;
			if ((g_bat_life_after_dot/10) >= 1) {
				printk( "[BAT][Fil]%s(), g_bat_life_after_dot:%d\n",
					__func__,
					g_bat_life_after_dot);
				max_bat_life_rise_val += (g_bat_life_after_dot/10);
				g_bat_life_after_dot = g_bat_life_after_dot%10;
			}
			bat_life = lastCap + min(rise_val, (int)max_bat_life_rise_val);
            //TO DO ...if interval is too big...will get a negative value for capacity returned...
		} else {
			bat_life = lastCap + rise_val;
			pr_err("[BAT][Fil]Error!!! %s(), interval < 0\n",
					__func__);
		}

		pr_debug( "[BAT][Fil]%s(), rise_val:%d, interval:%d, max_bat_life_rise_val:%d, bat_life:%d\n",
			__func__,
			rise_val,
			(int)interval,
			(int)max_bat_life_rise_val,
			bat_life);
	} else {
		bat_life = lastCap;
		pr_debug( "[BAT][Fil]%s(), keep the same bat_life:%d as before\n",
			__func__,
			bat_life);
		if(gCurr_ASUSswgauge>100)
		{
			tmp_interval +=interval;
			printk("[BAT][Fil] tmp_interval = %d \n",tmp_interval);
			if(tmp_interval >= 1200)
			{
				tmp_interval = 0;
				bat_life++;
			}
		}
		else
			tmp_interval = 0;
		if (rise_val < 0) {
			pr_err("[BAT][Fil] Error!!! %s(), rise val less than 0.\n", __func__);
		}
		
	}
	return bat_life;
}


/* update_bat_info_for_speical_case - Update battery info for some special cases.
 * Such as when to update battery life to 100.
 */
static int update_bat_info_for_speical_case(
	int bat_life,
	bool hasCable,
	bool isCharing,	
	bool isBatFull,
	bool isBatLow)
{
	int final_bat_life;

	if ((!isBatLow) && (bat_life <= 0)) {
		pr_info( "[BAT][Fil]%s(), bat not low, but get cap as 0, return 1\n", __func__);
		final_bat_life = BAT_LIFE_ONE;
		return final_bat_life;
	}

	if (bat_life >= BAT_LIFE_FULL) {
		if(false == isBatFull)
		//if(this->mpCharger->IsCharging(this->mpCharger))
		{
			pr_debug( "[BAT][Fil]%s(), Still in charging status, so update bat life to 99\n", __func__);
			final_bat_life = BAT_LIFE_BEFORE_FULL;
			return final_bat_life;
		} else {
			final_bat_life = BAT_LIFE_FULL;
			return final_bat_life;
		}
	} 


	if (bat_life >= BAT_LIFE_BEFORE_FULL_ONE_UNIT) {
		//Need to know if being charging or not	
		if (hasCable) {
			if(true == isBatFull){
				//if(this->mpCharger->IsCharging(this->mpCharger)) {
				pr_debug( "[BAT][Fil]%s(), keep bat life to 100\n", __func__);
				final_bat_life = BAT_LIFE_FULL;
				return final_bat_life;
			} else {
				pr_debug( "[BAT][Fil]%s(), still in charging\n", __func__);
				final_bat_life = bat_life;
				return final_bat_life;
			}
		}
	}

	final_bat_life = bat_life;

	return final_bat_life;
}

//#define BAT_LOW_LIFE_MAP_START 	10

//static int bat_low_life_map_tbl[BAT_LOW_LIFE_MAP_START + 1] = {1, 1, 1, 1, 2, 3, 4, 6, 7, 8, 10};

int AXC_Cap_Filter_A66_GetType(struct AXI_Cap_Filter *apCapFilter)
{
	AXC_Cap_Filter_A66 *this = container_of(apCapFilter, AXC_Cap_Filter_A66, parentCapFilter);
	return this->filterType;
}


int AXC_Cap_Filter_A66_FilterCapacity(struct AXI_Cap_Filter *apCapFilter, int nowCap, int lastCap, bool hasCable, bool isCharing, bool isBatFull, bool isBatLow, int maxMah, int interval)
{
	int bat_life;
	AXC_Cap_Filter_A66 *this = container_of(apCapFilter, AXC_Cap_Filter_A66, parentCapFilter);

	//Eason: choose Capacity type SWGauge/BMS +++
	if ((1==g_CapType)&&isBatLow && (nowCap <= 0) && (lastCap <= 3)) {
		printk("[BAT][Fil][BMS]%s(), bat low and cap <= 2, shutdown!! \n", __func__);
		return BAT_LIFE_TO_SHUTDOWN;
	}
	//Eason: choose Capacity type SWGauge/BMS ---

	/* the criteria to set bat life as 0 to shutdown */	
	if (isBatLow && (nowCap <= 0) && (lastCap <= 3)) {
		pr_info("[BAT][Fil]%s(), bat low and cap <= 3, shutdown!! \n", __func__);
		return BAT_LIFE_TO_SHUTDOWN;
	}

//ASUS_BSP Lenter +++
#if defined(ASUS_A600KL_PROJECT) || defined(ASUS_A500KL_PROJECT)
#if defined(ASUS_CHARGING_MODE) && !defined(ASUS_FACTORY_BUILD)
	if ((nowCap <= 0) && (lastCap <= 3) && (g_CHG_mode != 1) && (gCurr_ASUSswgauge >= 0)) {
		pr_info("[BAT][Fil]%s(), bat low and cap <= 3, shutdown!! \n", __func__);
		return BAT_LIFE_TO_SHUTDOWN;
	}
#endif
#endif
//ASUS_BSP Lenter ---

	/* battery low life remap - avoid user not to see battery low warning notification */
/*	if (nowCap <= BAT_LOW_LIFE_MAP_START) {
		bat_life = bat_low_life_map_tbl[nowCap];
		pr_info("[BAT][Fil]%s(), remap cap from %d to %d \n", __func__, nowCap, bat_life);
		return bat_life;
	}
*/
	if (hasCable) {	// has cable
		bat_life = eval_bat_life_when_charging(nowCap, lastCap, maxMah, interval, this->capMah);
	} else {	// no cable
		bat_life = eval_bat_life_when_discharging(nowCap, lastCap, maxMah, interval, this->capMah);
	}

	bat_life = update_bat_info_for_speical_case(bat_life, hasCable, isCharing, isBatFull, isBatLow);

	pr_debug("[BAT][Fil]%s(), filter cap:%d \n", __func__, bat_life);
	return bat_life;

}

extern void AXC_Cap_Filter_A66_Constructor(AXI_Cap_Filter *apCapFilter, int filterType, int capMah)
{

	AXC_Cap_Filter_A66 *this = container_of(apCapFilter, AXC_Cap_Filter_A66, parentCapFilter);
	this->capMah = capMah;
	this->filterType = filterType;
	this->parentCapFilter.getType = AXC_Cap_Filter_A66_GetType;
	this->parentCapFilter.filterCapacity = AXC_Cap_Filter_A66_FilterCapacity;


}


