#include "esp_sntp.h"
#include "temperature-wifi.h"

static const char *TAG = "temperature_wifi";

//wifi and mqtt use different retry counters to connect
static int wifi_retry_count = 0;
static int mqtt_retry_count = 0;

#define MAXIUM_RETRY CONFIG_MAXIMUN_CONNECT_RETRY
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define MQTT_CONNECTED_BIT BIT0
#define MQTT_FAIL_BIT BIT1

void event_handler(void *handler_arg, esp_event_base_t base, int32_t id, void *event_data)
{
  temperature_wifi *temperature_wifi_ref = (temperature_wifi *)handler_arg;
  // if the cast fail in any reasen or the handler_arg is null this not a valid wifi/ip event to consume
  if (temperature_wifi_ref == NULL || handler_arg == NULL)
  {
    ESP_LOGW(TAG, "Unexpected Event, handler_arg was null or cast fail");
    return;
  }

  if (base == WIFI_EVENT)
  {
    ESP_LOGD(TAG, "Consume Wifi Event");
    temperature_wifi_ref->consume_wifi_event(id, event_data);
  }
  else if (base == IP_EVENT)
  {
    ESP_LOGD(TAG, "Consume IP Event");
    temperature_wifi_ref->consume_ip_event(id, event_data);
  }
  else
  {
    ESP_LOGW(TAG, "Unexpected base event: %s", base);
  }
}

// workaround to go back to mqtt event handler object scope, so we can set the connected bit and call other object stuff
// the observer was set in the constructor, the event handler was set when the mqtt client start to connect
static mqtt_event_handler *mqtt_handler;
static esp_err_t mqtt_event_handler_static(esp_mqtt_event_handle_t event)
{
  if (event != NULL && mqtt_handler != NULL)
  {
    return mqtt_handler->consume_mqtt_event(event);
  }
  return ESP_ERR_INVALID_ARG;
}

temperature_wifi::temperature_wifi(wifi_config_t *config, esp_mqtt_client_config_t *mqtt_config)
{
  this->config = config;
  this->mqtt_config = mqtt_config;
  //create event groups
  this->temperature_wifi_event_group = xEventGroupCreate();
  this->mqtt_event_group = xEventGroupCreate();
  //set the static event handler here
  this->mqtt_config->event_handle = mqtt_event_handler_static;
  mqtt_handler = this;
}

temperature_wifi::~temperature_wifi()
{
}

esp_err_t temperature_wifi::create_event_loop()
{
  ESP_LOGD(TAG, "Create Event Loop");
  esp_err_t return_code;

  return_code = esp_event_loop_create_default();
  // only continue if ESP_OK
  if (return_code == ESP_OK)
  {
    return_code = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, this);
  }

  if (return_code == ESP_OK)
  {
    return_code = esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &event_handler, this);

    if (return_code == ESP_OK)
    {
      ESP_LOGD(TAG, "Event Loop Succesfully created.");
    }
  }

  // if something went wrong
  if (return_code != ESP_OK)
  {
    this->log_err_code(return_code, "Fail to create and register event handler");
  }
  return return_code;
}

esp_err_t temperature_wifi::start_wifi_sta()
{
  esp_err_t return_code;
  this->netif = esp_netif_create_default_wifi_sta();
  return_code = esp_wifi_set_mode(WIFI_MODE_STA);

  if (return_code == ESP_OK)
  {
    return_code = esp_wifi_set_config(ESP_IF_WIFI_STA, this->config);
  }

  if (return_code == ESP_OK)
  {
    return_code = esp_wifi_start();
  }

  if (return_code != ESP_OK)
  {
    this->log_err_code(return_code, "fail to start wifi station");
  }

  return return_code;
}

esp_err_t temperature_wifi::start_wifi()
{
  ESP_LOGI(TAG, "Start Wifi");
  esp_err_t return_code;

  return_code = this->create_event_loop();
  if (return_code == ESP_OK)
  {
    esp_wifi_init(&this->init_config);
    return_code = this->start_wifi_sta();
    ESP_LOGD(TAG, "Station started");
  }
  if (return_code == ESP_OK)
  {
    this->event_group_wait();
  }
  return return_code;
}

void temperature_wifi::consume_ip_event(int32_t id, void *event_data)
{
  switch (id)
  {
  case IP_EVENT_STA_GOT_IP:
  {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGD(TAG, "Got ip: " IPSTR, IP2STR(&event->ip_info.ip));
    //reset the counter after connecting to access point
    wifi_retry_count = 0;
    xEventGroupSetBits(this->temperature_wifi_event_group, WIFI_CONNECTED_BIT);
    break;
  }
  default:
    break;
  }
}

