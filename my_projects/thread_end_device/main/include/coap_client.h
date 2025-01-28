#pragma once

#include "openthread/udp.h"
#include "openthread/thread.h"
#include "openthread/logging.h"
#include "openthread/coap.h"
#include "openthread/instance.h"
#include "openthread/cli.h"
#include "esp_openthread.h"

void coapClientStartCallback(otChangedFlags changed_flags, void* ctx);