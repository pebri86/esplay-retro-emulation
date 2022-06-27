#ifndef APPFS_H
#define APPFS_H

#include "esp_err.h"
#include <stdint.h>
#include "esp_spi_flash.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APPFS_PART_TYPE 0x43		/*<! Default partition type of an appfs partition */
#define APPFS_PART_SUBTYPE 0x3		/*<! Default partition subtype of an appfs partition */

typedef int appfs_handle_t;

#define APPFS_INVALID_FD -1			/*<! Some functions return this to indicate an error situation */

/**
 * @brief Initialize the appfs code and mount the appfs partition.
 *
 * Run this before using any of the other AppFs APIs
 *
 * @param type Partition type. Normally you'd pass APPFS_PART_TYPE here.
 * @param subtype Partition subtype. Normally you'd pass APPFS_PART_SUBTYPE here.
 * @return ESP_OK if all OK, an error from the underlying partition or flash code otherwise.
 */
esp_err_t appfsInit(int type, int subtype);

/**
 * @brief Check if a file with the given filename exists.
 *
 * @param filename Filename to check
 * @return 1 if a file with a name which exactly matches filename exists; 0 otherwise.
 */
int appfsExists(const char *filename);

/**
 * @brief Check if a file descriptor is valid
 *
 * Because file descriptors are integers which are more-or-less valid over multiple sessions, they can be stored
 * in non-volatile memory and re-used later. When doing this, a sanity check to see if the fd still
 * points at something valid may be useful. This function provides that sanity check.
 *
 * @param fd File descriptor to check
 * @return True if fd points to a valid file, false otherwise.
 */
bool appfsFdValid(int fd);

/**
 * @brief Open a file on a mounted appfs
 *
 * @param filename Filename of the file to open
 * @return The filedescriptor if succesful, APPFS_INVALID_FD if not. */
appfs_handle_t appfsOpen(const char *filename);

/**
 * @brief Close a file on a mounted appfs
 *
 * @note In the current appfs implementation, this is a no-op. This may change in the future, however.
 * @param handle File descriptor to close
 */
void appfsClose(appfs_handle_t handle);

/**
 * @brief Delete a file on the appfs
 *
 * @param filename Name of the file to delete
 * @return ESP_OK if file successfully deleted, an error otherwise.
 */
esp_err_t appfsDeleteFile(const char *filename);

/**
 * @brief Create a new file on the appfs
 *
 * Initially, the file will have random contents consisting of whatever used the sectors of
 * flash it occupies earlier. Note that this function also opens the file and returns a file
 * descriptor to it if succesful; no need for a separate appfsOpen call.
 *
 * @param filename Name of the file to be created
 * @param size Size of the file, in bytes
 * @param handle Pointer to an appfs_handle_t which will store the file descriptor of the created file
 * @return ESP_OK if file successfully deleted, an error otherwise.
 */
esp_err_t appfsCreateFile(const char *filename, size_t size, appfs_handle_t *handle);

/**
 * @brief Map a file into memory
 *
 * This maps a (portion of a) file into memory, where you can access it as if it was an array of bytes in RAM.
 * This uses the MMU and flash cache of the ESP32 to accomplish this effect. The memory is read-only; trying
 * to write to it will cause an exception.
 *
 * @param fd File descriptor of the file to map.
 * @param offset Offset into the file where the map starts
 * @param len Lenght of the map
 * @param out_ptr Pointer to a const void* variable where, if successful, a pointer to the memory is stored.
 * @param memory One of SPI_FLASH_MMAP_DATA or SPI_FLASH_MMAP_INST, where the former does a map to data memory
 *               and the latter a map to instruction memory. You'd normally use the first option.
 * @param out_handle Pointer to a spi_flash_mmap_handle_t variable. This variable is needed to later free the
 *                   map again.
 * @return ESP_OK if file successfully deleted, an error otherwise.
 */
esp_err_t appfsMmap(appfs_handle_t fd, size_t offset, size_t len, const void** out_ptr, 
									spi_flash_mmap_memory_t memory, spi_flash_mmap_handle_t* out_handle);

/**
 * @brief Unmap a previously mmap'ped file
 *
 * This unmaps a region previously mapped with appfsMmap
 *
 * @param handle Handle obtained in the previous appfsMmap call
 */
void appfsMunmap(spi_flash_mmap_handle_t handle);

/**
 * @brief Erase a portion of an appfs file
 *
 * This sets all bits in the region to be erased to 1, so an appfsWrite can reset selected bits to 0 again.
 * 
 * @param fd File descriptor of file to erase in.
 * @param start Start offset of file portion to be erased. Must be aligned to 4KiB.
 * @param len Length of file portion to be erased. Must be a multiple of 4KiB.
 * @return ESP_OK if file successfully deleted, an error otherwise.
 */
esp_err_t appfsErase(appfs_handle_t fd, size_t start, size_t len);

/**
 * @brief Write to a file in appfs
 *
 * Note: Because this maps directly to a write of the underlying flash memory, this call is only able to
 * reset bits in the written area from 1 to 0. If you want to change bits from 0 to 1, call appfsErase on
 * the area to be written before calling this function. This function will return success even if the data
 * in flash is not the same as the data in the buffer due to bits being 1 in the buffer but 0 on flash.
 *
 * If the above paragraph is confusing, just remember to erase a region before you write to it.
 *
 * @param fd File descriptor of file to write to
 * @param start Offset into file to start writing
 * @param buf Buffer of bytes to write
 * @param len Length, in bytes, of data to be written
 * @return ESP_OK if write was successful, an error otherwise.
 */
