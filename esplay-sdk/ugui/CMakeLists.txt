set(include_dirs "")

set(srcs 
    "ugui.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "${include_dirs}")