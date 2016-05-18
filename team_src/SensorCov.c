/*
 * SensorCov().c
 *
 *  Created on: Oct 30, 2013
 *      Author: Nathan, edited by David
 */

#include "all.h"

extern DSPfilter A0filter;
extern DSPfilter A1filter;
extern DSPfilter A2filter;
extern DSPfilter A3filter;
extern DSPfilter A4filter;
extern DSPfilter A5filter;
extern DSPfilter A6filter;
extern DSPfilter A7filter;
extern DSPfilter B0filter;
extern DSPfilter B1filter;
extern DSPfilter B2filter;
extern DSPfilter B3filter;
extern DSPfilter B4filter;
extern DSPfilter B5filter;
extern DSPfilter B6filter;
extern DSPfilter B7filter;
extern DSPfilter GPIO19filter;
extern DSPfilter GPIO26filter;
extern clock_struct Clock_Ticks;

user_ops_struct ops_temp;
user_data_struct data_temp;

static filter throttle_filter;
SafetyVar32_t safety;

//initializing variables used in SensorCovInit
int i = 0;
int MAX = 0;
//throttle percentages
static const int BAT_THROTTLE[21] = {100, 95, 90, 85, 80, 75, 70, 65,
							   60, 55, 50, 45, 40, 35, 30, 25, 20, 15, 10, 5, 0};

//pointer float array storing the battery temperatures
float* BATT_CELL_TEMPS[48] = {&user_data.CellTemp1.F32, &user_data.CellTemp2.F32, &user_data.CellTemp3.F32,
		                     &user_data.CellTemp4.F32, &user_data.CellTemp5.F32, &user_data.CellTemp6.F32,
							 &user_data.CellTemp7.F32, &user_data.CellTemp8.F32, &user_data.CellTemp9.F32,
							 &user_data.CellTemp10.F32, &user_data.CellTemp11.F32, &user_data.CellTemp12.F32,
							 &user_data.CellTemp13.F32, &user_data.CellTemp14.F32, &user_data.CellTemp15.F32,
							 &user_data.CellTemp16.F32, &user_data.CellTemp17.F32, &user_data.CellTemp18.F32,
							 &user_data.CellTemp19.F32, &user_data.CellTemp20.F32, &user_data.CellTemp21.F32,
							 &user_data.CellTemp22.F32, &user_data.CellTemp23.F32, &user_data.CellTemp24.F32,
							 &user_data.CellTemp25.F32, &user_data.CellTemp26.F32, &user_data.CellTemp27.F32,
							 &user_data.CellTemp28.F32, &user_data.CellTemp29.F32, &user_data.CellTemp30.F32,
							 &user_data.CellTemp31.F32, &user_data.CellTemp32.F32, &user_data.CellTemp33.F32,
							 &user_data.CellTemp34.F32, &user_data.CellTemp35.F32, &user_data.CellTemp36.F32,
							 &user_data.CellTemp37.F32, &user_data.CellTemp38.F32, &user_data.CellTemp39.F32,
							 &user_data.CellTemp40.F32, &user_data.CellTemp41.F32, &user_data.CellTemp42.F32,
							 &user_data.CellTemp43.F32, &user_data.CellTemp44.F32, &user_data.CellTemp45.F32,
							 &user_data.CellTemp46.F32, &user_data.CellTemp47.F32, &user_data.CellTemp48.F32};

void SensorCov()
{
	SensorCovInit(4);
	while (sys_ops.State == STATE_SENSOR_COV)
	{
		LatchStruct();
		if (!Stack_Check()){
			SafetyVar_NewValue(&safety, 0);
			user_data.stack_limit.U32 = 1;
			user_data.driver_control_limits.U32 = user_data.stack_limit.U32 << 3;
			UpdateStruct();
			FillCANData();
		}
		else {
			SensorCovMeasure();
			UpdateStruct();
			FillCANData();
		}

	}
	SensorCovDeInit();
}

void SensorCovInit()
{
	//todo USER: SensorCovInit()
	SystemSensorInit(SENSOR_COV_STOPWATCH);

	initDSPfilter(&A5filter, 818);
	initDSPfilter(&A7filter, 818);
	EMA_Filter_Init(&throttle_filter, 1000);
}

