#include "mqtt_client.h"

class mqtt_event_handler
{
public:
  virtual esp_err_t consume_mqtt_event(esp_mqtt_event_handle_t event);
};