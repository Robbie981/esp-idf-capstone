#pragma once

#include "bme68x_lib.h"

void adc_init(void);

/**
 * @brief Starts the ADC reading process for PM2.5 sensor
 */
void launch_pm25_test_task(void);

float get_pm25_reading(void);

/**
 * @brief Initializes the BME680 sensor
 */
void bme68x_i2c_init(void);

/**
 * @brief Starts a loop to read data from the BME680 sensor
 */
void launch_bme68x_test_task(void);

/**
 * @brief Gets all data fields from the BME680 sensor
 */
void bme68x_data_retrieve(bme68x_data_t *data);

/**
 * @brief Compute IAQ score, returns a score from 0 - 100
 */
float bme68x_get_iaq(float gas_resistance, float humidity, float temp);

void launch_bme68x_gas_refresh_task(void);

/**
 * @brief Initializes UART for the MH-Z19C sensor
 */
void mhz19c_uart_init(void);

/**
 * @brief Gets one CO2 reading from the MH-Z19C sensor in ppm, returns -1 if error
 */
int mhz19c_get_co2_concentration(void);

void mhz19c_set_self_cali(bool enable_self_cali);

/**
 * @brief Starts a loop to read data from the MH-Z19C sensor
 */
void launch_mhz19c_test_task(void);