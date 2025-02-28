set(requires esp_adc openthread)

idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES "${requires}"
                       PRIV_REQUIRES nvs_flash
)