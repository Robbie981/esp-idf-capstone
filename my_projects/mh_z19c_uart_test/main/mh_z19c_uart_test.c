#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "driver/uart.h"
#include "string.h"
#include "driver/gpio.h"

static const int RX_BUF_SIZE = 1024;

#define TXD_PIN GPIO_NUM_5  // MH-Z19C TX (ESP32 RX)
#define RXD_PIN GPIO_NUM_4   // MH-Z19C RX (ESP32 TX)
#define UART_PORT UART_NUM_1

static const char *TAG = "MH-Z19C";

static const uint8_t mhz19c_read_co2_cmd[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79}; // MH-Z19C read CO2 concentration command
//static const uint8_t mhz19c_set_cali_cmd[9] = {0xFF, 0x01, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79}; // MH-Z19C turn on/off self-calibration function

static void init_uart(void)
{
    const uart_config_t uart_config = {
        .baud_rate = 9600,  // Required for MH-Z19C
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(UART_PORT, RX_BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_PORT, &uart_config);
    uart_set_pin(UART_PORT, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// Send request to mhz19c sensor
static void mhz19c_send_request(const uint8_t* request, size_t request_len)
{
    int txBytes = uart_write_bytes(UART_PORT, request, request_len);
    ESP_LOGI(TAG, "Sent request, wrote %d bytes", txBytes);
}

// Task to send data to mhz19c
static void tx_task(void *arg)
{
    static const char *TX_TASK_TAG = "TX_TASK";
    esp_log_level_set(TX_TASK_TAG, ESP_LOG_INFO);
    while (1)
    {
        mhz19c_send_request(mhz19c_read_co2_cmd, sizeof(mhz19c_read_co2_cmd));
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

// Helper function to calculate checksum for mhz19c sensor
uint8_t calculate_checksum(uint8_t *packet)
{
    uint8_t checksum = 0;

    for (int i = 1; i < 8; i++)
    {
        checksum += packet[i];  // Sum bytes 1 to 7
    }

    checksum = 0xFF - checksum;  // Subtract from 0xFF
    checksum += 1;  // Add 1

    return checksum;
}

// Task to parse data returned from mhz19c
static void rx_task(void *arg)
{
    uint8_t response[9];
    while (1)
    {
        int rxBytes = uart_read_bytes(UART_PORT, response, sizeof(response), 100);
        ESP_LOG_BUFFER_HEX(TAG, response, sizeof(response));
        // if (rxBytes == 9 && response[0] == 0xFF)  // Valid response
        // {
        //     int co2 = (response[2] << 8) | response[3];  // Combine high and low bytes
        //     ESP_LOGI(TAG, "CO2 Concentration: %d ppm", co2);
        // }
        // else
        // {
        //     ESP_LOGW(TAG, "Invalid response, read %d bytes", rxBytes);
        // }
    }
}

void app_main(void)
{
    init_uart();
    xTaskCreate(rx_task, "uart_rx_task", 2048, NULL, configMAX_PRIORITIES - 1, NULL);
    xTaskCreate(tx_task, "uart_tx_task", 2048, NULL, configMAX_PRIORITIES - 2, NULL);
}
