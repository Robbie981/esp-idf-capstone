#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "sensors.h"
#include "misc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define ADC_CHANNEL  ADC_CHANNEL_0
#define ADC_ATTEN_DB ADC_ATTEN_DB_12
#define ADC_UNIT_ID ADC_UNIT_1

#define PM25_ADC_READINGS 100
#define K_SCATTER 1.0      // Scaling factor for light intensity conversion
#define A_PARTICLE 1000.0  // Empirical coefficient for particle count
#define B_EXPONENT 1.5     // Empirical exponent for non-linearity
#define PARTICLE_RADIUS 1.0e-6  // in meters (1 µm)
#define PARTICLE_DENSITY 1.65e3  // in kg/m³ (density of dust particles)

#define SENSOR_RX_PIN GPIO_NUM_4  // MH-Z19C RX (ESP32 TX)
#define SENSOR_TX_PIN GPIO_NUM_5   // MH-Z19C TX (ESP32 RX)
#define UART_PORT UART_NUM_1
#define MHZ19C_BUFFER_SIZE 9

static SemaphoreHandle_t bme_sensor_mutex;
static SemaphoreHandle_t gas_ceil_mutex;
static adc_oneshot_unit_handle_t adc_handle = NULL;
static adc_cali_handle_t adc_cali_handle = NULL;
static bme68x_lib_t sensor;
static const uint8_t mhz19c_read_co2_cmd[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79}; // MH-Z19C read CO2 concentration command

/***************************************************************************
 *                         PARTICULATE SENSOR
 ***************************************************************************/

void adc_init(void)
{   
    // ADC INIT
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_ID,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ADC_CHANNEL, &config)); // ADC1_CH0
    
    //ADC CALIBRATION
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_ID,
        .chan = ADC_CHANNEL,
        .atten = ADC_ATTEN_DB,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle));
}

float get_pm25_reading(void)
{
    int adc_raw_value, adc_cali_value;
    double adc_cal_values[PM25_ADC_READINGS];

    // Fill the buffer with ADC readings
    for (int i = 0; i < PM25_ADC_READINGS; i++)
    {
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ADC_CHANNEL, &adc_raw_value));
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_raw_value, &adc_cali_value));

        adc_cal_values[i] = adc_cali_value;
        vTaskDelay(pdMS_TO_TICKS(150)); // Delay between readings
    }

    for (int i = 0; i < PM25_ADC_READINGS; i++)
    {
        printf("%.f ", adc_cal_values[i]); // Print with 3 decimal places
        if ((i + 1) % 10 == 0) // Newline every 10 readings
        {
            printf("\n");
        }
    }
    printf("\n\n"); // Final newline
    

    //  // Process the readings to calculate PM2.5 concentration
    //  double total_mass_concentration = 0.0;

    //  for (int i = 0; i < PM25_ADC_READINGS; i++)
    //  {
    //      double intensity = K_SCATTER * adc_cal_values_volts[i];
    //      double particle_count = A_PARTICLE * pow(intensity, B_EXPONENT);
    //      double particle_volume = (4.0 / 3.0) * M_PI * pow(PARTICLE_RADIUS, 3);
    //      double mass_per_particle = particle_volume * PARTICLE_DENSITY * 1e9; // Convert kg to µg
    //      total_mass_concentration += particle_count * mass_per_particle;
    //  }

    //  return (float) (total_mass_concentration / PM25_ADC_READINGS); // Return average PM2.5 concentration (µg/m³)
    return 0;
}