void SensorCovMeasure()
{
#define R1 10000.0 //Before ADC, Ohms
#define R2 20000.0
#define V5 5.08
#define B 1568.583480 //Ohm
#define ADC_SCALE 4096.0

	SensorCovSystemInit();
	//initialize used variables

	//value which represents the position in the BAT_THROTTLE array which holds the calculated throttle percentage
	int THROTTLE_LOOKUP = 0;

	//calculates throttle ratio
	user_data.throttle_percent_ratio.F32 = _IQtoF(_IQdiv(_IQ(A5RESULT), _IQ(ADC_SCALE)));

	user_data.RPM.F32 = 4000;

	//*******************************************************************************************************
	//*                                           TODO                                                      *
	//*                Clean up Sensor Conversion to make it cleaner  				                        *
	//*                                                                                                     *
	//*******************************************************************************************************


	//loop looks through the battery temperature array and deterimines the maximum temperature
	MAX = -999;
	for (i = 0; i < 48; i++){
		if (*BATT_CELL_TEMPS[i] > MAX){
			user_data.max_cell_temp.F32 = *BATT_CELL_TEMPS[i];
			MAX = user_data.max_cell_temp.F32;
		}
	}

	THROTTLE_LOOKUP = (int)(user_data.max_cell_temp.F32 * 2);

	//lookup the corrosponding throttle percentage
	if (THROTTLE_LOOKUP <= 90){
		//throttle percent cap is the maximum throttle value the rider can go
		user_data.throttle_percent_cap.F32 = BAT_THROTTLE[0];
		user_data.throttle_percent_cap.F32 = _IQtoF(_IQmpy(_IQ(user_data.throttle_percent_cap.F32), _IQ(0.01)));
		user_data.battery_limit.U32 = 0;
	}

	else {
		//lookup corrosponding throttle percentage if the temperature gets too high
		if (THROTTLE_LOOKUP > 110){
			user_data.throttle_percent_cap.F32 = 0;
		}
		else {
			user_data.throttle_percent_cap.F32 = BAT_THROTTLE[THROTTLE_LOOKUP-90];
			user_data.throttle_percent_cap.F32 = _IQtoF(_IQmpy(_IQ(user_data.throttle_percent_cap.F32), _IQ(0.01)));
			user_data.battery_limit.U32 = 1;
		}
	}

    //capping the throttle output - checking to see if the trottle is enabled and that there are no CAN timeouts
	if (!user_data.timeout_limit.U32 && throttle_toggle()){
			user_data.throttle_output.F32 = user_data.throttle_percent_ratio.F32;
			user_data.throttle_lock.U32 = 1;
			if (user_data.throttle_output.F32 >= user_data.throttle_percent_cap.F32){
				#ifdef EMA_FILTER_ENABLED
				EMA_Filter_NewInput(&throttle_filter, user_data.throttle_percent_cap.F32);
				user_data.throttle_output.F32 = EMA_Filter_GetFilteredOutput(&throttle_filter);
				#else
				user_data.throttle_output.F32 = user_data.throttle_percent_cap.F32;
				#endif

			}
	}
	else {
			#ifdef EMA_FILTER_ENABLED
			EMA_Filter_NewInput(&throttle_filter, 0);
			user_data.throttle_output.F32 = EMA_Filter_GetFilteredOutput(&throttle_filter);
			#else
			user_data.throttle_output.F32 = 0;
			#endif
	}

	//unfiltered throttle value
	user_data.no_filter.F32 = user_data.throttle_percent_ratio.F32;

	//sets throttle lock to 0 if the throttle is off
	if (!throttle_toggle()){
		user_data.throttle_lock.U32 = 0;
	}

	//initializes CRC
	if (!user_data.timeout_limit.U32){
		SafetyVar_NewValue(&safety, user_data.throttle_output.U32);
	}

	data_temp.gp_button = READGPBUTTON();

	//checks to see if the stack is close to overflowing
	//If it is, then the throttle is set to 0 and the stack limit is activated

	//sending the driver control limmits information
	user_data.driver_control_limits.U32 = user_data.stack_limit.U32 << 3;
	user_data.driver_control_limits.U32 += user_data.timeout_limit.U32 << 2;
	user_data.driver_control_limits.U32 += user_data.battery_limit.U32 << 1;
	user_data.driver_control_limits.U32 += user_data.throttle_lock.U32;

	PerformSystemChecks();
}

void LatchStruct()
{
	LatchSystemStruct();
	ops_temp = user_ops;
	data_temp = user_data;
}

void UpdateStruct()
{
	SaveOpStates();
	//user_data = data_temp;
	//todo USER: UpdateStruct
	//update with node specific op changes

	//if ops is not changed outside of sensor conversion copy temp over, otherwise don't change

	//Change bit is only set by ops changes outside of SensorCov.


	DetermineOpStates();
}

void SensorCovDeInit()
{
	//todo USER: SensorCovDeInit()
	SystemSensorDeInit();
}
