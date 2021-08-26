/*
*/
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_spi_flash.h"
#include "temperature-observer.h"
#include "temperature-wifi.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "nvs_flash.h"
#include "esp_err.h"
#include "time.h"
#include "esp_sntp.h"

static const char *TAG = "temperature_main";

#define WIFI_SSID CONFIG_WIFI_SSID

#define WIFI_PASSWORD CONFIG_WIFI_PASSWORD

#define BROKER_HOST CONFIG_BROKER_HOST

#define BROKER_PORT CONFIG_BROKER_PORT

#define INTERVAL 5

#if CONFIG_TRANSPORT_MQTT
#define TRANSPORT MQTT_TRANSPORT_OVER_TCP
#endif

#if CONFIG_TRANSPORT_MQTTS
#define TRANSPORT MQTT_TRANSPORT_OVER_SSL
#endif

#if CONFIG_TRANSPORT_WS
#define TRANSPORT MQTT_TRANSPORT_OVER_WS
#endif

#if CONFIG_PROTOCOL_WSS
#define TRANSPORT MQTT_TRANSPORT_OVER_WSS
#endif

extern "C"
{
  void app_main(void);
}

static temperature_observer *observer;

typedef struct
{
  tm execution_time;
} start_task_t;

void start_task(void *pvParmeters)
{
  start_task_t *data = (start_task_t *)pvParmeters;
  if(data == NULL)
  {
    ESP_LOGD(TAG, "Data was null");
  }
  if (data != NULL)
  {
    while(1)
    {
      time_t now;
      struct tm timeinfo;

      time(&now);
      localtime_r(&now, &timeinfo);
      if (timeinfo.tm_min == data->execution_time.tm_min && timeinfo.tm_sec == data->execution_time.tm_sec)
      {
        ESP_LOGI(TAG, "Start temperature sensor now");
        observer->start();
        break;
      }
      vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
  }
  vTaskDelete(NULL);
}

void pre_exec_timer(TimerHandle_t timer_handle)
{

}

void calculate_execution_time(tm *execution_time, int interval)
{
  //initialize variables and get time
  time_t now;
  time(&now);
  localtime_r(&now, execution_time);

  //simple math calculation for the execution time
  //exampe: 8 = 23 % 15
  //        30 = 23 - 8 + 15
  int moduloResult = execution_time->tm_min % interval;
  int minute = execution_time->tm_min - moduloResult + interval;

  //modify the time structure
  execution_time->tm_min = minute;
  execution_time->tm_sec = 0;
}

void receive_mqtt_client(esp_mqtt_client_handle_t mqtt_client)
{
  ESP_LOGI(TAG, "Receive mqtt client");
  if (observer != NULL)
  {
    ESP_LOGI(TAG, "pass mqtt client to observer");
    observer->set_mqtt_client(mqtt_client);
  }

  int retry = 0;
  const int retry_count = 10;
  while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count)
  {
    ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
    vTaskDelay(2000 / portTICK_PERIOD_MS);
  }

  struct tm execution_time;
  calculate_execution_time(&execution_time, INTERVAL);

  start_task_t data = {};
  data.execution_time = execution_time;
  TaskHandle_t task_handle = NULL;
  BaseType_t result = xTaskCreatePinnedToCore(start_task, "start task", 2048, &data, tskIDLE_PRIORITY, &task_handle, APP_CPU_NUM);
  xTimerCreate("Pre exec", 100, pdFALSE, &data, pre_exec_timer);

  if (result == pdPASS)
  {
    ESP_LOGI(TAG, "Task created");
  }
}

void app_main()
{
  ESP_LOGI(TAG, "Start Temperature Observer");

  ESP_LOGI(TAG, "Initialize Non Volatile Storage");
  esp_err_t err_code = nvs_flash_init();
  if (err_code == ESP_ERR_NVS_NO_FREE_PAGES || err_code == ESP_ERR_NVS_NEW_VERSION_FOUND)
  {
    ESP_LOGI(TAG, "Erase nvs and initialize again");
    ESP_ERROR_CHECK(nvs_flash_erase());
    err_code = nvs_flash_init();
  }

  ESP_ERROR_CHECK(err_code);
  esp_netif_init();

  // workaround initialization for wifi_config to avoid outside aggregate initializer in c++
  wifi_config_t wifi_config = {};
  strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
  strcpy((char *)wifi_config.sta.password, WIFI_PASSWORD);
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  esp_mqtt_client_config_t mqtt_config = {};
  mqtt_config.host = BROKER_HOST;
  mqtt_config.port = BROKER_PORT;
  mqtt_config.transport = TRANSPORT;

  observer = new temperature_observer();
  // observer->start();

  temperature_wifi *wifi_client = new temperature_wifi(&wifi_config, &mqtt_config);
  wifi_client->set_callback_mqtt_client(receive_mqtt_client);
  wifi_client->start_wifi();
}