static void pm25_test_task(void *arg)
{
    while (1)
    {
        get_pm25_reading();
        //ESP_LOGI(ADC_DEBUG_TAG, "PM2.5 Concentration: %.2f µg/m³", get_pm25_reading());
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void launch_pm25_test_task(void)
{
    xTaskCreate(pm25_test_task, "pm25_task", 8192, xTaskGetCurrentTaskHandle(), 3, NULL);
}

// FUNCTION TO BE IMPLEMENTED: retrieve PM2.5 value

/***************************************************************************
 *                         BME680 SENSOR
 ***************************************************************************/

void bme68x_i2c_init(void) 
{
    bme_sensor_mutex = xSemaphoreCreateMutex();
    gas_ceil_mutex = xSemaphoreCreateMutex();
    
    bme68x_lib_init(&sensor, NULL, BME68X_I2C_INTF);

    /* Set temperature, pressure and humidity oversampling (NONE to 16X)
    Higher oversampling means more data points are averaged per reading, increases measurement time */
    bme68x_lib_set_tph(&sensor, BME68X_OS_16X, BME68X_OS_NONE, BME68X_OS_16X);

    /* IIR filter coefficient, ONLY for temp and pressure
    Higher filter coefficient means less sensitive to changes, but responseds slow to changes */
    bme68x_lib_set_filter(&sensor, BME68X_FILTER_SIZE_63);

    /* Set heater profile (°C, ms), for burning VOCs */
    bme68x_lib_set_heater_prof_for(&sensor, 300, 200);

    /* Set ambient temperature for VOC measurements (use room temperature) */
    bme68x_lib_set_ambient_temp(&sensor, 25);
}

void bme68x_data_retrieve(bme68x_data_t *data)
{
    // Attempt to take the mutex before accessing the sensor data
    if (xSemaphoreTake(bme_sensor_mutex, portMAX_DELAY) == pdTRUE) {
        
        // Acquire the sensor in forced mode
        bme68x_lib_set_op_mode(&sensor, BME68X_FORCED_MODE);

        // Fetch and get the data from the sensor
        if (bme68x_lib_fetch_data(&sensor) > 0)
        {
            bme68x_lib_get_data(&sensor, data);
        }
        else
        {
            printf("Failed to retrieve BME680 data\n");
        }
        xSemaphoreGive(bme_sensor_mutex);
    }
    else
    {
        printf("Failed to acquire mutex for sensor\n");
    }
}

static float water_sat_density(float temp) 
{
    return (6.112 * 100 * exp((17.62 * temp) / (243.12 + temp))) / (461.52 * (temp + 273.15));
}

float bme68x_get_iaq(float gas_resistance, float humidity, float temp) 
{
    static float gas_ceil = 0; // Static variable to store the gas ceiling
    static int burn_in_counter = 0;
    float iaq = -1;

    // Calculate the maximum absolute water density
    float rho_max = water_sat_density(temp);
    float hum_abs = humidity * 10 * rho_max;

    // Apply humidity compensation to gas resistance
    float comp_gas = gas_resistance * exp(0.03 * hum_abs);

    // Calculate the IAQ score as a percentage
    if (burn_in_counter < 25) 
    {
        burn_in_counter++;
    }
    else 
    {
        // Update the gas ceiling to the new highest value if needed
        if (comp_gas > gas_ceil) 
        {
            if (xSemaphoreTake(gas_ceil_mutex, portMAX_DELAY) == pdTRUE)
            {
                gas_ceil = comp_gas;
                xSemaphoreGive(gas_ceil_mutex);
            }
        }
        iaq = fminf(powf(comp_gas / gas_ceil, 2), 1.0) * 100.0;
    }

    return iaq;
}

static void bme68x_test_task(void *arg)
{
    while (1)
    {
        bme68x_data_t data;
        bme68x_data_retrieve(&data);
        
        printf("BME680 Sensor: %.2f °C, %.2f %%, %.2f Ohm, %.2f iaq\n",
                data.temperature, data.humidity, data.gas_resistance, 
                bme68x_get_iaq(data.gas_resistance, data.humidity, data.temperature));

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void launch_bme68x_test_task(void)
{
    xTaskCreate(bme68x_test_task, "bme68x_test_task", 8192, xTaskGetCurrentTaskHandle(), 3, NULL);
}

static void bme68x_gas_refresh_task(void *arg)
{
    while (1)
    {
        bme68x_data_t data;
        bme68x_data_retrieve(&data);
        bme68x_get_iaq(data.gas_resistance, data.humidity, data.temperature);

        vTaskDelay(pdMS_TO_TICKS(1500));
    }
}

void launch_bme68x_gas_refresh_task(void)
{
    xTaskCreate(bme68x_gas_refresh_task, "bme68x_gas_refresh_task", 8192, xTaskGetCurrentTaskHandle(), 3, NULL);
}

/***************************************************************************
 *                           CO2 SENSOR
 ***************************************************************************/

void mhz19c_uart_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 9600,  // For MH-Z19C
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(UART_PORT, 1024 * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, SENSOR_RX_PIN, SENSOR_TX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// Helper function to calculate checksum for mhz19c sensor
static uint8_t mhz19c_calculate_checksum(uint8_t *packet)
{
    uint8_t checksum = 0;

    for (int i = 1; i < 8; i++)
    {
        checksum += packet[i];  // Sum bytes 1 to 7
    }

    return (0xFF - checksum) + 1 ;  // Subtract from 0xFF then add 1
}

// Get one CO2 reading from MH-Z19C sensor
int mhz19c_get_co2_concentration(void)
{
    // Send CO2 request
    int txBytes = uart_write_bytes(UART_PORT, mhz19c_read_co2_cmd, sizeof(mhz19c_read_co2_cmd));
    if (txBytes != sizeof(mhz19c_read_co2_cmd))
    {
        ESP_LOGE(CO2_DEBUG_TAG, "Failed to write CO2 request");
        return -1;
    }
    //ESP_LOGI(CO2_DEBUG_TAG, "Sent CO2 request, wrote %d bytes", txBytes);

    // Read and parse response
    uint8_t response[MHZ19C_BUFFER_SIZE];
    int rxBytes = uart_read_bytes(UART_PORT, response, sizeof(response), pdMS_TO_TICKS(200));
    uart_flush(UART_PORT);
    if (rxBytes <= 0)
    {
        ESP_LOGE(CO2_DEBUG_TAG, "Failed to read UART buffer or timeout occurred");
        return -1;
    }

    //ESP_LOG_BUFFER_HEX(CO2_DEBUG_TAG, response, sizeof(response));

    if (mhz19c_calculate_checksum(response) == response[8])
    {
        uint8_t high_byte = response[2];
        uint8_t low_byte = response[3];
        int co2_concentration = (high_byte << 8) | low_byte;

        // Log CO2 data
        // ESP_LOGI(CO2_DEBUG_TAG, "CO2 High Byte: 0x%02X (%d)", high_byte, high_byte);
        // ESP_LOGI(CO2_DEBUG_TAG, "CO2 Low Byte: 0x%02X (%d)", low_byte, low_byte);
        // ESP_LOGI(CO2_DEBUG_TAG, "CO2 Concentration: %d ppm", co2_concentration);
        return co2_concentration;
    }
    else
    {
        ESP_LOGE(CO2_DEBUG_TAG, "Checksum error");
        return -1;
    }    
}

static void mhz19c_test_task(void *arg)
{
    while (1)
    {
        int co2_concentration = mhz19c_get_co2_concentration();
        printf("CO2 Concentration: %d ppm\n", co2_concentration);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void launch_mhz19c_test_task(void)
{
    xTaskCreate(mhz19c_test_task, "mhz19c_test_task", 2048, xTaskGetCurrentTaskHandle(), 3, NULL);
}

/***************************************************************************
 *                              END OF FILE
 ***************************************************************************/