idf_component_register(SRCS "app_main.cpp" "camera.cpp" "events.cpp" "network.cpp" "notify.cpp" "storage.cpp"
    INCLUDE_DIRS "." "../components/auditor/include"
    REQUIRES auditor esp32-camera nvs_flash json esp_timer driver mbedtls esp_psram
             esp_wifi esp_netif esp_event esp_http_client lwip)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
