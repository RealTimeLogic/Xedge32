function installswig {
    read -p "SWIG is not installed. Do you want to install SWIG (Y/n)?"
    if [ "$REPLY" == "n" ]; then
        return
    fi
    sudo apt -y install swig
}
command -v swig >/dev/null 2>&1 || installswig

pushd ~/esp/esp-idf || abort
IDFP=$PWD
popd

swig -DCONFIG_IDF_TARGET_ESP32=1 -I$IDFP/components/esp32/ -I$IDFP/components/esp_common/include -I$IDFP/components/hal/include -I$IDFP/components/hal/include/hal -I$IDFP/components/soc/include -I$IDFP/components/soc/esp32/include -I$IDFP/components/driver/include -I$IDFP/components/driver/include/driver -I$IDFP/components/esp_rom/include -I$IDFP/components/driver/esp32/include -I$IDFP/components/nvs_flash/include -Imain -lua -o main/esp_wrap.c tools/esp.i
