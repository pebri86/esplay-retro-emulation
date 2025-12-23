/* HTTP File Server Example
   Updated for ESP-IDF v5.5.1
*/

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_http_server.h"
#include "uri_encode.h"
#include "appfs.h"
#include "file_server.h"

/* Max length a file path can have on storage */
#if defined(CONFIG_FATFS_MAX_LFN)
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_FATFS_MAX_LFN)
#else
/* Fallback to 255 if LFN is not explicitly defined in sdkconfig, 
   plus the base VFS path length */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 255)
#endif

/* Max size of an individual file. */
#define MAX_FILE_SIZE (20 * 1024 * 1024) // 20 MB
#define MAX_FILE_SIZE_STR "20MB"

#define SCRATCH_BUFSIZE 8192
#define SPI_FLASH_ERASE_SIZE 32768

struct file_server_data
{
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
};

static const char *TAG = "file_server";

static bool SD_getFreeSpace(uint32_t *tot, uint32_t *free)
{
    FATFS *fs;
    DWORD fre_clust, fre_sect, tot_sect;

    /* Updated f_getfree for newer FatFS versions used in IDF 5.x */
    if (f_getfree("0:", &fre_clust, &fs) == FR_OK)
    {
        tot_sect = (fs->n_fatent - 2) * fs->csize;
        fre_sect = fre_clust * fs->csize;

        /* Assuming 512 bytes per sector, converting to KiB */
        *tot = tot_sect / 2;
        *free = fre_sect / 2;
        return true;
    }
    return false;
}

static int is_regular_file(const char *path)
{
    struct stat path_stat;
    if (stat(path, &path_stat) != 0) {
        return 0;
    }
    return S_ISREG(path_stat.st_mode);
}

static esp_err_t index_html_get_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "307 Temporary Redirect");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0); 
    return ESP_OK;
}

static esp_err_t favicon_get_handler(httpd_req_t *req)
{
    extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const unsigned char favicon_ico_end[] asm("_binary_favicon_ico_end");
    const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);
    return ESP_OK;
}

