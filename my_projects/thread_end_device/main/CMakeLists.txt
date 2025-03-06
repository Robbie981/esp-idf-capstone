set(requires esp_adc openthread bme68x_lib)
set(priv_requires nvs_flash json)

idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES "${requires}"
                       PRIV_REQUIRES "${priv_requires}"
)