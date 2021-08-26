#pragma once

#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "ds18b20.h"
#include "owb.h"
#include "time.h"

class temperature_observer
{
private:
  owb_rmt_driver_info rmt_driver_info;
  OneWireBus *oneWireBus;
  DS18B20_Info *ds18b20_info;

  esp_mqtt_client_handle_t mqtt_client;
  bool is_observer_initialized = false;

  DS18B20_ERROR measure(float *value);
public:
  temperature_observer();
  ~temperature_observer();

  /**
   * Initialize the the one wire bus and the ds18b20 temperature sensor
   * This should be called befor start
   */
  esp_err_t init();

  /**
   * Set the mqtt client handle for the observer.
   * This should be called before start()
  */
  void set_mqtt_client(esp_mqtt_client_handle_t mqtt_client);

  /**
   * Start the observer, consider to call init and set_mqtt_client before call start
  */
  void start();
};