static esp_err_t http_resp_dir_html(httpd_req_t *req, const char *dirpath)
{
    httpd_resp_sendstr_chunk(req, "<!DOCTYPE html><html><body>");

    extern const unsigned char upload_script_start[] asm("_binary_upload_script_mini_html_start");
    extern const unsigned char upload_script_end[] asm("_binary_upload_script_mini_html_end");
    const size_t upload_script_size = (upload_script_end - upload_script_start);

    httpd_resp_send_chunk(req, (const char *)upload_script_start, upload_script_size);

    if (strcmp(dirpath, "/appfs") == 0) {
        httpd_resp_sendstr_chunk(req,
                             "<table class=\"t\">"
                             "<col width=\"800px\" /><col width=\"300px\" /><col width=\"100px\" />"
                             "<thead><tr><th>Name</th><th>Size</th><th>Action</th></tr></thead>"
                             "<tbody>");

        int fd = APPFS_INVALID_FD;
        const char *name;
        int size;
        char entrysize[16];
        char idx[10]; // Increased size for safety
        char storageSpace[100];
        while(1) {
            fd = appfsNextEntry(fd);
            if (fd == APPFS_INVALID_FD) break;
            appfsEntryInfo(fd, &name, &size);
            snprintf(entrysize, sizeof(entrysize), "%d", size);
            snprintf(idx, sizeof(idx), "%d", fd);
            
            httpd_resp_sendstr_chunk(req, "<tr><td>");
            httpd_resp_sendstr_chunk(req, name);
            httpd_resp_sendstr_chunk(req, "</td><td>");
            httpd_resp_sendstr_chunk(req, entrysize);
            httpd_resp_sendstr_chunk(req, "</td><td>");
            httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/uninstall/");
            httpd_resp_sendstr_chunk(req, idx);
            httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Uninstall</button></form>");
            httpd_resp_sendstr_chunk(req, "</td></tr>\n");
        }
        httpd_resp_sendstr_chunk(req, "</tbody></table>");
        snprintf(storageSpace, sizeof(storageSpace), "Storage : %10lu KB available.", (unsigned long)(appfsGetFreeMem() / 1024));
        httpd_resp_sendstr_chunk(req, storageSpace);

    } else {
        uint32_t btot = 0, bfree = 0;
        char entrypath[FILE_PATH_MAX];
        char entrysize[16];
        const char *entrytype;
        char storageSpace[100];

        struct dirent *entry;
        struct stat entry_stat;

        DIR *dir = opendir(dirpath);
        const size_t dirpath_len = strlen(dirpath);

        strlcpy(entrypath, dirpath, sizeof(entrypath));
        if (entrypath[dirpath_len - 1] != '/') {
            strlcat(entrypath, "/", sizeof(entrypath));
        }

        if (!dir) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Directory does not exist");
            return ESP_FAIL;
        }

        httpd_resp_sendstr_chunk(req,
                             "<table class=\"t\">"
                             "<col width=\"800px\" /><col width=\"300px\" /><col width=\"300px\" /><col width=\"100px\" />"
                             "<thead><tr><th>Name</th><th>Type</th><th>Size (Bytes)</th><th>Delete</th></tr></thead>"
                             "<tbody>");

        while ((entry = readdir(dir)) != NULL) {
            entrytype = (entry->d_type == DT_DIR ? "directory" : "file");
            
            size_t path_pos = strlen(entrypath);
            strlcpy(entrypath + path_pos, entry->d_name, sizeof(entrypath) - path_pos);
            
            if (stat(entrypath, &entry_stat) == -1) {
                continue;
            }
            snprintf(entrysize, sizeof(entrysize), "%ld", (long)entry_stat.st_size);

            httpd_resp_sendstr_chunk(req, "<tr><td><a href=\"");
            httpd_resp_sendstr_chunk(req, req->uri);
            if (req->uri[strlen(req->uri) - 1] != '/') {
                httpd_resp_sendstr_chunk(req, "/");
            }
            httpd_resp_sendstr_chunk(req, entry->d_name);
            httpd_resp_sendstr_chunk(req, "\">");
            httpd_resp_sendstr_chunk(req, entry->d_name);
            httpd_resp_sendstr_chunk(req, "</a></td><td>");
            httpd_resp_sendstr_chunk(req, entrytype);
            httpd_resp_sendstr_chunk(req, "</td><td>");
            httpd_resp_sendstr_chunk(req, entrysize);
            httpd_resp_sendstr_chunk(req, "</td><td>");
            httpd_resp_sendstr_chunk(req, "<form method=\"post\" action=\"/delete");
            httpd_resp_sendstr_chunk(req, req->uri);
            if (req->uri[strlen(req->uri) - 1] != '/') {
                httpd_resp_sendstr_chunk(req, "/");
            }
            httpd_resp_sendstr_chunk(req, entry->d_name);
            httpd_resp_sendstr_chunk(req, "\"><button type=\"submit\">Delete</button></form>");
            httpd_resp_sendstr_chunk(req, "</td></tr>\n");
            
            // Reset entrypath for next iteration
            entrypath[path_pos] = '\0';
        }
        closedir(dir);

        httpd_resp_sendstr_chunk(req, "</tbody></table>");
        SD_getFreeSpace(&btot, &bfree);
        snprintf(storageSpace, sizeof(storageSpace), "Storage : %10lu/%10lu MiB available.", (unsigned long)(bfree / 1024), (unsigned long)(btot / 1024));
        httpd_resp_sendstr_chunk(req, storageSpace);
    }

    httpd_resp_sendstr_chunk(req, "</div></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

#define IS_FILE_EXT(filename, ext) \
    (strlen(filename) >= sizeof(ext) && strcasecmp(&filename[strlen(filename) - sizeof(ext) + 1], ext) == 0)

static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filename)
{
    if (IS_FILE_EXT(filename, ".pdf")) return httpd_resp_set_type(req, "application/pdf");
    if (IS_FILE_EXT(filename, ".html")) return httpd_resp_set_type(req, "text/html");
    if (IS_FILE_EXT(filename, ".jpeg")) return httpd_resp_set_type(req, "image/jpeg");
    if (IS_FILE_EXT(filename, ".ico")) return httpd_resp_set_type(req, "image/x-icon");
    return httpd_resp_set_type(req, "text/plain");
}

