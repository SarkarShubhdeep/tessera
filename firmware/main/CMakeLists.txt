idf_component_register(
    SRCS "main.c" "board.c" "mic.c" "settings.c" "net.c" "claude.c"
    INCLUDE_DIRS "."
    REQUIRES tessera_ui lvgl esp_lcd esp_driver_i2c esp_driver_i2s esp_driver_gpio esp_driver_ledc esp_driver_spi esp_timer
             esp_app_format nvs_flash esp_wifi esp_netif esp_event esp_http_client mbedtls
    EMBED_TXTFILES
        "../../sim/plates/01-timer.json"
        "../../sim/plates/02-number.json"
        "../../sim/plates/04-list.json"
        "../../sim/plates/05-choice.json"
        "../../sim/plates/06-gauge.json"
        "certs/anthropic_roots.pem")
