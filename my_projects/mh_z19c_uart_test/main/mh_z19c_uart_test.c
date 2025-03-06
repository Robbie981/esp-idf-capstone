#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"

static const int RX_BUF_SIZE = 1024;

#define SENSOR_RX_PIN GPIO_NUM_4  // MH-Z19C RX (ESP32 TX)
#define SENSOR_TX_PIN GPIO_NUM_5   // MH-Z19C TX (ESP32 RX)
#define UART_PORT UART_NUM_1
#define MHZ19C_BUFFER_SIZE 9

static const char *TAG = "MH-Z19C";

static const uint8_t mhz19c_read_co2_cmd[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79}; // MH-Z19C read CO2 concentration command
//static const uint8_t mhz19c_set_cali_cmd[9] = {0xFF, 0x01, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79}; // MH-Z19C turn on/off self-calibration function

static void init_uart(void)
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
static bool mhz19c_read_co2_concentration(void)
{
    // Send CO2 request
    int txBytes = uart_write_bytes(UART_PORT, mhz19c_read_co2_cmd, sizeof(mhz19c_read_co2_cmd));
    if (txBytes != sizeof(mhz19c_read_co2_cmd))
    {
        ESP_LOGE(TAG, "Failed to write CO2 request");
        return false;
    }
    ESP_LOGI(TAG, "Sent CO2 request, wrote %d bytes", txBytes);

    // Read and parse response
    uint8_t response[MHZ19C_BUFFER_SIZE];
    int rxBytes = uart_read_bytes(UART_PORT, response, sizeof(response), pdMS_TO_TICKS(200));
    if (rxBytes <= 0)
    {
        ESP_LOGE(TAG, "Failed to read UART buffer or timeout occurred");
        return false;
    }

    ESP_LOG_BUFFER_HEX(TAG, response, sizeof(response));

    if (mhz19c_calculate_checksum(response) == response[8])
    {
        uint8_t high_byte = response[2];
        uint8_t low_byte = response[3];
        int co2 = (high_byte << 8) | low_byte;

        // Log CO2 data
        // ESP_LOGI(TAG, "CO2 High Byte: 0x%02X (%d)", high_byte, high_byte);
        // ESP_LOGI(TAG, "CO2 Low Byte: 0x%02X (%d)", low_byte, low_byte);
        ESP_LOGI(TAG, "CO2 Concentration: %d ppm", co2);
    }
    else
    {
        ESP_LOGE(TAG, "Checksum error");
        return false;
    }

    // Clear UART buffer after every read
    uart_flush(UART_PORT);

    return true;
}

static void uart_task(void *arg)
{
    while (1)
    {
        if (!mhz19c_read_co2_concentration()) //returns false if error occured
        {
            ESP_LOGW(TAG, "Failed to read CO2 concentration");
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    init_uart();
    xTaskCreate(uart_task, "uart_task", 2048, NULL, 5, NULL);
}