static const char *get_path_from_uri(char *dest, const char *base_path, const char *uri, size_t destsize)
{
    const size_t len = strlen(uri);
    char buffer[len + 1];
    uri_decode(uri, len, buffer);

    const size_t base_pathlen = strlen(base_path);
    size_t pathlen = strlen(buffer);

    const char *quest = strchr(buffer, '?');
    if (quest) pathlen = MIN(pathlen, quest - buffer);
    const char *hash = strchr(buffer, '#');
    if (hash) pathlen = MIN(pathlen, hash - buffer);

    if (base_pathlen + pathlen + 1 > destsize) return NULL;

    strcpy(dest, base_path);
    strlcpy(dest + base_pathlen, buffer, pathlen + 1);
    return dest + base_pathlen;
}

static esp_err_t download_get_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri, sizeof(filepath));
    if (!filename) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Filename too long");
        return ESP_FAIL;
    }

    if (!is_regular_file(filepath)) {
        if (stat(filepath, &file_stat) == -1) {
            if (strcmp(filename, "/favicon.ico") == 0) return favicon_get_handler(req);
            if (strcmp(filename, "/appfs") == 0) return http_resp_dir_html(req, filename);
        } else {
            return http_resp_dir_html(req, filepath);
        }
    }

    if (stat(filepath, &file_stat) == -1) {
        if (strcmp(filename, "/index.html") == 0) return index_html_get_handler(req);
        if (strcmp(filename, "/favicon.ico") == 0) return favicon_get_handler(req);
        if (strcmp(filename, "/appfs") == 0) return http_resp_dir_html(req, filename);
        
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File does not exist");
        return ESP_FAIL;
    }

    fd = fopen(filepath, "r");
    if (!fd) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filename);
    char *chunk = ((struct file_server_data *)req->user_ctx)->scratch;
    size_t chunksize;
    do {
        chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd);
        if (httpd_resp_send_chunk(req, chunk, chunksize) != ESP_OK) {
            fclose(fd);
            httpd_resp_sendstr_chunk(req, NULL);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
            return ESP_FAIL;
        }
    } while (chunksize != 0);

    fclose(fd);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t install_apps_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    FILE *fd = NULL;
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri + sizeof("/install/") - 1, sizeof(filepath));
    
    if (!filename || filename[strlen(filename) - 1] == '/') {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File already exists");
        return ESP_FAIL;
    }

    if (req->content_len > MAX_FILE_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File size exceeds limit");
        return ESP_FAIL;
    }

    fd = fopen("/sd/temporary", "w");
    if (!fd) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create temp file");
        return ESP_FAIL;
    }

    char *buf = ((struct file_server_data *)req->user_ctx)->scratch;
    int received;
    int remaining = req->content_len;
    int filesize = req->content_len;

    while (remaining > 0) {
        if ((received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            fclose(fd);
            unlink("/sd/temporary");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Reception failed");
            return ESP_FAIL;
        }
        if (received != fwrite(buf, 1, received, fd)) {
            fclose(fd);
            unlink("/sd/temporary");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Write failed");
            return ESP_FAIL;
        }
        remaining -= received;
    }
    fclose(fd);
    
    // Process appfs installation
    fd = fopen("/sd/temporary", "r");
    if (!fd) return ESP_FAIL;

    char *chunk = calloc(SCRATCH_BUFSIZE, 1);
    appfs_handle_t fds;
    esp_err_t err = appfsCreateFile(filename, filesize, &fds);
    if (err != ESP_OK) {
        fclose(fd);
        free(chunk);
        return ESP_FAIL;
    }
	
    int address = 0;
    size_t chunksize;
    while ((chunksize = fread(chunk, 1, SCRATCH_BUFSIZE, fd)) > 0) {
        if ((address & (SPI_FLASH_ERASE_SIZE - 1)) == 0) {
            appfsErase(fds, address, SPI_FLASH_ERASE_SIZE);
        }
        appfsWrite(fds, address, (uint8_t *)chunk, chunksize);
        address += chunksize;
    }
    
    fclose(fd);
    unlink("/sd/temporary");
    free(chunk);
    
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/appfs");
    httpd_resp_sendstr(req, "File uploaded successfully");
    return ESP_OK;
}

