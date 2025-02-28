#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "coap_client.h"
#include "misc.h"
#include "esp_log.h"
#include "sensors.h"

typedef struct
{
    char sensor_id[40]; // UUID string
    float temperature;
    float humidity;
    float pm25;
    float tvoc;
    float co2;
} SensorData;

static void init_sensor_data(SensorData *data)
{
    strcpy(data->sensor_id, "ffffffff-ffff-ffff-ffff-ffffffffffff");
    data->temperature = -1;
    data->humidity = -1;
    data->pm25 = -1;
    data->tvoc = -1;
    data->co2 = -1;
}

static void build_json_payload(char *buffer, size_t buffer_size, const SensorData *data) 
{
    snprintf(buffer, buffer_size, 
        "{"
        "\"sensor\": \"%s\","
        "\"temperature\": %.2f,"
        "\"humidity\": %.2f,"
        "\"pm25\": %.2f,"
        "\"tvoc\": %.2f,"
        "\"co2\": %.2f"
        "}", 
        data->sensor_id, data->temperature, data->humidity, data->pm25, data->tvoc, data->co2);
}

/**@brief Function for sending data with CoAP */
static void coap_send_data(const SensorData *sensor_data)
{
    otError       error = OT_ERROR_NONE;
    otMessage   * myMessage;
    otMessageInfo myMessageInfo;
    const char  * serverIpAddr = "fd00:0:fb01:1::1";
    //const char * myTemperatureJson = "{\"temperature\": 23.32}";
    char jsonPayload[256];
    build_json_payload(jsonPayload, sizeof(jsonPayload), sensor_data);

    do{
        //Create a new message
        myMessage = otCoapNewMessage(OT_INSTANCE, NULL);
        if (myMessage == NULL) 
            ESP_LOGI(LOCAL_DEBUG_TAG, "Failed to allocate message for CoAP Request\r\n");
        
        //Set CoAP type and code in the message
        otCoapMessageInit(myMessage, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_PUT);

        //Add the URI path option in the message
        error = otCoapMessageAppendUriPathOptions(myMessage, "coapdata");
        if (error != OT_ERROR_NONE){ break; }

        //Add the content format option in the message
        error = otCoapMessageAppendContentFormatOption(myMessage, OT_COAP_OPTION_CONTENT_FORMAT_JSON );
        if (error != OT_ERROR_NONE){ break; }

        //Set the payload delimiter in the message
        error = otCoapMessageSetPayloadMarker(myMessage);
        if (error != OT_ERROR_NONE){ break; }

        ///Append the payload to the message
        error = otMessageAppend(myMessage, jsonPayload, strlen(jsonPayload));
        if (error != OT_ERROR_NONE){ break; }

        //Set the UDP-destination port of the CoAP-server
        memset(&myMessageInfo, 0, sizeof(myMessageInfo));
        myMessageInfo.mPeerPort = OT_DEFAULT_COAP_PORT;

        //Set the IP-address of the CoAP-server
        error = otIp6AddressFromString(serverIpAddr, &myMessageInfo.mPeerAddr);
        if (error != OT_ERROR_NONE){ break; }

        //Send CoAP-request
        error = otCoapSendRequest(OT_INSTANCE, myMessage, &myMessageInfo, NULL, NULL);
    }while(false);

    if (error != OT_ERROR_NONE) 
    {
        ESP_LOGI(LOCAL_DEBUG_TAG, "Failed to send CoAP message: %d\r\n", error);
        otMessageFree(myMessage);
    }
    else
    {
        otCliOutputFormat("CoAP data sent.\r\n\0");
    }
}

static void coap_process(void *aContext)
{
    while(true)
    {
        SensorData data;
        init_sensor_data(&data);
        // TODO: get real data from sensors
        
        coap_send_data(&data);
        vTaskDelay(3000 / portTICK_PERIOD_MS);
    }
}

/**
 * Init CoAP on client device
 */
static void coap_init(uint16_t port)
{
    otError error = OT_ERROR_NONE;

    // Try to start CoAP process 
    error = otCoapStart(OT_INSTANCE, port);
    if (error != OT_ERROR_NONE) 
        ESP_LOGW(LOCAL_DEBUG_TAG, "Failed to start COAP client.");
    else
        ESP_LOGI(LOCAL_DEBUG_TAG, "CoAP client started on port %d.", port);
}

/**
 * Callback function to start CoAP client
 */
void coapClientStartCallback(otChangedFlags changed_flags, void* ctx)
{
    OT_UNUSED_VARIABLE(ctx);
    static otDeviceRole s_previous_role = OT_DEVICE_ROLE_DISABLED;

    if (!OT_INSTANCE) { return; }
    otDeviceRole role = otThreadGetDeviceRole(OT_INSTANCE);
    if (s_previous_role != role && (role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_CHILD))  // role changed to either child or router  
    {
        ESP_LOGI(LOCAL_DEBUG_TAG, "Role changed to child/router");
        coap_init(OT_DEFAULT_COAP_PORT);
        xTaskCreate(coap_process, "coap_process", 4096, NULL, 4, NULL);
    }
    s_previous_role = role;
}