esp_err_t appfsWrite(appfs_handle_t fd, size_t start, uint8_t *buf, size_t len);

/**
 * @brief Read a portion of a file in appfs
 * 
 * This function reads ``len`` bytes of file data, starting from offset ``start``, into the buffer
 * ``buf``. Note that if you do many reads, it usually is more efficient to map the file into memory and
 * read from it that way instead.
 *
 * @param fd File descriptor of the file
 * @param start Offset in the file to start reading from
 * @param buf Buffer to contain the read data
 * @param len Length, in bytes, of the data to read
 * @return ESP_OK if read was successful, an error otherwise.
 */
esp_err_t appfsRead(appfs_handle_t fd, size_t start, void *buf, size_t len);

/**
 * @brief Atomically rename a file
 * 
 * This atomically renames a file. If a file with the target name already exists, it will be deleted. This action
 * is done atomically, so at any point in time, either the original or the new file will fully exist under the target name.
 *
 * @param from Original name of file
 * @param to Target name of file
 * @return ESP_OK if rename was successful, an error otherwise.
 */
esp_err_t appfsRename(const char *from, const char *to);

/**
 * @brief Get file information
 *
 * Given a file descriptor, this returns the name and size of the file. The file descriptor needs
 * to be valid for this to work.
 * 
 * @param fd File descriptor
 * @param name Pointer to a char pointer. This will be pointed at the filename in memory. There is no need
 *             to free the pointer memory afterwards. Pointer memory is valid while the file exists / is not 
 *             deleted. Can be NULL if name information is not wanted.
 * @param size Pointer to an int where the size of the file will be written, or NULL if this information is
 *             not wanted.
 */
void appfsEntryInfo(appfs_handle_t fd, const char **name, int *size);

/**
 * brief Get the next entry in the appfs.
 *
 * This function can be used to list all the files existing in the appfs. Pass it APPFS_INVALID_FD when
 * calling it for the first time to receive the first file descriptor. Pass it the result of the previous call
 * to get the next file descriptor. When this function returns APPFS_INVALID_FD, all files have been enumerated.
 * You can use ``appfsEntryInfo()`` to get the file name and size associated with the returned file descriptors
 *
 * @param fd File descriptor returned by previous call, or APPFS_INVALID_FD to get the first file descriptor
 * @return Next file descriptor, or APPFS_INVALID_FD if all files have been enumerated
 */
appfs_handle_t appfsNextEntry(appfs_handle_t fd);

/**
 * @brief Get file descriptor of currently running app.
 *
 * @param ret_app Pointer to variable to hold the file descriptor
 * @return ESP_OK on success, an error when e.g. the currently running code isn't located in appfs.
 */
esp_err_t appfsGetCurrentApp(appfs_handle_t *ret_app);

/**
 * @brief Get amount of free space in appfs partition
 *
 * @return amount of free space, in bytes
 */
size_t appfsGetFreeMem();

/**
 * @brief Debugging function: dump current appfs state
 *
 * Prints state of the appfs to stdout.
 */
void appfsDump();


#ifdef BOOTLOADER_BUILD
#include "bootloader_flash_priv.h"

/**
  * @brief Appfs bootloader support: struct to hold a region of a file to map
 */
typedef struct {
	uint32_t fileAddr;		/*<! Offset in file */
	uint32_t mapAddr;		/*<! Address to map to */
	uint32_t length;		/*<! Length of region */
} AppfsBlRegionToMap;

/**
 * @brief Bootloader only: initialize appfs
 *
 * @param offset Offset, in bytes, in flash of the appfs partition
 * @param len Length, in bytes, of appfs partition
 */
esp_err_t appfsBlInit(uint32_t offset, uint32_t len);

/**
 * @brief Bootloader only: de-init appfs
 */
void appfsBlDeinit();

/**
* @brief Get flash address offset of appfs file
*
* @param fd File descriptor
* @return flash address
*/
uint32_t get_appfs_file_offset(int fd);

/**
 * @brief Bootloader only: Map an entire appfs file into memory
 *
 * Note that only one file can be mapped at a time, and that between a
 * appfsBlMmap and appfsBlMunmap call, the appfs is in a state where the appfs meta information
 * is unmapped, meaning other appfs functions cannot be used.
 *
 * @param fd File descriptor to map
 * @return pointer to the mapped file
 */
void* appfsBlMmap(int fd);

/**
 * @brief Bootloader only: Un-mmap a file
 */
void appfsBlMunmap();

/*
 * @brief Bootloader only: map multiple regions within a file to various memory addressed.
 *
 * Used to load an app into memory for later execution.
 *
 * @param fd File descriptor to be mapped in
 * @param regions An array of region descriptors
 * @param noRegions Amount of regions to map
 * @return ESP_OK on success, an error when the map failed.
 */
esp_err_t appfsBlMapRegions(int fd, AppfsBlRegionToMap *regions, int noRegions);

#endif

#ifdef __cplusplus
}
#endif


#endif