static esp_err_t upload_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri + sizeof("/upload") - 1, sizeof(filepath));
    if (!filename || filename[strlen(filename) - 1] == '/') {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Invalid filename");
        return ESP_FAIL;
    }

    if (stat(filepath, &file_stat) == 0) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "File already exists");
        return ESP_FAIL;
    }

    FILE *fd = fopen(filepath, "w");
    if (!fd) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    char *buf = ((struct file_server_data *)req->user_ctx)->scratch;
    int received;
    int remaining = req->content_len;

    while (remaining > 0) {
        if ((received = httpd_req_recv(req, buf, MIN(remaining, SCRATCH_BUFSIZE))) <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            fclose(fd);
            unlink(filepath);
            return ESP_FAIL;
        }
        fwrite(buf, 1, received, fd);
        remaining -= received;
    }
    fclose(fd);

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File uploaded successfully");
    return ESP_OK;
}

static esp_err_t uninstall_apps_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    const char *name;
    const char *idx_str = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri + sizeof("/uninstall/") - 1, sizeof(filepath));
    if (!idx_str) return ESP_FAIL;

    int fd = atoi(idx_str);
    appfsEntryInfo(fd, &name, NULL);
    appfsDeleteFile(name);

    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/appfs");
    httpd_resp_sendstr(req, "File deleted successfully");
    return ESP_OK;
}

static esp_err_t delete_post_handler(httpd_req_t *req)
{
    char filepath[FILE_PATH_MAX];
    struct stat file_stat;

    const char *filename = get_path_from_uri(filepath, ((struct file_server_data *)req->user_ctx)->base_path,
                                             req->uri + sizeof("/delete") - 1, sizeof(filepath));
    
    if (!filename || stat(filepath, &file_stat) == -1) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File does not exist");
        return ESP_FAIL;
    }

    unlink(filepath);
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_sendstr(req, "File deleted successfully");
    return ESP_OK;
}

esp_err_t start_file_server(const char *base_path)
{
    static struct file_server_data *server_data = NULL;

    if (!base_path || strcmp(base_path, "/sd") != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (server_data) return ESP_ERR_INVALID_STATE;

    server_data = calloc(1, sizeof(struct file_server_data));
    if (!server_data) return ESP_ERR_NO_MEM;
    
    strlcpy(server_data->base_path, base_path, sizeof(server_data->base_path));

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    /* IDF 5.x specific: stack_size is now under config.stack_size */
    config.stack_size = 10240; // Increased slightly for safety in 5.5.1
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 10; // Ensure enough slots for handlers

    if (httpd_start(&server, &config) != ESP_OK) {
        free(server_data);
        return ESP_FAIL;
    }

    const httpd_uri_t uris[] = {
        { .uri = "/upload/*",    .method = HTTP_POST, .handler = upload_post_handler,    .user_ctx = server_data },
        { .uri = "/install/*",   .method = HTTP_POST, .handler = install_apps_handler,   .user_ctx = server_data },
        { .uri = "/uninstall/*", .method = HTTP_POST, .handler = uninstall_apps_handler, .user_ctx = server_data },
        { .uri = "/delete/*",    .method = HTTP_POST, .handler = delete_post_handler,    .user_ctx = server_data },
        { .uri = "/*",           .method = HTTP_GET,  .handler = download_get_handler,   .user_ctx = server_data }
    };

    for (int i = 0; i < sizeof(uris)/sizeof(uris[0]); i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }

    return ESP_OK;
}