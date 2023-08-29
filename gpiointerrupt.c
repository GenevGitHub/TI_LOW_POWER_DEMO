/*
 * Copyright (c) 2015-2019, Texas Instruments Incorporated
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * *  Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * *  Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * *  Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *  ======== gpiointerrupt.c ========
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Driver Header files */
#include <ti/drivers/GPIO.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26XX.h>
#include <ti/drivers/PIN.h>
#include <ti/drivers/pin/PINCC26XX.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/BIOS.h>
#include <ti/drivers/dpl/HwiP.h>
#include <ti/drivers/UART.h>
#include <xdc/runtime/Error.h>

/* Example/Board Header files */
#include "Board.h"
#include <ti/devices/DeviceFamily.h>
#include DeviceFamily_constructPath(inc/hw_prcm.h)
#include DeviceFamily_constructPath(driverlib/sys_ctrl.h)

#include <ti/devices/cc26x0r2/driverlib/cpu.h>

Task_Struct systemTask;
Char myTaskStack[512];
/* Semaphore used to gate for shutdown */
Semaphore_Struct shutdownSem;

//Power Manager Notification
Power_NotifyObj powerNotifyObj;

//UART Peripheral Objects
UART_Handle UART_handle;
UART_Params UART_params;

// Application Power_NotifyFxn function prototype
static int powerTransitionNotifyFxn(unsigned int eventType, uintptr_t eventArg,uintptr_t clientArg);

/*UART Callback*/
static void RxControl(uint8_t rxByte);
static void UART_Read(uint8_t *message, uint8_t size);
static void rxCallback(UART_Handle UART_handle, void *rxBuf, size_t size);
static void txCallback(UART_Handle UART_handle, void *rxBuf, size_t size);
static uint8_t receivedByte;

//WakeUp Pin Configuration
PIN_Config ExternalWakeUpPin[] = {
    Board_PIN_BUTTON0 | PIN_INPUT_EN | PIN_PULLUP | PINCC26XX_WAKEUP_NEGEDGE,
    PIN_TERMINATE
};



uint8_t active;
void Power(int16_t status)
{
   switch(status)
   {
   case Power_ACTIVE:
       active = 1;
       break;

   case Power_ENTERING_SLEEP:
       active = 2;
       break;

   case Power_EXITING_SLEEP:
       active = 3;
       break;

   case Power_ENTERING_SHUTDOWN:
       active = 4;
       break;

   case Power_CHANGING_PERF_LEVEL:
       active = 5;
       break;
   }
}


uint8_t isPowerOff = 0;
uint_fast16_t powerTransition;
static int powerTransitionNotifyFxn(unsigned int eventType, uintptr_t eventArg,uintptr_t clientArg)
{
    if(eventType == PowerCC26XX_ENTERING_SHUTDOWN)
    {
        isPowerOff = 1;
        powerTransition = Power_getTransitionState();
        Power(powerTransition);
        return (Power_NOTIFYDONE);
    }
    else if (eventType == PowerCC26XX_ENTERING_STANDBY)
    {
        isPowerOff = 2;
        powerTransition = Power_getTransitionState();
        Power(powerTransition);
        return (Power_NOTIFYDONE);
    }
    else if (eventType == PowerCC26XX_AWAKE_STANDBY)
    {
        return (Power_NOTIFYDONE);
    }
    else if (eventType == PowerCC26XX_AWAKE_STANDBY_LATE)
    {
        return (Power_NOTIFYDONE);

    }
    return (Power_NOTIFYERROR);
}


/*
 *  ======== gpioButtonFxn0 ========
 *  Callback function for the GPIO interrupt on Board_GPIO_BUTTON1 / Board_GPIO_BUTTON0.
 *  This may not be used for all boards.
 */
uint8_t led_toggle = 0;
uint8_t Board_GPIO_LED_state = 1;
uint8_t whichButton = 0xFF;
uint8_t pinConfig = 0xFF;
/*Use external interrupt to wake up the device*/
void gpioButtonFxn0(uint_least8_t index)
{
    switch(index)
    {
       case Board_GPIO_BUTTON1:
           whichButton = 1;
           Board_GPIO_LED_state = 0;
           Semaphore_post(Semaphore_handle(&shutdownSem)); //trigger the event
           break;
       default:
           break;

    }
}



uint8_t dllm = 0;
int_fast16_t success;

void waitMS(uint32_t time)
{
    CPUdelay(12000*time);
}

static void systemFxn(UArg a0, UArg a1)
{
    GPIO_write(Board_GPIO_LED1, Board_GPIO_LED_state);

    Semaphore_pend(Semaphore_handle(&shutdownSem), BIOS_WAIT_FOREVER);
    if(Board_GPIO_LED_state == 1)
    {
        GPIO_write(Board_GPIO_LED1, Board_GPIO_LED_state);
    }
    else if (Board_GPIO_LED_state == 0)
    {
        GPIO_write(Board_GPIO_LED1, 0);
    }
    /* Configure DIO for wake up from shutdown */
    pinConfig = PINCC26XX_setWakeup(ExternalWakeUpPin); /*The system resets (REBOOTS) automatically*/
    Power_shutdown(0, 0);
    while(1)
    {
    }
}

