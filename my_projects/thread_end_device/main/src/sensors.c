#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sensors.h"
#include "misc.h"
#include "esp_log.h"

#define ADC_CHANNEL  ADC_CHANNEL_0
#define ADC_ATTEN_DB ADC_ATTEN_DB_12
#define ADC_UNIT_ID ADC_UNIT_1

TaskHandle_t ADCTaskHandle = NULL;

static void adc_task(void *arg)
{
    int adc_raw_value, adc_cali_value;
    
    /*                        ADC INIT                          */

    adc_oneshot_unit_handle_t handle;
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_ID,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(handle, ADC_CHANNEL, &config)); // ADC1_CH0
    
    /*                         ADC CALIBRATION                           */

    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_ID,
        .chan = ADC_CHANNEL,
        .atten = ADC_ATTEN_DB,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle));

    /*          READ RAW ADC INPUT AND CONVERT TO MILIVOLTS             */
    while(1)
    {
        ESP_ERROR_CHECK(adc_oneshot_read(handle, ADC_CHANNEL, &adc_raw_value));
        adc_cali_raw_to_voltage(cali_handle, adc_raw_value, &adc_cali_value);
        // printf("%d", adc_cali_value);
        // printf("\n");

        /* TODO: Do something with adc_cali_value
            - Collect many vallues over a time period
            - Calculate PM2.5 
            - Store in queue?
        */

        vTaskDelay(pdMS_TO_TICKS(500));
    }
    adc_oneshot_del_unit(handle);
    adc_cali_delete_scheme_curve_fitting(cali_handle);
    vTaskDelete(NULL);
}

void launch_adc_process(void)
{
    xTaskCreate(adc_task, "ADC Task", 8192, xTaskGetCurrentTaskHandle(), 3, NULL);
}

// FUNCTION TO BE IMPLEMENTED: retrieve PM2.5 value


