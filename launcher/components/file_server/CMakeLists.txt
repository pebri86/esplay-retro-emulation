set(include_dirs "include")

set(srcs 
    "uri_encode.c"
    "file_server.c")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "${include_dirs}"
                    EMBED_FILES "favicon.ico"
                    EMBED_FILES "upload_script.mini.html"
                    REQUIRES esp_http_server vfs fatfs appfs)