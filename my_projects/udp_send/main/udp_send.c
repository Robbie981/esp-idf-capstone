#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_openthread.h"
#include "esp_openthread_cli.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hal/uart_types.h"
#include "nvs_flash.h"
#include "openthread/cli.h"
#include "openthread/instance.h"
#include "openthread/logging.h"
#include "openthread/tasklet.h"
#include "openthread/thread.h"
#include "openthread/udp.h"

/*************DEFINES START*************/
#define MY_THREAD_PANID 0x2222 // 16 bit
#define MY_THREAD_EXT_PANID {0x22, 0x22, 0x00, 0x00, 0xab, 0xce, 0x11, 0x22} // 64 bit
#define MY_THREAD_NETWORK_NAME "my_ot_network"
#define MY_MESH_LOCAL_PREFIX {0xfd, 0x00, 0x00, 0x00, 0xfb, 0x01, 0x00, 0x01} // 64 bits, first 8 bits always 0xfd 
#define MY_THREAD_NETWORK_KEY {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff} // ot default key
/*************DEFINES END*************/

/*************GLOBALS START*************/
static esp_netif_t *openthread_netif = NULL;
/*************GLOBALS END*************/

static esp_netif_t *init_openthread_netif(const esp_openthread_platform_config_t *config)
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_OPENTHREAD();
    esp_netif_t *netif = esp_netif_new(&cfg);
    assert(netif != NULL);
    ESP_ERROR_CHECK(esp_netif_attach(netif, esp_openthread_netif_glue_init(config)));

    return netif;
}

static void config_thread_dataset(void)
{
    otInstance *myOtInstance = esp_openthread_get_instance();
    otOperationalDataset aDataset;
    
    // overwrite current active dataset 
    ESP_ERROR_CHECK(otDatasetGetActive(myOtInstance, &aDataset));
    memset(&aDataset, 0, sizeof(otOperationalDataset));

    aDataset.mActiveTimestamp.mSeconds = 0;
    aDataset.mComponents.mIsActiveTimestampPresent = true;

    aDataset.mChannel = 11;
    aDataset.mComponents.mIsChannelPresent = true;

    aDataset.mChannelMask = (otChannelMask)0x7fff800; // enables all radio channels (11–26)
    aDataset.mComponents.mIsChannelMaskPresent = true;

    aDataset.mPanId = (otPanId)MY_THREAD_PANID;
    aDataset.mComponents.mIsPanIdPresent = true;

    uint8_t extPanId[OT_EXT_PAN_ID_SIZE] = MY_THREAD_EXT_PANID;
    memcpy(aDataset.mExtendedPanId.m8, extPanId, sizeof(aDataset.mExtendedPanId));
    aDataset.mComponents.mIsExtendedPanIdPresent = true;

    uint8_t key[OT_NETWORK_KEY_SIZE] = MY_THREAD_NETWORK_KEY;
    memcpy(aDataset.mNetworkKey.m8, key, sizeof(aDataset.mNetworkKey));
    aDataset.mComponents.mIsNetworkKeyPresent = true;

    static char aNetworkName[] = MY_THREAD_NETWORK_NAME;
    size_t length = strlen(aNetworkName);
    assert(length <= OT_NETWORK_NAME_MAX_SIZE);
    memcpy(aDataset.mNetworkName.m8, aNetworkName, length);
    aDataset.mComponents.mIsNetworkNamePresent = true;

    uint8_t meshLocalPrefix[OT_MESH_LOCAL_PREFIX_SIZE] = MY_MESH_LOCAL_PREFIX;
    memcpy(aDataset.mMeshLocalPrefix.m8, meshLocalPrefix, sizeof(aDataset.mMeshLocalPrefix));
    aDataset.mComponents.mIsMeshLocalPrefixPresent = true;

    otDatasetSetActive(myOtInstance, &aDataset); // cli command: dataset commit active
}

static void thread_instance_init(void)
{
    esp_openthread_platform_config_t config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };

    ESP_ERROR_CHECK(esp_openthread_init(&config)); // Init OpenThread stack and instance
    esp_openthread_cli_init();                     // Init OpenThread CLI

    openthread_netif = init_openthread_netif(&config);
    esp_netif_set_default_netif(openthread_netif); // set default network interface as thread network interface

#if CONFIG_OPENTHREAD_LOG_LEVEL_DYNAMIC
    // The OpenThread log level directly matches ESP log level
    (void)otLoggingSetLevel(CONFIG_LOG_DEFAULT_LEVEL);
#endif
}

static void thread_network_start(void)
{
    otInstance *myOtInstance = esp_openthread_get_instance();

    /* Start the Thread network interface (cli command: ifconfig up) */
    otIp6SetEnabled(myOtInstance, true);

    /* Start the Thread stack (cli command: thread start) */
    otThreadSetEnabled(myOtInstance, true);
}

static void thread_process(void *aContext)
{
    // Run CLI and main OpenThread loops
    esp_openthread_cli_create_task();
    esp_openthread_launch_mainloop(); // doesn't return unless error occurs

    // Clean up if mainloop exits
    printf("Thread mainloop returned, cleaning up!\n");
    esp_openthread_netif_glue_deinit();
    esp_netif_destroy(openthread_netif);

    esp_vfs_eventfd_unregister();
    vTaskDelete(NULL);
}

static void udp_send(void){
  otError error = OT_ERROR_NONE;
  const char *buf = "Test message";
  
  otInstance *myOtInstance = esp_openthread_get_instance();
  otUdpSocket mySocket;

  otMessageInfo messageInfo;
  memset(&messageInfo, 0, sizeof(messageInfo));
  // set destination IP address and port #
  otIp6AddressFromString("ff03::1", &messageInfo.mPeerAddr); // broadcast address 
  messageInfo.mPeerPort = 1234; // dest port

  do
  {
    error = otUdpOpen(myOtInstance, &mySocket, NULL, NULL);
    if (error != OT_ERROR_NONE) break;

    otMessage *udp_message = otUdpNewMessage(myOtInstance, NULL);
    error = otMessageAppend(udp_message, buf, (uint16_t)strlen(buf)); 
    if (error != OT_ERROR_NONE) break;

    error = otUdpSend(myOtInstance, &mySocket, udp_message, &messageInfo);
    if (error != OT_ERROR_NONE) break;

    error = otUdpClose(myOtInstance, &mySocket);
  }while(false);

  if (error == OT_ERROR_NONE)
  {
    otCliOutputFormat("Message sent.\0");
  }else
  {
    printf("udpSend error!\n");
  }
}

static void udp_process(void *aContext)
{
    while(true)
    {
        udp_send();
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}

// TODO: add button handler to send udp messasge on button press

void app_main(void)
{
    // Used eventfds:
    // * netif
    // * ot task queue
    // * radio driver
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

    thread_instance_init();
    config_thread_dataset();
    thread_network_start();
    xTaskCreate(thread_process, "thread_process", 10240, xTaskGetCurrentTaskHandle(), 5, NULL);
    xTaskCreate(udp_process, "udp_process", 4096, NULL, 4, NULL);
}
