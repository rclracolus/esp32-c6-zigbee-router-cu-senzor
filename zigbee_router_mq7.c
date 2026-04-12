#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_adc_cal.h"
#include "esp_zigbee_core.h"
#include "ha/esp_zigbee_ha_standard.h"

static const char *TAG = "ZB_ROUTER_MQ7";

/* GPIO Configuration */
#define PAIRING_BUTTON_GPIO     GPIO_NUM_9      // Buton pentru pairing
#define MQ7_ANALOG_GPIO         ADC_CHANNEL_0   // GPIO0 pentru MQ-7 (ADC1_CH0)
#define LED_INDICATOR_GPIO      GPIO_NUM_8      // LED status (optional)

/* Zigbee Configuration */
#define INSTALLCODE_POLICY_ENABLE   false
#define MAX_CHILDREN                10

/* MQ-7 Configuration */
#define MQ7_SAMPLE_PERIOD_MS        2000        // Citește la fiecare 2 secunde
#define MQ7_ALARM_THRESHOLD         800         // Prag de alarmă CO (în ppm simulat)
#define DEFAULT_VREF                1100        // Referință ADC în mV

/* Zigbee Endpoint */
#define HA_ESP_SENSOR_ENDPOINT      10

/* Button debounce */
#define BUTTON_DEBOUNCE_MS          50
#define BUTTON_LONG_PRESS_MS        3000

static esp_adc_cal_characteristics_t *adc_chars;
static TimerHandle_t button_timer;
static bool button_pressed = false;
static uint32_t button_press_time = 0;

/* Zigbee configuration */
#define ESP_ZB_ZR_CONFIG()                                              \
    {                                                                   \
        .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER,                      \
        .install_code_policy = INSTALLCODE_POLICY_ENABLE,              \
        .nwk_cfg = {                                                   \
            .zczr_cfg = {                                              \
                .max_children = MAX_CHILDREN,                          \
            },                                                         \
        },                                                             \
    }

#define ESP_ZB_DEFAULT_RADIO_CONFIG()                           \
    {                                                           \
        .radio_mode = RADIO_MODE_NATIVE,                       \
    }

#define ESP_ZB_DEFAULT_HOST_CONFIG()                            \
    {                                                           \
        .host_connection_mode = HOST_CONNECTION_MODE_NONE,     \
    }

/* Structure pentru datele senzorului */
typedef struct {
    uint16_t co_level;          // Nivel CO în ppm (simulat)
    bool alarm_status;          // Status alarmă
    uint32_t raw_adc;           // Valoare brută ADC
} mq7_data_t;

static mq7_data_t mq7_data = {0};

/* LED indicator functions */
static void led_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << LED_INDICATOR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    gpio_set_level(LED_INDICATOR_GPIO, 0);
}

static void led_set(bool state)
{
    gpio_set_level(LED_INDICATOR_GPIO, state ? 1 : 0);
}