uint8_t fuck = 0;
static void RxControl(uint8_t rxByte)
{
   if(rxByte == 0x01)
   {
         fuck ++;
         GPIO_toggle(Board_GPIO_LED0);

   }
}

static void UART_Read(uint8_t *message, uint8_t size)
{
    UART_read(UART_handle,message,size);
}

static void rxCallback(UART_Handle UART_handle, void *rxBuf, size_t size)
{
    //Do Anything u want
    RxControl(((uint8_t *)rxBuf)[0]);
    UART_Read(&receivedByte,1);
}

static void txCallback(UART_Handle UART_handle, void *rxBuf, size_t size)
{
  //Do Anything u want
}

static void ButtonControl();

uint8_t counter = 0;

static Clock_Handle timer;
static Clock_Params clkParams;
static uint32_t clockTicks;
static Error_Block eb;

uint8_t btvalue = 0xFF;
bool buttonPressed = false;
bool bootSuccess = false;
uint8_t bootProcess = 0xFF;

uint8_t messageid;
uint8_t pressed;
uint8_t logicLevel = 0xFF;
uint8_t fail = 0;
void motorcontrol_singleButtonCB(uint8_t messageID)
{
   // for debugging only
    if (messageID == 0x01)
    {

        messageid = messageID;
        if(PIN_getInputValue(Board_PIN_BUTTON0) == 0)
        {
            bootSuccess = true;
        }
        else
        {
            //button_Counter_Stop();
            bootSuccess = false;
            bootProcess = 0xFF;
            fail = 1;
        }
    }
    else if (messageID == 0x02)
    {
        messageid = messageID;
    }
    else if (messageID == 0x03)
    {
        messageid = messageID;
    }
    else if (messageID == 0x04)
    {
        messageid = messageID;
    }
    else if(messageID == 0x05)
    {
        messageid = messageID;
    }
    else if (messageID == 0x00)
    {
        messageid = messageID;
    }
}

uint32_t timerPeriod;
uint8_t risingEdgeCount = 0;    // make this static if not debugging
uint8_t fallingEdgeCount = 0;   // make this static if not debugging
static uint8_t buttonBehavior = 0x00; //It's a default waiting state!!!!

void button_Counter_init()
{
    clockTicks = 1500 * (1000 / Clock_tickPeriod) -1 ;
    timer = Clock_create(ButtonControl,clockTicks,&clkParams, &eb);
    Clock_Params_init(&clkParams);
    clkParams.period = 0;
    clkParams.startFlag = FALSE;
    clkParams.arg = (UArg)0x0000;
    //Clock_setTimeout(timer, clockTicks);
    //Clock_setPeriod(timer, clockTicks);
}

void button_Counter_SetPeriod(uint32_t clockTimeout)
{
   uint32_t ticks = clockTimeout*(1000/Clock_tickPeriod)-1;
   Clock_setTimeout(timer,ticks);
}

void button_Counter_Start()
{
  Clock_start(timer);
}

void button_Counter_Stop()
{
  Clock_stop(timer);
}

uint8_t seconds = 0;
uint8_t time_elapsed = 0;
uint8_t buttonEvent = 0x00;
/*Timer IRQ Handler*/
static void ButtonControl()
{
    button_Counter_Stop();
    if(risingEdgeCount == 0 && fallingEdgeCount == 1)
    {
        buttonEvent = 0x01;
    }
    else if (risingEdgeCount == 1 && fallingEdgeCount == 1)
    {
        buttonEvent = 0x02;                             //callback -> lightControl_change();
    }
    // TOGGLE BLE Advertising
    else if (risingEdgeCount == 1 && fallingEdgeCount == 2)
    {
        buttonEvent = 0x03;
    }
    // CHANGE SPEED MODE
    else if (risingEdgeCount == 2 && fallingEdgeCount == 2)
    {
        buttonEvent = 0x04;
    }
    // TOGGLE UNITS METRIC/IMPERIAL
    else if (risingEdgeCount == 3 && fallingEdgeCount == 3)
    {
        buttonEvent = 0x05;
    }
    // DO NOTHING
    else
    {
        buttonEvent = 0x00;
    }
    timerPeriod = 1500; //SINGLE_BUTTON_TIMER_OV_TIME_LONG
    risingEdgeCount = 0;   // reset to 0
    fallingEdgeCount = 0;   // reset to 0
    buttonBehavior = 0x00; //SINGLE_BUTTON_WAITING_STATE
    motorcontrol_singleButtonCB(buttonEvent);
}

