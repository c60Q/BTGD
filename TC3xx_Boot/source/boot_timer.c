/**
 * @file Timer.c
 * @author AEP_SW
 * @brief function description
 * @version 0.1
 * @date 2025-05-03
 *
 * @copyright Copyright (c) 2024 COMPAL. Inc. All Rights Reserved.
 *
 */

#include "Ifx_Types.h"
#include "IfxGpt12.h"
#include "IfxStm.h"
#include "IfxPort.h"
#include "boot_timer.h"

//===================Const Variable Definition===================
//Define the number of time
#define DAY_IN_SECONDS      86400
#define HOURS_IN_SECONDS    3600
#define MIN_IN_SECONDS      60
#define EXECUTE_TASK     0U
#define TASK_ENABLE      1U
#define TASK_DISABLE     0U
#define PERIOD_TASK      1U
#define ONE_TIME_TASK    0U

#define ISR_PRIORITY_TIMER_T3           254 //Define the GPT12 Timer interrupt priority, low to high 1-255, 0 is reserve


typedef void (*TaskFunction)(void);
void interruptTimer3(void);


typedef struct
{
    uint8        TaskEnable;
    uint8        OperationMode;
    const uint8  StartTime;
    uint16       TaskPeriod;
    uint16       TaskCounter;
    TaskFunction Fun;
} TimerTask;

//===================Local Function Declare======================

//===================Local Variable Definition===================
static uint16 g_counter = 0;


//===================Public Variable Definition==================

//===================Public Function Declare=====================
IFX_INTERRUPT(interruptTimer3, 0, 254); //Macro defining the Interrupt Service Routine

//===================Local Function==============================

//===================Public Function=============================
/**
 * @brief Timer 3 ISR
 *
 * @param : none
 * @return : none
 */
void interruptTimer3(void)
{
    g_counter++;
}

/**
 * @brief Get system time
 *
 * @param : none
 * @return : systemTime : system time
 */
systemTime getTime(void)
{
    systemTime g_time;

    //Get the system time (since the last reset of the microcontroller) in seconds
    g_time.totalSeconds = IfxStm_get(&MODULE_STM0) / IfxStm_getFrequency(&MODULE_STM0);

    //Calculate the number of days
    g_time.days = g_time.totalSeconds / DAY_IN_SECONDS;

    //Calculate the number of hours
    g_time.hours = (g_time.totalSeconds - (g_time.days * DAY_IN_SECONDS)) / HOURS_IN_SECONDS;

    //Calculate the number of minutes
    g_time.minutes = (g_time.totalSeconds - (g_time.days * DAY_IN_SECONDS) - (g_time.hours * HOURS_IN_SECONDS)) / MIN_IN_SECONDS;

    //Calculate the number of seconds
    g_time.seconds = (g_time.totalSeconds - (g_time.days * DAY_IN_SECONDS) - (g_time.hours * HOURS_IN_SECONDS) - (g_time.minutes * MIN_IN_SECONDS));

//    IfxStdIf_DPipe_print(&g_ascStandardInterface, "Total Seconds: %d\r\n", (int)g_time.totalSeconds);

    return g_time;
}

/**
 * @brief Initialize timer
 *
 * @param : uint16 time_value : timer period parameter
 * @return : none
 */
void Timer_Init(uint16 time_value)
{
    //Initialize the GPT12 module
    IfxGpt12_enableModule(&MODULE_GPT120);                                          //Enable the GPT12 module
    IfxGpt12_setGpt1BlockPrescaler(&MODULE_GPT120, IfxGpt12_Gpt1BlockPrescaler_4);  //Set GPT1 block prescaler

    //Initialize the Timer T3
    IfxGpt12_T3_setMode(&MODULE_GPT120, IfxGpt12_Mode_timer);                       //Set T3 to timer mode
    IfxGpt12_T3_setTimerDirection(&MODULE_GPT120, IfxGpt12_TimerDirection_down);    //Set T3 count direction
    IfxGpt12_T3_setTimerPrescaler(&MODULE_GPT120, IfxGpt12_TimerInputPrescaler_2);  //Set T3 input prescaler
    IfxGpt12_T3_setTimerValue(&MODULE_GPT120, time_value);                          //Set T3 start value

    //Initialize the Timer T2
    IfxGpt12_T2_setMode(&MODULE_GPT120, IfxGpt12_Mode_reload);                      //Set T2 to reload mode
    IfxGpt12_T2_setReloadInputMode(&MODULE_GPT120, IfxGpt12_ReloadInputMode_bothEdgesTxOTL); //Set reload trigger
    IfxGpt12_T2_setTimerValue(&MODULE_GPT120,time_value);                           //Set T2 reload value

    //Initialize the interrupt
    volatile Ifx_SRC_SRCR *src_01 = IfxGpt12_T3_getSrc(&MODULE_GPT120);             //Get the interrupt address
    IfxSrc_init(src_01, IfxSrc_Tos_cpu0, ISR_PRIORITY_TIMER_T3);          			//Initialize service request
    IfxSrc_enable(src_01);                                                          //Enable GPT12 interrupt

    IfxGpt12_T3_run(&MODULE_GPT120, IfxGpt12_TimerRun_start);                       //Start the timer
}

/**
 * @brief Enable Timer3 ISR
 *
 * @param : none
 * @return : none
 */
void enable_T3_isr(void)
{
    volatile Ifx_SRC_SRCR *src_01 = IfxGpt12_T3_getSrc(&MODULE_GPT120); //Get the interrupt address
    IfxSrc_enable(src_01);                                              //Enable GPT12 interrupt
}

/**
 * @brief Disable Timer3 ISR
 *
 * @param : none
 * @return : none
 */
void disable_T3_isr(void)
{
    volatile Ifx_SRC_SRCR *src_01 = IfxGpt12_T3_getSrc(&MODULE_GPT120); //Get the interrupt address
    IfxSrc_disable(src_01);                                             //Enable GPT12 interrupt
}