static void led_blink(int times, int delay_ms)
{
    for (int i = 0; i < times; i++) {
        led_set(true);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        led_set(false);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

/* ADC/MQ-7 Initialization */
static void mq7_adc_init(void)
{
    // Configure ADC
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(MQ7_ANALOG_GPIO, ADC_ATTEN_DB_11);
    
    // Characterize ADC
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 
                             DEFAULT_VREF, adc_chars);
    
    ESP_LOGI(TAG, "MQ-7 ADC initialized on GPIO0 (ADC1_CH0)");
}

/* Read MQ-7 sensor */
static void mq7_read_sensor(void)
{
    uint32_t adc_reading = 0;
    
    // Multisampling pentru acuratețe
    for (int i = 0; i < 32; i++) {
        adc_reading += adc1_get_raw(MQ7_ANALOG_GPIO);
    }
    adc_reading /= 32;
    
    // Convert to voltage
    uint32_t voltage = esp_adc_cal_raw_to_voltage(adc_reading, adc_chars);
    
    // Simulare conversie la ppm CO (formula simplificată)
    // Într-o implementare reală, ai nevoie de curba de calibrare MQ-7
    // Rs/R0 = f(ppm) - vezi datasheet-ul MQ-7
    float ratio = (float)voltage / 3300.0;
    uint16_t co_ppm = (uint16_t)(ratio * 1000); // Conversie simplificată
    
    mq7_data.raw_adc = adc_reading;
    mq7_data.co_level = co_ppm;
    mq7_data.alarm_status = (co_ppm > MQ7_ALARM_THRESHOLD);
    
    ESP_LOGI(TAG, "MQ-7: ADC=%lu, Voltage=%lumV, CO=%uppm, Alarm=%s",
             adc_reading, voltage, co_ppm, 
             mq7_data.alarm_status ? "YES" : "NO");
    
    // LED indicator pentru alarmă
    if (mq7_data.alarm_status) {
        led_blink(3, 100);
    }
}

/* Button interrupt handler */
static void IRAM_ATTR button_isr_handler(void *arg)
{
    uint32_t gpio_num = (uint32_t)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    if (gpio_num == PAIRING_BUTTON_GPIO) {
        xTimerStartFromISR(button_timer, &xHigherPriorityTaskWoken);
    }
    
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

/* Button debounce timer callback */
static void button_timer_callback(TimerHandle_t xTimer)
{
    int button_state = gpio_get_level(PAIRING_BUTTON_GPIO);
    
    if (button_state == 0 && !button_pressed) {
        // Button pressed
        button_pressed = true;
        button_press_time = xTaskGetTickCount();
        ESP_LOGI(TAG, "Button pressed - starting pairing countdown");
        led_set(true);
    } 
    else if (button_state == 1 && button_pressed) {
        // Button released
        uint32_t press_duration = (xTaskGetTickCount() - button_press_time) * portTICK_PERIOD_MS;
        button_pressed = false;
        led_set(false);
        
        if (press_duration >= BUTTON_LONG_PRESS_MS) {
            // Long press - start pairing
            ESP_LOGI(TAG, "Long press detected (%lums) - Starting pairing mode", press_duration);
            led_blink(5, 200);
            
            // Permite pairing pentru 180 secunde
            esp_zb_bdb_open_network(180);
            ESP_LOGI(TAG, "Network opened for 180 seconds for new devices to join");
        } else {
            // Short press - doar info
            ESP_LOGI(TAG, "Short press detected (%lums) - Press 3s for pairing", press_duration);
            led_blink(2, 100);
        }
    }
}

/* Button initialization */
static void button_init(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << PAIRING_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    gpio_config(&io_conf);
    
    // Create debounce timer
    button_timer = xTimerCreate("button_timer", 
                                pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS),
                                pdFALSE, NULL, button_timer_callback);
    
    // Install ISR
    gpio_install_isr_service(0);
    gpio_isr_handler_add(PAIRING_BUTTON_GPIO, button_isr_handler, 
                        (void *)PAIRING_BUTTON_GPIO);
    
    ESP_LOGI(TAG, "Pairing button initialized on GPIO%d", PAIRING_BUTTON_GPIO);
}

/* Zigbee attribute update */
static void update_zigbee_attributes(void)
{
    /* Update CO level attribute */
    esp_zb_zcl_set_attribute_val(HA_ESP_SENSOR_ENDPOINT,
                                  ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
                                  &mq7_data.co_level,
                                  false);
    
    /* Update alarm status */
    esp_zb_zcl_set_attribute_val(HA_ESP_SENSOR_ENDPOINT,
                                  ESP_ZB_ZCL_CLUSTER_ID_BINARY_INPUT,
                                  ESP_ZB_ZCL_CLUSTER_SERVER_ROLE,
                                  ESP_ZB_ZCL_ATTR_BINARY_INPUT_PRESENT_VALUE_ID,
                                  &mq7_data.alarm_status,
                                  false);
}

/* MQ-7 monitoring task */
static void mq7_monitor_task(void *pvParameters)
{
    ESP_LOGI(TAG, "MQ-7 monitoring task started");
    
    // Warmup period pentru MQ-7 (60 secunde recomandat în datasheet)
    ESP_LOGI(TAG, "MQ-7 warming up for 10 seconds...");
    vTaskDelay(pdMS_TO_TICKS(10000));
    ESP_LOGI(TAG, "MQ-7 ready!");
    
    while (1) {
        mq7_read_sensor();
        update_zigbee_attributes();
        vTaskDelay(pdMS_TO_TICKS(MQ7_SAMPLE_PERIOD_MS));
    }
}

/* Zigbee signal handler */
static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask)
{
    ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct)
{
    uint32_t *p_sg_p = signal_struct->p_app_signal;
    esp_err_t err_status = signal_struct->esp_err_status;
    esp_zb_app_signal_type_t sig_type = *p_sg_p;
    
    switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Zigbee stack initialized");
        esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
        break;
        
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Device started up in %s factory-reset mode", 
                     sig_type == ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START ? "" : "non");
            ESP_LOGI(TAG, "Start network steering");
            esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
            led_blink(3, 300);
        } else {
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)", 
                     esp_err_to_name(err_status));
            led_blink(10, 100);
        }
        break;
        
    case ESP_ZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            esp_zb_ieee_addr_t extended_pan_id;
            esp_zb_get_extended_pan_id(extended_pan_id);
            ESP_LOGI(TAG, "✓ Joined network successfully");
            ESP_LOGI(TAG, "  Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                     extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4],
                     extended_pan_id[3], extended_pan_id[2], extended_pan_id[1], extended_pan_id[0]);
            ESP_LOGI(TAG, "  PAN ID: 0x%04hx", esp_zb_get_pan_id());
            ESP_LOGI(TAG, "  Channel: %d", esp_zb_get_current_channel());
            ESP_LOGI(TAG, "  Short Address: 0x%04hx", esp_zb_get_short_address());
            led_set(true);
            vTaskDelay(pdMS_TO_TICKS(1000));
            led_set(false);
        } else {
            ESP_LOGI(TAG, "Network steering failed (status: %s)", esp_err_to_name(err_status));
            ESP_LOGI(TAG, "Retrying in 1 second...");
            esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, 
                                   ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
        }
        break;
        
    case ESP_ZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Network permit join status changed");
        }
        break;
        
    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s", 
                 esp_zb_zdo_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
}

