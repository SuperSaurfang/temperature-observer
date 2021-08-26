#include "temperature-observer.h"

static const char *TAG = "temperature_observer";

temperature_observer::temperature_observer(/* args */)
{
  this->ds18b20_info = ds18b20_malloc();
  this->oneWireBus = owb_rmt_initialize(&this->rmt_driver_info, GPIO_NUM_25, RMT_CHANNEL_1, RMT_CHANNEL_0);
  ESP_LOGI(TAG, "ds18b20 created, and init is: %d", this->ds18b20_info->init);
}

temperature_observer::~temperature_observer()
{
  ds18b20_free(&this->ds18b20_info);
  owb_uninitialize(this->oneWireBus);
}

esp_err_t temperature_observer::init()
{
  // error and status codes
  owb_status owb_status;
  DS18B20_ERROR ds18b20_error;

  // set crc to true
  owb_status = owb_use_crc(this->oneWireBus, true);
  if(owb_status != OWB_STATUS_OK)
  {
    return ESP_FAIL;
  }

  ds18b20_init_solo(this->ds18b20_info, this->oneWireBus);
  ds18b20_use_crc(this->ds18b20_info, true);
  bool is_successful = ds18b20_set_resolution(this->ds18b20_info, DS18B20_RESOLUTION_12_BIT);

  // parasitic power default false
  bool with_parasitic_power = false;
  ds18b20_error = ds18b20_check_for_parasite_power(this->oneWireBus, &with_parasitic_power);
  if(ds18b20_error != DS18B20_OK || is_successful == false)
  {
    return ESP_FAIL;
  }
  owb_status = owb_use_parasitic_power(this->oneWireBus, with_parasitic_power);
  if(owb_status != OWB_STATUS_OK)
  {
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Sensor initialization successful");
  this->is_observer_initialized = true;
  return ESP_OK;
}

DS18B20_ERROR temperature_observer::measure(float *value)
{
  ds18b20_convert_all(this->oneWireBus);
  ds18b20_wait_for_conversion(this->ds18b20_info);
  return ds18b20_read_temp(this->ds18b20_info, value);
}

void temperature_observer::set_mqtt_client(esp_mqtt_client_handle_t mqtt_client)
{
  this->mqtt_client = mqtt_client;
}

void temperature_observer::start()
{
  //just make sure the mqtt client is set and the observer is initialized otherwise we cannot start
  if(this->mqtt_client != NULL && this->is_observer_initialized == true)
  {
    ESP_LOGD(TAG, "Ready to start");
  }
  else
  {
    ESP_LOGD(TAG, "Not ready to start");
  }
}