set(src_dirs
            MYIIC
            LCD
            MYSPI
            XL9555
            KEY
            SPI_SD
            TOUCH
            RTC
			MYFATFS
            USART
			)

set(include_dirs
            MYIIC
            LCD
            MYSPI
            XL9555
            KEY
            SPI_SD
            TOUCH
            RTC
			MYFATFS
            USART
			)
            
set(requires
            driver
            esp_lcd
            fatfs
            newlib
            esp_timer
            nvs_flash
            lwip)

idf_component_register(SRC_DIRS ${src_dirs} INCLUDE_DIRS ${include_dirs} REQUIRES ${requires})

component_compile_options(-ffast-math -O3 -Wno-error=format)
