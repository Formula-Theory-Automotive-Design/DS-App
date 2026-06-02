idf_component_register(
    SRCS
        "app_main.cpp"
        "uart_driver.cpp"
        "control.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        crsf-core      # shared protocol library (components/crsf-core)
        driver         # UART
        esp_timer      # esp_timer_get_time()
)