/* Create custom sensor endpoint */
static void esp_zb_create_sensor_ep(esp_zb_ep_list_t *ep_list)
{
    esp_zb_cluster_list_t *cluster_list = esp_zb_zcl_cluster_list_create();
    
    /* Basic cluster */
    esp_zb_attribute_list_t *basic_cluster = esp_zb_basic_cluster_create(NULL);
    uint8_t manufacturer_name[] = {9, 'E', 's', 'p', 'r', 'e', 's', 's', 'i', 'f'};
    uint8_t model_id[] = {13, 'M', 'Q', '-', '7', '.', 'R', 'o', 'u', 't', 'e', 'r', '.', '1'};
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manufacturer_name);
    esp_zb_basic_cluster_add_attr(basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model_id);
    esp_zb_cluster_list_add_basic_cluster(cluster_list, basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    
    /* Analog Input cluster for CO level */
    esp_zb_attribute_list_t *analog_input_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_ANALOG_INPUT);
    float initial_value = 0.0f;
    esp_zb_analog_input_cluster_add_attr(analog_input_cluster, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID, &initial_value);
    uint8_t description[] = {8, 'C', 'O', ' ', 'L', 'e', 'v', 'e', 'l'};
    esp_zb_analog_input_cluster_add_attr(analog_input_cluster, ESP_ZB_ZCL_ATTR_ANALOG_INPUT_DESCRIPTION_ID, description);
    esp_zb_cluster_list_add_analog_input_cluster(cluster_list, analog_input_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    
    /* Binary Input cluster for alarm status */
    esp_zb_attribute_list_t *binary_input_cluster = esp_zb_zcl_attr_list_create(ESP_ZB_ZCL_CLUSTER_ID_BINARY_INPUT);
    bool alarm_initial = false;
    esp_zb_binary_input_cluster_add_attr(binary_input_cluster, ESP_ZB_ZCL_ATTR_BINARY_INPUT_PRESENT_VALUE_ID, &alarm_initial);
    esp_zb_cluster_list_add_binary_input_cluster(cluster_list, binary_input_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
    
    /* Create endpoint */
    esp_zb_endpoint_config_t endpoint_config = {
        .endpoint = HA_ESP_SENSOR_ENDPOINT,
        .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
        .app_device_id = ESP_ZB_HA_CUSTOM_ATTR_DEVICE_ID,
        .app_device_version = 0
    };
    esp_zb_ep_list_add_ep(ep_list, cluster_list, endpoint_config);
}

/* Zigbee main task */
static void esp_zb_task(void *pvParameters)
{
    /* Initialize Zigbee stack */
    esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();
    esp_zb_init(&zb_nwk_cfg);
    
    /* Create endpoint list */
    esp_zb_ep_list_t *ep_list = esp_zb_ep_list_create();
    
    /* Add custom sensor endpoint */
    esp_zb_create_sensor_ep(ep_list);
    
    /* Register device */
    esp_zb_device_register(ep_list);
    
    /* Register action handler */
    esp_zb_core_action_handler_register(NULL);
    
    /* Set network channel */
    esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);
    
    ESP_LOGI(TAG, "Starting Zigbee router with MQ-7 sensor");
    ESP_ERROR_CHECK(esp_zb_start(false));
    
    /* Main loop */
    esp_zb_main_loop_iteration();
}

void app_main(void)
{
    esp_zb_platform_config_t config = {
        .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
    };
    
    /* Initialize NVS */
    ESP_ERROR_CHECK(nvs_flash_init());
    
    /* Initialize platform */
    ESP_ERROR_CHECK(esp_zb_platform_config(&config));
    
    /* Initialize peripherals */
    led_init();
    button_init();
    mq7_adc_init();
    
    ESP_LOGI(TAG, "╔════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  ESP32-C6 Zigbee Router + MQ-7 CO      ║");
    ESP_LOGI(TAG, "║  Pairing Button: GPIO%d (3s press)     ║", PAIRING_BUTTON_GPIO);
    ESP_LOGI(TAG, "║  MQ-7 Sensor: GPIO0 (ADC1_CH0)         ║");
    ESP_LOGI(TAG, "║  LED Status: GPIO%d                     ║", LED_INDICATOR_GPIO);
    ESP_LOGI(TAG, "╚════════════════════════════════════════╝");
    
    /* Start Zigbee task */
    xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
    
    /* Start MQ-7 monitoring task */
    xTaskCreate(mq7_monitor_task, "MQ7_monitor", 2048, NULL, 4, NULL);
}
