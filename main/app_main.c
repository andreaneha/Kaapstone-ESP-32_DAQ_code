/* 
    Original Code : https://github.com/tuanpmt/esp-request-app by tuanpmt
    tuanpmt git hub: https://github.com/tuanpmt
    
    integration of ADC and communication with the Heruku Kaapstone web application : Andrea Fernandes
    Andrea Fernandes: https://github.com/andreaneha
 */
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "esp_request.h"

#include "esp_spi_flash.h"
#include <driver/adc.h>
#include "esp_adc_cal.h"
#include <sys/time.h>



#define DEFAULT_VREF    1100 

//funtions used
uint32_t * ADCread();
void configADC();

//constants used in files

static esp_adc_cal_characteristics_t *adc_chars;
static const adc_channel_t channel = ADC1_CHANNEL_0;     //GPIO34 if ADC1, GPIO14 if ADC2
static const adc_atten_t atten = ADC_ATTEN_DB_0;
static const adc_unit_t unit = ADC_UNIT_1;

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

/* The event group allows multiple bits for each event,
   but we only care about one event - are we connected
   to the AP with an IP? */
const int CONNECTED_BIT = BIT0;


static const char *TAG = "REQAPP";

struct PushData{
    int loop;
    uint32_t *readings;
}PushData;


void configADC(){
        adc_channel_t currentChannel = 0;
        for(int i = 0; i<8; i++){
            adc1_config_width(ADC_WIDTH_BIT_12);
            adc1_config_channel_atten(currentChannel,ADC_ATTEN_DB_0);
            currentChannel++;
        }
}


uint32_t * ADCread(){
    uint32_t* sensorReadings = malloc(sizeof(uint32_t)*8);
    adc_channel_t currentChannel = channel;
    for(int i = 0; i<7; i++){
        uint32_t adc_reading = 0;

        adc_reading = adc1_get_raw((adc1_channel_t)currentChannel);
        adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
        esp_adc_cal_characterize(unit, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);
        //esp_adc_cal_value_t val_type = esp_adc_cal_characterize(unit, atten, ADC_WIDTH_BIT_12, DEFAULT_VREF, adc_chars);
        uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
        //printf("(%d) Raw: %d\tVoltage: %dmV\n",currentChannel, adc_reading, voltage);
        sensorReadings[i] = voltage;
        currentChannel++;
    
        //printf(">>ADC reading: %d\n", val);
        free(adc_chars);
        }
        sensorReadings[7] = '\0';
        return sensorReadings;
}


static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        break;
    default:
        break;
    }
    return ESP_OK;
}

static void initialise_wifi(void)
{
    tcpip_adapter_init();
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_RAM) );
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
        },
    };
    ESP_LOGI(TAG, "Setting WiFi configuration SSID %s...", wifi_config.sta.ssid);
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    
}

static void request_task(void *pvParameters)
{
    struct PushData * data = (struct PushData *)pvParameters;
    uint32_t *readings = data->readings;
    char req_url[400];
    request_t *req;
    int blah = 500;
    int status;
    xEventGroupWaitBits(wifi_event_group, CONNECTED_BIT, false, true, portMAX_DELAY);
    ESP_LOGI(TAG, "Connected to AP, freemem=%d",esp_get_free_heap_size());
    req = req_new("kaapstone.herokuapp.com/sensor?time=60&s1=1.11&s2=2.22&s3=444&s4=242&s5=231&s6=231&s7=213");
    status = req_perform(req);
    req_clean(req);
    ESP_LOGI(TAG, "Finish request, status=%d, freemem=%d", status, esp_get_free_heap_size());
    vTaskDelete(NULL);
}

void app_main()
{
    nvs_flash_init();
    initialise_wifi();
    
    struct PushData *data = malloc(sizeof(PushData));
    data->loop = 0;
    configADC();
    struct timeval startTime;
    struct timeval endTime;
    struct timeval hundoEnd;
    struct timeval hundoStart;
    uint32_t * readings;
    int loop = 0;
    gettimeofday(&hundoStart,NULL);
    struct timeval prevTime; 
    while(1){
            if (loop){
                gettimeofday(&startTime,NULL);
                readings = ADCread();

                gettimeofday(&endTime,NULL);
                
                long elapsed = (endTime.tv_sec-startTime.tv_sec)*1000000LL + endTime.tv_usec-startTime.tv_usec;
                long start = startTime.tv_sec*1000000LL + startTime.tv_usec;
                long end = endTime.tv_sec*1000000LL + startTime.tv_usec;

                printf("sensor1: %d,sensor2:%d,sensor3:%d,sensor4:%d,sensor5:%d,sensor6:%d,sensor7:%d,%lu,%lu,%lu\n",
                readings[0], readings[1], readings[2], readings[3], readings[4], readings[5], readings[6],
                start, end, elapsed);
                
                
                
                //time lag calculation - need one reading taken every 0.001 seconds
                long remainder = 10000-elapsed;
                //---------------------------------------------
                vTaskDelay(10);
                
                //printf(">> %lu, %lu\n", remainder, elapsed);
                
                //readings[0] = 500;
                data->loop = loop;
                data->readings = readings;
                
                
                
                //xTaskCreate(&request_task, "request_task", 8192, data, 5, NULL);
                
                loop = loop+1;
                free(readings);
                data->readings = NULL;
            }
            loop++; 
            fflush(stdout);
        }
}
