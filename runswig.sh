CPWD=$PWD
cd ..
swig -DCONFIG_IDF_TARGET_ESP32=1 -Iesp-idf/components/esp32/ -Iesp-idf/components/esp_common/include -Iesp-idf/components/soc/include -Iesp-idf/components/soc/esp32/include -Iesp-idf/components/driver/include -Iesp-idf/components/esp_rom/include -Iesp-idf/components/driver/esp32/include -Iesp-idf/components/nvs_flash/include -I$CPWD/main -lua -o $CPWD/main/esp_wrap.c $CPWD/tools/esp.i
