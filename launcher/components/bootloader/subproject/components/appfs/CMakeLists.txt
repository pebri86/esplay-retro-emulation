set(include_dirs "include")

set(srcs 
    "appfs.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "${include_dirs}"
                    REQUIRES spi_flash bootloader_support)