void singleButton_processButtonEvt(uint8_t logicLevel)
{
    if(logicLevel == 0)
    {
        fallingEdgeCount++;
    }
    if(fallingEdgeCount == 0)    // Ignores the rising edge after a long press
    {
        risingEdgeCount = 0;
        return;
    }
    if(logicLevel == 1)
    {
        risingEdgeCount++;
    }
    if(buttonBehavior == 0x00) //SINGLE_BUTTON_WAITING_STATE
    {
        buttonBehavior = 0x01; //SINGLE_BUTTON_EXECUTING_STATE
        timerPeriod = 1500; //SINGLE_BUTTON_TIMER_OV_TIME_LONG
        button_Counter_SetPeriod(timerPeriod);
        button_Counter_Start();
    }
    else if(buttonBehavior == 0x01)
    {
        timerPeriod = 500; //SINGLE_BUTTON_TIMER_OV_TIME_SHORT
        button_Counter_Stop();
        button_Counter_SetPeriod(timerPeriod);
        button_Counter_Start();
    }

}

uint32_t resetSource = 0xFFFF;
PIN_Handle pulse;
PIN_State  pulseState;

/*Activate GPIO Pin to inject a rising / falling edge pulse to the slave device*/
PIN_Config pulseGenerator[] = {
      Board_DIO15 | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
      PIN_TERMINATE
};

PIN_Handle buttonVerify;
PIN_State buttonState;
PIN_Config button[] = {
      Board_PIN_BUTTON0 | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
      PIN_TERMINATE
};


void *mainThread(void *arg0)
{

    GPIO_setConfig(Board_GPIO_LED0, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);
    resetSource =  SysCtrlResetSourceGet();
    /*Boot check starts*/
    button_Counter_init(); /*Equivalent to UDHAL_BootChecking_Start() */
    buttonVerify = PIN_open(&buttonState, button); /*Equivalent to UDHAL_BootChecking_Start() */
    /*Start booting process here!!!!*/
    if(resetSource == RSTSRC_WAKEUP_FROM_SHUTDOWN)
    {
        while(bootSuccess == false)
        {
            logicLevel = PIN_getInputValue(Board_PIN_BUTTON0);
            if(logicLevel == 0 && bootProcess == 0xFF)
            {
                 singleButton_processButtonEvt(logicLevel);
                 bootProcess = 0x01;
                 if(bootSuccess == true)
                 {
                     break;
                 }
            }
            if(fail == 1)
            {
                /* Configure DIO for wake up from shutdown */
                pinConfig = PINCC26XX_setWakeup(ExternalWakeUpPin); /*The system resets (REBOOTS) automatically*/
                Power_shutdown(0, 0);
                while(1);
            }
        }
    }
    else if (resetSource == RSTSRC_PWR_ON)
    {
         bootProcess = 0x02;
    }
    else if (resetSource == RSTSRC_PIN_RESET)
    {
         bootProcess = 0x03;
    }

    /*Start System Control !!!*/
    GPIO_setConfig(Board_GPIO_LED1, GPIO_CFG_OUT_STD | GPIO_CFG_OUT_LOW);

    GPIO_write(Board_GPIO_LED1, 0);   // test LED

    GPIO_setConfig(Board_GPIO_BUTTON1, GPIO_CFG_IN_PU | GPIO_CFG_IN_INT_FALLING); //Falling Edge Trigger EXTI

    /* install Button callback */
    GPIO_setCallback(Board_GPIO_BUTTON1, gpioButtonFxn0);

    /* Enable interrupts */
    GPIO_enableInt(Board_GPIO_BUTTON1);

    /*Create RTOS TASK!!*/
    Task_Params taskParams;
    Task_Params_init(&taskParams);
    taskParams.stack     = myTaskStack;
    taskParams.stackSize = sizeof(myTaskStack);
    Task_construct(&systemTask,systemFxn,&taskParams,NULL);

    /* Configure shutdown semaphore. */
    Semaphore_Params semParams;
    Semaphore_Params_init(&semParams);
    semParams.mode = Semaphore_Mode_BINARY;
    Semaphore_construct(&shutdownSem, 0, &semParams);

    uint8_t event_List = PowerCC26XX_ENTERING_SHUTDOWN | PowerCC26XX_ENTERING_STANDBY | PowerCC26XX_AWAKE_STANDBY | PowerCC26XX_AWAKE_STANDBY_LATE;
    Power_registerNotify(&powerNotifyObj, event_List, powerTransitionNotifyFxn, NULL);

    /*Activate UART*/
    UART_init();
    UART_Params_init(&UART_params);
    UART_params.baudRate      = 115200;
    UART_params.writeMode     = UART_MODE_CALLBACK;
    UART_params.writeDataMode = UART_DATA_BINARY;
    UART_params.writeCallback = txCallback;
    UART_params.readMode      = UART_MODE_CALLBACK;
    UART_params.readTimeout   = 1;
    UART_params.readDataMode  = UART_DATA_BINARY;
    UART_params.readCallback  = rxCallback;
    UART_handle = UART_open(Board_UART0, &UART_params);
    UART_Read(&receivedByte,1);
    return 0;
    //Open UART Signal Now!

}

