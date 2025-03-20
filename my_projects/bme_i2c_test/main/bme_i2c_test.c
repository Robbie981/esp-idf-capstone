#include <stdio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <string.h>
#include <bme68x_lib.h>
#include <esp_log.h>

void bme68x_test(void *pvParameters) {
    bme68x_lib_t sensor;

    bme68x_lib_init(&sensor, NULL, BME68X_I2C_INTF);

    /* Set temperature, pressure and humidity oversampling (NONE to 16X)
    Higher oversampling means more data points are averaged per reading, increases measurement time */
    bme68x_lib_set_tph(&sensor, BME68X_OS_8X, BME68X_OS_8X, BME68X_OS_8X);

    /* IIR filter coefficient, ONLY for temp and pressure
    Higher filter coefficient means less sensitive to changes, but responseds slow to changes */
    bme68x_lib_set_filter(&sensor, BME68X_FILTER_SIZE_7);

    /* Set heater profile (°C, ms), for burning VOCs */
    bme68x_lib_set_heater_prof_for(&sensor, 300, 100);

    /* Set ambient temperature for VOC measurements (use room temperature) */
    bme68x_lib_set_ambient_temp(&sensor, 25);

    while (1) {
        bme68x_lib_set_op_mode(&sensor, BME68X_FORCED_MODE);

        // Wait for measurement completion
        vTaskDelay(pdMS_TO_TICKS(1000));

        // Fetch and get data
        if (bme68x_lib_fetch_data(&sensor) > 0) {
            bme68x_data_t data;
            bme68x_lib_get_data(&sensor, &data);
            printf("BME680 Sensor: %.2f °C, %.2f %%, %.2f hPa, %.2f Ohm\n",
                   data.temperature, data.humidity, data.pressure / 100.0, data.gas_resistance);
        }
    }
}

void app_main() {
    xTaskCreate(bme68x_test, "bme68x_test", 4096, NULL, 5, NULL);
}
