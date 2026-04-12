idf_component_register(
    SRCS "zigbee_router_mq7.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_zigbee_lib driver esp_adc
)
