// Copyright (c) 2019-2020, XMOS Ltd, All rights reserved

/* FreeRTOS headers */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

/* Library headers */
#include "soc.h"
#include "rtos_support.h"

/* BSP/bitstream headers */
#include "bitstream_devices.h"
#include "gpio_driver.h"

/* App headers */
#include "app_conf.h"
#include "audio_pipeline.h"

static QueueHandle_t gpio_event_q;
static TaskHandle_t gpio_handler_task;
static TimerHandle_t volume_up_timer;
static TimerHandle_t volume_down_timer;

GPIO_ISR_CALLBACK_FUNCTION( gpio_dev_callback, device, source_id )
{
    BaseType_t xYieldRequired = pdFALSE;

    xTaskNotifyFromISR( gpio_handler_task, source_id, eSetBits, &xYieldRequired );

    return xYieldRequired;
}

static void volume_up( void )
{
    BaseType_t gain = 0;
    gain = audiopipeline_get_stage1_gain();
    if( gain != 0xFFFFFFFF )
    {
        gain++;
    }
    audiopipeline_set_stage1_gain( gain );
}

static void volume_down( void )
{
    BaseType_t gain = 0;
    gain = audiopipeline_get_stage1_gain();
    if( gain > 0 )
    {
        gain--;
    }
    audiopipeline_set_stage1_gain( gain );
}

void vVolumeUpCallback( TimerHandle_t pxTimer )
{
    volume_up();
}

void vVolumeDownCallback( TimerHandle_t pxTimer )
{
    volume_down();
}

void gpio_ctrl_t0(void *arg)
{
    soc_peripheral_t dev = arg;
    uint32_t mabs_buttons;
    uint32_t buttonA, buttonB, buttonC, buttonD;
    uint32_t status;
    BaseType_t gain = 0;
    BaseType_t saved_gain = 0;

    volume_up_timer = xTimerCreate(
                            "vol_up",
                            pdMS_TO_TICKS(appconfGPIO_VOLUME_RAPID_FIRE_MS),
                            pdTRUE,
                            NULL,
                            vVolumeUpCallback );

    volume_down_timer = xTimerCreate(
                            "vol_down",
                            pdMS_TO_TICKS(appconfGPIO_VOLUME_RAPID_FIRE_MS),
                            pdTRUE,
                            NULL,
                            vVolumeDownCallback);

    /* Initialize LED outputs */
    gpio_init(dev, gpio_4C);

    /* Initialize button inputs */
    gpio_init(dev, gpio_4D);	// Buttons on 0 and 1

    /* Enable interrupts on buttons */
    gpio_irq_setup_callback(dev, gpio_4D, gpio_dev_callback);
    gpio_irq_enable(dev, gpio_4D);

    for (;;) {
        xTaskNotifyWait(
                0x00000000UL,    /* Don't clear notification bits on entry */
                0xFFFFFFFFUL,    /* Reset full notification value on exit */
                &status,         /* Pass out notification value into status */
                portMAX_DELAY ); /* Wait indefinitely until next notification */

        mabs_buttons = gpio_read( dev, status );
        buttonA = ( mabs_buttons >> 0 ) & 0x01;
        buttonB = ( mabs_buttons >> 1 ) & 0x01;

        /* Turn on LEDS based on buttons */
        gpio_write_pin(dev, gpio_4C, 0, buttonA);
        gpio_write_pin(dev, gpio_4C, 1, buttonA);
        gpio_write_pin(dev, gpio_4C, 2, buttonB);
        gpio_write_pin(dev, gpio_4C, 3, buttonB);

        /* Adjust volume based on LEDs */
        if( buttonA == 0 )   /* Up */
        {
            xTimerStart( volume_up_timer, 0 );
            volume_up();
        }
        else
        {
            xTimerStop( volume_up_timer, 0 );
        }

        if( buttonB == 0 )   /* Down */
        {
            xTimerStart( volume_down_timer, 0 );
            volume_down();
        }
        else
        {
            xTimerStop( volume_down_timer, 0 );
        }
    }
}

void gpio_ctrl_create( UBaseType_t priority )
{
    soc_peripheral_t dev;

    gpio_event_q = xQueueCreate(2, sizeof(void *));

    dev = gpio_driver_init(
            BITSTREAM_GPIO_DEVICE_A,        /* Initializing GPIO device A */
            NULL,                           /* No app data */
            0);                             /* This device's interrupts should happen on core 0 */

    xTaskCreate(gpio_ctrl_t0, "t0_gpio_ctrl", portTASK_STACK_DEPTH(gpio_ctrl_t0), dev, priority, &gpio_handler_task);
}
