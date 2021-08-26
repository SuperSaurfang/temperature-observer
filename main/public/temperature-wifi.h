#pragma once

#include "esp_wifi.h"
#include "esp_log.h"
#include "freertos/event_groups.h"
#include "mqtt-event-handler.h"
#include "temperature-observer.h"

typedef void (*callback_mqtt_client)(esp_mqtt_client_handle_t mqtt_client);

class temperature_wifi : public mqtt_event_handler
{
private:
  wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
  wifi_config_t *config;
  EventGroupHandle_t temperature_wifi_event_group;

  esp_mqtt_client_handle_t mqtt_client;
  esp_mqtt_client_config_t *mqtt_config;
  EventGroupHandle_t mqtt_event_group;

  callback_mqtt_client callback;

  /**
   * Create the event loop to handle wifi event
   * @return an esp error code
   */
  esp_err_t create_event_loop();

  /**
   * Create a wifi station, that can connect to an access point
   * @return esp error code
  */
  esp_err_t start_wifi_sta();

  /**
   * Log an esp error code, with a message
   * @param err_code the error code
   * @param message the message
  */
  void log_err_code(esp_err_t err_code, const char *message);

  /**
   * Call wifi connect and log error if not succeed
  */
  void connect();

  /**
   * Wait for the event group handle
  */
  void event_group_wait();

  /**
   * Connects the mqtt client to the mqtt broker
   */
  void connect_mqtt();

  /**
   * Start to sync the system time via sntp
   */
  void sync_time();

public:
  esp_netif_t *netif;
  /**
   * constructor
   * @param config wifi configuration
  */
  temperature_wifi(wifi_config_t *config, esp_mqtt_client_config_t *mqtt_config);
  ~temperature_wifi();

  /**
   * Start the wifi client
   * @return esp error code. ESP_OK means succeed
   */
  esp_err_t start_wifi();

  /**
   * Consume a wifi event
   * @param id event id for wifi events
   * @param event_data data from the event, should be cast to specific typ
  */
  void consume_wifi_event(int32_t id, void *event_data);

  /**
   * Consume an ip event
   * @param id event id for ip events
   * @param event_date data from the event, should be cast to specific typ
  */
  void consume_ip_event(int32_t id, void *event_data);

  void set_callback_mqtt_client(callback_mqtt_client callback);

  esp_err_t consume_mqtt_event(esp_mqtt_event_handle_t event) override;
};