void temperature_wifi::consume_wifi_event(int32_t id, void *event_data)
{
  switch (id)
  {
  case WIFI_EVENT_STA_START:
  {
    ESP_LOGD(TAG, "Event station start");
    this->connect();
    break;
  }
  case WIFI_EVENT_STA_DISCONNECTED:
    ESP_LOGD(TAG, "Event station disconnected");
    if (wifi_retry_count < MAXIUM_RETRY)
    {
      this->connect();
      wifi_retry_count++;
      ESP_LOGD(TAG, "Retry to connect to the AP");
    }
    else
    {
      xEventGroupSetBits(this->temperature_wifi_event_group, WIFI_FAIL_BIT);
    }
    ESP_LOGW(TAG, "connect to the AP fail");
    break;
  case WIFI_EVENT_STA_CONNECTED:
    ESP_LOGD(TAG, "Event Station connected");
    break;
  default:
    ESP_LOGD(TAG, "Other wifi event, id: %d", id);
    break;
  }
}

void temperature_wifi::connect()
{
  esp_err_t return_code = esp_wifi_connect();
  //only if something went wrong
  if (return_code != ESP_OK)
  {
    this->log_err_code(return_code, "Wifi connect fail");
  }
}

void temperature_wifi::event_group_wait()
{
  EventBits_t bits = xEventGroupWaitBits(this->temperature_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE,
                                         pdFALSE,
                                         portMAX_DELAY);

  if (bits & WIFI_CONNECTED_BIT)
  {
    ESP_LOGI(TAG, "Connected to access point");
    this->sync_time();
    this->connect_mqtt();
  }
  else if (bits & WIFI_FAIL_BIT)
  {
    ESP_LOGW(TAG, "Failed to connect to access point");
  }
  else
  {
    ESP_LOGW(TAG, "Unexpected event");
  }
}

void temperature_wifi::log_err_code(esp_err_t error_code, const char *message)
{
  const char *code_name = esp_err_to_name(error_code);
  ESP_LOGW(TAG, "Return Code: %s, from: %s", code_name, message);
}

void temperature_wifi::sync_time()
{
  // set timezone here
  setenv("TZ", "UTC-2", 1);
  tzset();

  sntp_setoperatingmode(SNTP_OPMODE_POLL);
  // future plan: maybe replace the string with a config, so the user can switch the ntp server
  sntp_setservername(0, "pool.ntp.org");
  sntp_init();
  ESP_LOGI(TAG, "Start SNTP client");
}

void temperature_wifi::connect_mqtt()
{
  ESP_LOGI(TAG, "Start connecting to MQTT");
  this->mqtt_client = esp_mqtt_client_init(this->mqtt_config);
  if (this->mqtt_client == NULL)
  {
    ESP_LOGW(TAG, "Unable to create mqtt client");
  }
  else
  {
    ESP_LOGI(TAG, "Start mqtt client now");
    esp_err_t return_code = esp_mqtt_client_start(this->mqtt_client);
    ESP_ERROR_CHECK(return_code);
    if (return_code == ESP_OK)
    {
      EventBits_t bits = xEventGroupWaitBits(this->mqtt_event_group, MQTT_CONNECTED_BIT | MQTT_FAIL_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
      if (bits & MQTT_CONNECTED_BIT & (this->callback != NULL))
      {
        this->callback(this->mqtt_client);
      }
      else if (bits & MQTT_FAIL_BIT)
      {
        ESP_LOGW(TAG, "Fail connect to MQTT Broker");
        esp_mqtt_client_stop(this->mqtt_client);
      }
      else if(this->callback == NULL)
      {
        ESP_LOGW(TAG, "Callback function was null");
      }
      else
      {
        ESP_LOGW(TAG, "Something unexpected happened");
      }
    }
  }
}

esp_err_t temperature_wifi::consume_mqtt_event(esp_mqtt_event_handle_t event)
{
  switch (event->event_id)
  {
  case MQTT_EVENT_ERROR:
    ESP_LOGD(TAG, "ESP Error event");
    xEventGroupSetBits(this->mqtt_event_group, MQTT_FAIL_BIT);
    break;
  case MQTT_EVENT_CONNECTED:
  {
    ESP_LOGD(TAG, "Connected to broker");
    xEventGroupSetBits(this->mqtt_event_group, MQTT_CONNECTED_BIT);
    //reset the counter
    mqtt_retry_count = 0;
    break;
  }
  case MQTT_EVENT_DISCONNECTED:
    if (mqtt_retry_count < MAXIUM_RETRY)
    {
      mqtt_retry_count++;
      ESP_LOGI(TAG, "Retry to connect");
    }
    else
    {
      xEventGroupSetBits(this->mqtt_event_group, MQTT_FAIL_BIT);
    }
    ESP_LOGI(TAG, "Unable to connect mqtt broker");
    break;
  case MQTT_EVENT_DATA:
    // handle incomming data here
    ESP_LOGI(TAG, "Esp data event");
    break;
  default:
    ESP_LOGI(TAG, "Unkown Event id: %d", event->event_id);
    break;
  }
  return ESP_OK;
}

void temperature_wifi::set_callback_mqtt_client(callback_mqtt_client callback)
{
  this->callback = callback;
}