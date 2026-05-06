#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_zigbee.h"
#include "ezbee/zcl/cluster/basic_desc.h"
#include "ezbee/zcl/cluster/analog_input_desc.h"
#include "ezbee/zcl/cluster/binary_input_desc.h"

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

/* Zigbee Endpoint */
#define HA_ESP_SENSOR_ENDPOINT      10

/* Button debounce */
#define BUTTON_DEBOUNCE_MS          50
#define BUTTON_LONG_PRESS_MS        3000

static adc_oneshot_unit_handle_t adc_handle;
static adc_cali_handle_t adc_cali_handle;
static TimerHandle_t button_timer;
static bool button_pressed = false;
static uint32_t button_press_time = 0;


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
    // Configure ADC oneshot unit
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    // Configure channel
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, MQ7_ANALOG_GPIO, &chan_cfg));

    // Initialize calibration
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id  = ADC_UNIT_1,
        .chan     = MQ7_ANALOG_GPIO,
        .atten    = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_cfg, &adc_cali_handle));

    ESP_LOGI(TAG, "MQ-7 ADC initialized on GPIO0 (ADC1_CH0)");
}

/* Read MQ-7 sensor */
static void mq7_read_sensor(void)
{
    int adc_reading = 0;
    int sample;

    // Multisampling pentru acuratețe
    for (int i = 0; i < 32; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, MQ7_ANALOG_GPIO, &sample));
        adc_reading += sample;
    }
    adc_reading /= 32;

    // Convert to voltage
    int voltage = 0;
    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, adc_reading, &voltage));
    
    // Simulare conversie la ppm CO (formula simplificată)
    // Într-o implementare reală, ai nevoie de curba de calibrare MQ-7
    // Rs/R0 = f(ppm) - vezi datasheet-ul MQ-7
    float ratio = (float)voltage / 3300.0;
    uint16_t co_ppm = (uint16_t)(ratio * 1000); // Conversie simplificată
    
    mq7_data.raw_adc = adc_reading;
    mq7_data.co_level = co_ppm;
    mq7_data.alarm_status = (co_ppm > MQ7_ALARM_THRESHOLD);
    
    ESP_LOGI(TAG, "MQ-7: ADC=%d, Voltage=%dmV, CO=%uppm, Alarm=%s",
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
            ezb_bdb_open_network(180);
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
    ezb_zcl_set_attr_value(HA_ESP_SENSOR_ENDPOINT,
                           EZB_ZCL_CLUSTER_ID_ANALOG_INPUT,
                           EZB_ZCL_CLUSTER_SERVER,
                           EZB_ZCL_ATTR_ANALOG_INPUT_PRESENT_VALUE_ID,
                           EZB_ZCL_STD_MANUF_CODE,
                           &mq7_data.co_level,
                           false);

    /* Update alarm status */
    ezb_zcl_set_attr_value(HA_ESP_SENSOR_ENDPOINT,
                           EZB_ZCL_CLUSTER_ID_BINARY_INPUT,
                           EZB_ZCL_CLUSTER_SERVER,
                           EZB_ZCL_ATTR_BINARY_INPUT_PRESENT_VALUE_ID,
                           EZB_ZCL_STD_MANUF_CODE,
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
static void bdb_start_top_level_commissioning_cb(void *ctx)
{
    uint8_t mode_mask = (uint8_t)(uintptr_t)ctx;
    ezb_bdb_start_top_level_commissioning(mode_mask);
}

bool esp_zb_app_signal_handler(const ezb_app_signal_t *signal)
{
    ezb_app_signal_type_t sig_type = ezb_app_signal_get_type(signal);
    const ezb_bdb_signal_simple_params_t *params = (const ezb_bdb_signal_simple_params_t *)ezb_app_signal_get_params(signal);
    esp_err_t err_status = (params) ? esp_zigbee_err_to_esp(params->status) : ESP_OK;

    switch (sig_type) {
    case EZB_ZDO_SIGNAL_SKIP_STARTUP:
        ESP_LOGI(TAG, "Zigbee stack initialized");
        ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_INITIALIZATION);
        break;

    case EZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case EZB_BDB_SIGNAL_DEVICE_REBOOT:
        if (err_status == ESP_OK) {
            ESP_LOGI(TAG, "Device started up in %s factory-reset mode",
                     sig_type == EZB_BDB_SIGNAL_DEVICE_FIRST_START ? "" : "non");
            ESP_LOGI(TAG, "Start network steering");
            ezb_bdb_start_top_level_commissioning(EZB_BDB_MODE_NETWORK_STEERING);
            led_blink(3, 300);
        } else {
            ESP_LOGW(TAG, "Failed to initialize Zigbee stack (status: %s)",
                     esp_err_to_name(err_status));
            led_blink(10, 100);
        }
        break;

    case EZB_BDB_SIGNAL_STEERING:
        if (err_status == ESP_OK) {
            ezb_extpanid_t extended_pan_id;
            ezb_nwk_get_extended_panid(&extended_pan_id);
            ESP_LOGI(TAG, "Joined network successfully");
            ESP_LOGI(TAG, "  Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x",
                     extended_pan_id.u8[7], extended_pan_id.u8[6], extended_pan_id.u8[5], extended_pan_id.u8[4],
                     extended_pan_id.u8[3], extended_pan_id.u8[2], extended_pan_id.u8[1], extended_pan_id.u8[0]);
            ESP_LOGI(TAG, "  PAN ID: 0x%04hx", ezb_nwk_get_panid());
            ESP_LOGI(TAG, "  Channel: %d", ezb_nwk_get_current_channel());
            ESP_LOGI(TAG, "  Short Address: 0x%04hx", ezb_nwk_get_short_address());
            led_set(true);
            vTaskDelay(pdMS_TO_TICKS(1000));
            led_set(false);
        } else {
            ESP_LOGI(TAG, "Network steering failed (status: %s)", esp_err_to_name(err_status));
            ESP_LOGI(TAG, "Retrying in 1 second...");
            esp_zigbee_task_queue_post(bdb_start_top_level_commissioning_cb,
                                      (void *)(uintptr_t)EZB_BDB_MODE_NETWORK_STEERING);
        }
        break;

    case EZB_NWK_SIGNAL_PERMIT_JOIN_STATUS:
        ESP_LOGI(TAG, "Network permit join status changed");
        break;

    default:
        ESP_LOGI(TAG, "ZDO signal: %s (0x%x), status: %s",
                 ezb_app_signal_to_string(sig_type), sig_type,
                 esp_err_to_name(err_status));
        break;
    }
    return true;
}

/* Create custom sensor endpoint and return device descriptor */
static ezb_af_device_desc_t esp_zb_create_sensor_device(void)
{
    /* --- Basic cluster server --- */
    ezb_zcl_basic_cluster_server_config_t basic_cfg = {
        .zcl_version  = EZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
        .power_source = EZB_ZCL_BASIC_POWER_SOURCE_SINGLE_PHASE_MAINS,
    };
    ezb_zcl_cluster_desc_t basic_desc = ezb_zcl_basic_create_cluster_desc(&basic_cfg, EZB_ZCL_CLUSTER_SERVER);
    uint8_t manufacturer_name[] = {9, 'E', 's', 'p', 'r', 'e', 's', 's', 'i', 'f'};
    uint8_t model_id[]          = {13, 'M', 'Q', '-', '7', '.', 'R', 'o', 'u', 't', 'e', 'r', '.', '1'};
    ezb_zcl_basic_cluster_desc_add_attr(basic_desc, EZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, manufacturer_name);
    ezb_zcl_basic_cluster_desc_add_attr(basic_desc, EZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, model_id);

    /* --- Analog Input cluster server - CO level (ppm) --- */
    ezb_zcl_analog_input_cluster_server_config_t analog_cfg = {
        .out_of_service = false,
        .present_value  = 0.0f,
        .status_flags   = EZB_ZCL_ANALOG_INPUT_STATUS_FLAGS_DEFAULT_VALUE,
    };
    ezb_zcl_cluster_desc_t analog_desc = ezb_zcl_analog_input_create_cluster_desc(&analog_cfg, EZB_ZCL_CLUSTER_SERVER);
    uint8_t co_description[] = {8, 'C', 'O', ' ', 'L', 'e', 'v', 'e', 'l'};
    ezb_zcl_analog_input_cluster_desc_add_attr(analog_desc, EZB_ZCL_ATTR_ANALOG_INPUT_DESCRIPTION_ID, co_description);

    /* --- Binary Input cluster server - alarm status --- */
    ezb_zcl_binary_input_cluster_server_config_t binary_cfg = {
        .out_of_service = false,
        .present_value  = false,
        .status_flags   = EZB_ZCL_BINARY_INPUT_STATUS_FLAGS_DEFAULT_VALUE,
    };
    ezb_zcl_cluster_desc_t binary_desc = ezb_zcl_binary_input_create_cluster_desc(&binary_cfg, EZB_ZCL_CLUSTER_SERVER);
    uint8_t alarm_description[] = {5, 'A', 'l', 'a', 'r', 'm'};
    ezb_zcl_binary_input_cluster_desc_add_attr(binary_desc, EZB_ZCL_ATTR_BINARY_INPUT_DESCRIPTION_ID, alarm_description);

    /* --- Endpoint --- */
    ezb_af_ep_config_t ep_config = {
        .ep_id              = HA_ESP_SENSOR_ENDPOINT,
        .app_profile_id     = EZB_AF_HA_PROFILE_ID,
        .app_device_id      = 0x0302,  /* Temperature Sensor device ID (generic sensor) */
        .app_device_version = 0,
    };
    ezb_af_ep_desc_t ep_desc = ezb_af_create_endpoint_desc(&ep_config);
    ezb_af_endpoint_add_cluster_desc(ep_desc, basic_desc);
    ezb_af_endpoint_add_cluster_desc(ep_desc, analog_desc);
    ezb_af_endpoint_add_cluster_desc(ep_desc, binary_desc);

    /* --- Device --- */
    ezb_af_device_desc_t dev_desc = ezb_af_create_device_desc();
    ezb_af_device_add_endpoint_desc(dev_desc, ep_desc);

    return dev_desc;
}

/* Zigbee main task */
static void esp_zb_task(void *pvParameters)
{
    /* Build full config including radio */
    esp_zigbee_config_t zb_config = {
        .device_config = {
            .device_type         = EZB_NWK_DEVICE_TYPE_ROUTER,
            .install_code_policy = INSTALLCODE_POLICY_ENABLE,
            .zczr_config = {
                .max_children = MAX_CHILDREN,
            },
        },
        .platform_config = {
            .radio_config = {
                .radio_mode = ESP_ZIGBEE_RADIO_MODE_NATIVE,
            },
        },
    };
    ESP_ERROR_CHECK(esp_zigbee_init(&zb_config));

    /* Register signal handler */
    ezb_app_signal_add_handler(esp_zb_app_signal_handler);

    /* Create and register device */
    ezb_af_device_desc_t dev_desc = esp_zb_create_sensor_device();
    ESP_ERROR_CHECK(ezb_af_device_desc_register(dev_desc));

    /* Register ZCL action handler */
    ezb_zcl_core_action_handler_register(NULL);

    /* Set primary channel mask - all channels 11-26 */
    ezb_bdb_set_primary_channel_set(0x07FFF800);

    ESP_LOGI(TAG, "Starting Zigbee router with MQ-7 sensor");
    ESP_ERROR_CHECK(esp_zigbee_start(false));

    /* Main loop - blocks until stack stops */
    ESP_ERROR_CHECK(esp_zigbee_launch_mainloop());
}

void app_main(void)
{
    /* Initialize NVS */
    ESP_ERROR_CHECK(nvs_flash_init());
    
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
