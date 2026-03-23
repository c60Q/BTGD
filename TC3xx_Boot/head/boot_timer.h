/**
 * @file Timer.h
 * @author AEP_SW
 * @brief function description
 * @version 0.1
 * @date 2025-05-03
 *
 * @copyright Copyright (c) 2024 COMPAL. Inc. All Rights Reserved.
 *
 */

#ifndef TIMER_H_
#define TIMER_H_

//===================Const Variable Definition===================
#define Time_1ms  12498u

typedef struct
{
    uint64 totalSeconds;
    uint64 days;
    uint64 hours;
    uint64 minutes;
    uint64 seconds;
} systemTime;

typedef enum
{
    //Period Tasks
    TaskScheduler = 0U,
    LedTask,
    UartTask,
    AdcTask,
    CanTask,
    LinTask,
    MqttTask,
    //One Time Tasks
    StateTransitionDelay,
    EndofTask
} TaskList;

//===================Public Function Declare=====================
extern systemTime getTime(void); //STM Module: get system in second
extern void Timer_Init(uint16 time_value); //Initial GPT Module: Setting Timer ISR Interval
//Enable/Disable GPT Timer ISR
extern void enable_T3_isr(void);
extern void disable_T3_isr(void);
extern void Timer_Task(void);
extern void Timer_Init_Task(void);
extern void Timer_Enable_Task(TaskList EnTask, uint16 Period, uint8 OPMode);
extern void Timer_Disable_Task(TaskList DisTask);

#endif /* TIMER_H_ */
