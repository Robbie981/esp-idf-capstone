#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "coap_client.h"
#include "misc.h"
#include "esp_log.h"

static void coap_send_data(void);
static void coap_send_data_response_handler(void * p_context, otMessage * p_message, const otMessageInfo * p_message_info, otError result);

/**@brief Function for sending data with CoAP */
static void coap_send_data(void)
{
    otError       error = OT_ERROR_NONE;
    otMessage   * myMessage;
    otMessageInfo myMessageInfo;
    const char  * serverIpAddr = "fd00:0:fb01:1::1";
    const char * myTemperatureJson = "{\"temperature\": 23.32}";

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
        error = otMessageAppend(myMessage, myTemperatureJson, strlen(myTemperatureJson));
        if (error != OT_ERROR_NONE){ break; }

        //Set the UDP-destination port of the CoAP-server
        memset(&myMessageInfo, 0, sizeof(myMessageInfo));
        myMessageInfo.mPeerPort = OT_DEFAULT_COAP_PORT;

        //Set the IP-address of the CoAP-server
        error = otIp6AddressFromString(serverIpAddr, &myMessageInfo.mPeerAddr);
        if (error != OT_ERROR_NONE){ break; }

        //Send CoAP-request
        error = otCoapSendRequest(OT_INSTANCE, myMessage, &myMessageInfo, coap_send_data_response_handler, NULL);
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

/**@brief Function for handling the response from the request. */
static void coap_send_data_response_handler(void * p_context, otMessage * p_message, const otMessageInfo * p_message_info, otError result)
{                                              
    if (result == OT_ERROR_NONE) 
        otCliOutputFormat("Delivery confirmed.\r\n\0");
    else
        ESP_LOGI(LOCAL_DEBUG_TAG, "Delivery not confirmed: %d\r\n", result);
}

static void coap_process(void *aContext)
{
    while(true)
    {
        coap_send_data();
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
        xTaskCreate(coap_process, "udp_process", 4096, NULL, 4, NULL);
    }
    s_previous_role = role;
}