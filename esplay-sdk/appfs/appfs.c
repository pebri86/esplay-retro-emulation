/*
Theory of operation:
An appfs filesystem is meant to store executable applications (=ESP32 programs) alongside other
data that is mmap()-able as a contiguous file.

Appfs does that by making sure the files rigidly adhere to the 64K-page-structure (called a 'sector'
in this description) as predicated by the ESP32s MMU. This way, every file can be mmap()'ed into a 
contiguous region or ran as an ESP32 application. (For the future, maybe: Smaller files can be stored 
in parts of a 64K page, as long as all are contiguous and none cross any 64K boundaries. 
What about fragmentation tho?)

Because of these reasons, only a few operations are available:
- Creating a file. This needs the filesize to be known beforehand; a file cannot change size afterwards.
- Modifying a file. This follows the same rules as spi_flash_*  because it maps directly to the underlying flash.
- Deleting a file
- Mmap()ping a file
This makes the interface to appfs more akin to the partition interface than to a real filesystem.

At the moment, appfs is not yet tested with encrypted flash; compatibility is unknown.

Filesystem meta-info is stored using the first sector: there are 2 32K half-sectors there with management
info. Each has a serial and a checksum. The sector with the highest serial and a matching checksum is
taken as current; the data will ping-pong between the sectors. (And yes, this means the pages in these
sectors will be rewritten every time a file is added/removed. Appfs is built with the assumption that
it's a mostly store-only filesystem and apps will only change every now and then. The flash chips 
connected to the ESP32 chips usually can do up to 100.000 erases, so for most purposes the lifetime of
the flash with appfs on it exceeds the lifetime of the product.)

Appfs assumes a partition of 16MiB or less, allowing for 256 128-byte sector descriptors to be stored in
the management half-sectors. The first descriptor is a header used for filesystem meta-info.

Metainfo is stored per sector; each sector descriptor contains a zero-terminated filename (no 
directories are supported, but '/' is an usable character), the size of the file and a pointer to the
next entry for the file if needed. The filename is only set for the first sector; it is all zeroes 
(actually: ignored) for other entries.

Integrity of the meta-info is guaranteed: the file system will never be in a state where sectors are 
lost or anything. Integrity of data is *NOT* guaranteed: on power loss, data may be half-written,
contain sectors with only 0xff, and so on. It's up to the user to take care of this. However, files that
are not written to do not run the risk of getting corrupted.

With regards to this code: it is assumed that an ESP32 will only have one appfs on flash, so everything
is implemented as a singleton.

*/

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <alloca.h>
#include <rom/crc.h>
#include "esp_spi_flash.h"
#include "esp_partition.h"
#include "esp_log.h"
#include "esp_err.h"
#include "appfs.h"
#include "rom/cache.h"
#include "sdkconfig.h"


#if !CONFIG_SPI_FLASH_WRITING_DANGEROUS_REGIONS_ALLOWED
#error "Appfs will not work with SPI flash dangerous regions checking. Please use 'make menuconfig' to enable writing to dangerous regions."
#endif

static const char *TAG = "appfs";


#define APPFS_SECTOR_SZ SPI_FLASH_MMU_PAGE_SIZE
#define APPFS_META_SZ (APPFS_SECTOR_SZ/2)
#define APPFS_META_CNT 2
#define APPFS_META_DESC_SZ 128
#define APPFS_PAGES 255
#define APPFS_MAGIC "AppFsDsc"

#define APPFS_USE_FREE 0xff		//No file allocated here
#define APPFS_ILLEGAL 0x55		//Sector cannot be used (usually because it's outside the partition)
#define APPFS_USE_DATA 0		//Sector is in use for data

typedef struct  __attribute__ ((__packed__)) {
	uint8_t magic[8]; //must be AppFsDsc
	uint32_t serial;
	uint32_t crc32;
	uint8_t reserved[128-16];
} AppfsHeader;

typedef struct  __attribute__ ((__packed__)) {
	char name[112]; //Only set for 1st sector of file. Rest has name set to 0xFF 0xFF ...
	uint32_t size; //in bytes
	uint8_t next; //next page containing the next 64K of the file; 0 if no next page (Because allocation always starts at 0 and pages can't refer to a lower page, 0 can never occur normally)
	uint8_t used; //one of APPFS_USE_*
	uint8_t reserved[10];
} AppfsPageInfo;

typedef struct  __attribute__ ((__packed__)) {
	AppfsHeader hdr;
	AppfsPageInfo page[APPFS_PAGES];
} AppfsMeta;

static int appfsActiveMeta=0; //number of currently active metadata half-sector (0 or 1)
static const AppfsMeta *appfsMeta=NULL; //mmap'ed flash
#ifndef BOOTLOADER_BUILD
static const esp_partition_t *appfsPart=NULL;
static spi_flash_mmap_handle_t appfsMetaMmapHandle;
#else
static uint32_t appfsPartOffset=0;
#endif


static int page_in_part(int page) {
#ifndef BOOTLOADER_BUILD
	return  ((page+1)*APPFS_SECTOR_SZ < appfsPart->size);
#else
	return 1;
#endif
}

//Find active meta half-sector. Updates appfsActiveMeta to the most current one and returns ESP_OK success.
//Returns ESP_ERR_NOT_FOUND when no active metasector is found.
static esp_err_t findActiveMeta() {
	int validSec=0; //bitmap of valid sectors
	uint32_t serial[APPFS_META_CNT]={0};
	AppfsHeader hdr;
	for (int sec=0; sec<APPFS_META_CNT; sec++) {
		//Read header
		memcpy(&hdr, &appfsMeta[sec].hdr, sizeof(AppfsHeader));
		if (memcmp(hdr.magic, APPFS_MAGIC, 8)==0) {
			//Save serial
			serial[sec]=hdr.serial;
			//Save and zero CRC
			uint32_t expectedCrc=hdr.crc32;
			hdr.crc32=0;
			uint32_t crc=0;
			crc=crc32_le(crc, (const uint8_t *)&hdr, APPFS_META_DESC_SZ);
			for (int j=0; j<APPFS_PAGES; j++) {
				crc=crc32_le(crc, (const uint8_t *)&appfsMeta[sec].page[j], APPFS_META_DESC_SZ);
			}
			if (crc==expectedCrc) {
				validSec|=(1<<sec);
			} else {
				ESP_LOGD(TAG, "Meta sector %d does not have a valid CRC (have %X expected %X.", sec, crc, expectedCrc);
			}
		} else {
			ESP_LOGD(TAG, "Meta sector %d does not have a valid magic header.", sec);
		}
	}
	//Here, validSec should be a bitmap of sectors that are valid, while serials[] should contain their
	//serials.
	int best=-1;
	for (int sec=0; sec<APPFS_META_CNT; sec++) {
		if (validSec&(1<<sec)) {
			if (best==-1 || serial[sec]>serial[best]) best=sec;
		}
	}

	ESP_LOGI(TAG, "Meta page 0: %svalid (serial %d)", (validSec&1)?"":"in", serial[0]);
	ESP_LOGI(TAG, "Meta page 1: %svalid (serial %d)", (validSec&2)?"":"in", serial[1]);

	//'best' here is either still -1 (no valid sector found) or the sector with the highest valid serial.
	if (best==-1) {
		ESP_LOGI(TAG, "No valid page found.");
		//Eek! Nothing found!
		return ESP_ERR_NOT_FOUND;
	} else {
		ESP_LOGI(TAG, "Using page %d as current.", best);
	}
	appfsActiveMeta=best;
	return ESP_OK;
}


static int appfsGetFirstPageFor(const char *filename) {
	for (int j=0; j<APPFS_PAGES; j++) {
		if (appfsMeta[appfsActiveMeta].page[j].used==APPFS_USE_DATA && strcmp(appfsMeta[appfsActiveMeta].page[j].name, filename)==0) {
			return j;
		}
	}
	//Nothing found.
	return APPFS_INVALID_FD;
}

bool appfsFdValid(int fd) {
	if (fd<0 || fd>=APPFS_PAGES) return false;
	if (appfsMeta[appfsActiveMeta].page[(int)fd].used!=APPFS_USE_DATA) return false;
	if (appfsMeta[appfsActiveMeta].page[(int)fd].name[0]==0xff) return false;
	return true;
}

int appfsExists(const char *filename) {
	return (appfsGetFirstPageFor(filename)==-1)?0:1;
}

appfs_handle_t appfsOpen(const char *filename) {
	return appfsGetFirstPageFor(filename);
}

void appfsClose(appfs_handle_t handle) {
	//Not needed in this implementation. Added for possible later use (concurrency?)
}

void appfsEntryInfo(appfs_handle_t fd, const char **name, int *size) {
	if (name) *name=appfsMeta[appfsActiveMeta].page[fd].name;
	if (size) *size=appfsMeta[appfsActiveMeta].page[fd].size;
}

appfs_handle_t appfsNextEntry(appfs_handle_t fd) {
	if (fd==APPFS_INVALID_FD) {
		fd=0;
	} else {
		fd++;
	}

	if (fd>=APPFS_PAGES || fd<0) return APPFS_INVALID_FD;

	while (appfsMeta[appfsActiveMeta].page[fd].used!=APPFS_USE_DATA || appfsMeta[appfsActiveMeta].page[fd].name[0]==0xff) {
		fd++;
		if (fd>=APPFS_PAGES) return APPFS_INVALID_FD;
	}

	return fd;
}

size_t appfsGetFreeMem() {
	size_t ret=0;
	for (int i=0; i<APPFS_PAGES; i++) {
		if (appfsMeta[appfsActiveMeta].page[i].used==APPFS_USE_FREE && page_in_part(i)) {
			ret+=APPFS_SECTOR_SZ;
		}
	}
	return ret;
}

#ifdef BOOTLOADER_BUILD

#include "bootloader_flash_priv.h"
#include "soc/soc.h"
#include "soc/cpu.h"
#include "soc/rtc.h"
#include "soc/dport_reg.h"
#include "esp32/rom/spi_flash.h"
#include "esp32/rom/cache.h"

esp_err_t appfsBlInit(uint32_t offset, uint32_t len) {
	//Compile-time sanity check on size of structs
	_Static_assert(sizeof(AppfsHeader)==APPFS_META_DESC_SZ, "sizeof AppfsHeader != 128bytes");
	_Static_assert(sizeof(AppfsPageInfo)==APPFS_META_DESC_SZ, "sizeof AppfsPageInfo != 128bytes");
	_Static_assert(sizeof(AppfsMeta)==APPFS_META_SZ, "sizeof AppfsMeta != APPFS_META_SZ");
	//Map meta page
	appfsMeta=bootloader_mmap(offset, APPFS_SECTOR_SZ);
	if (!appfsMeta) return ESP_ERR_NOT_FOUND;
	if (findActiveMeta()!=ESP_OK) {
		//No valid metadata half-sector found. Initialize the first sector.
		ESP_LOGE(TAG, "No valid meta info found. Bailing out.");
		return ESP_ERR_NOT_FOUND;
	}
	appfsPartOffset=offset;
	ESP_LOGD(TAG, "Initialized.");
	return ESP_OK;
}

void appfsBlDeinit() {
	bootloader_munmap(appfsMeta);
}

#define MMU_BLOCK0_VADDR  SOC_DROM_LOW
#define MMU_SIZE          (0x320000)
#define MMU_BLOCK50_VADDR (MMU_BLOCK0_VADDR + MMU_SIZE)
#define FLASH_READ_VADDR MMU_BLOCK50_VADDR

esp_err_t appfsBlMapRegions(int fd, AppfsBlRegionToMap *regions, int noRegions) {
	uint8_t pages[255];
	int pageCt=0;
	int page=fd;
	do {
		pages[pageCt++]=page;
		page=appfsMeta[appfsActiveMeta].page[page].next;
	} while (page!=0);
	//Okay, we have our info.
	bootloader_munmap(appfsMeta);
	
	Cache_Read_Disable( 0 );
	Cache_Flush( 0 );
	for (int i = 0; i < DPORT_FLASH_MMU_TABLE_SIZE; i++) {
		DPORT_PRO_FLASH_MMU_TABLE[i] = DPORT_FLASH_MMU_TABLE_INVALID_VAL;
	}
	
	for (int i=0; i<noRegions; i++) {
		uint32_t p=regions[i].fileAddr/APPFS_SECTOR_SZ;
		uint32_t d=regions[i].mapAddr&~(APPFS_SECTOR_SZ-1);
		for (uint32_t a=0; a<regions[i].length; a+=APPFS_SECTOR_SZ) {
			ESP_LOGI(TAG, "Flash mmap seg %d: %X from %X", i, d, appfsPartOffset+((pages[p]+1)*APPFS_SECTOR_SZ));
			for (int cpu=0; cpu<2; cpu++) {
				int e = cache_flash_mmu_set(cpu, 0, d, appfsPartOffset+((pages[p]+1)*APPFS_SECTOR_SZ), 64, 1);
				if (e != 0) {
					ESP_LOGE(TAG, "cache_flash_mmu_set failed for cpu %d: %d", cpu, e);
					Cache_Read_Enable(0);
					return ESP_ERR_NO_MEM;
				}
			}
			d+=APPFS_SECTOR_SZ;
			p++;
		}
	}
	DPORT_REG_CLR_BIT( DPORT_PRO_CACHE_CTRL1_REG, (DPORT_PRO_CACHE_MASK_IRAM0) | (DPORT_PRO_CACHE_MASK_IRAM1 & 0) | (DPORT_PRO_CACHE_MASK_IROM0 & 0) | DPORT_PRO_CACHE_MASK_DROM0 | DPORT_PRO_CACHE_MASK_DRAM1 );
	DPORT_REG_CLR_BIT( DPORT_APP_CACHE_CTRL1_REG, (DPORT_APP_CACHE_MASK_IRAM0) | (DPORT_APP_CACHE_MASK_IRAM1 & 0) | (DPORT_APP_CACHE_MASK_IROM0 & 0) | DPORT_APP_CACHE_MASK_DROM0 | DPORT_APP_CACHE_MASK_DRAM1 );
	Cache_Read_Enable( 0 );
	return ESP_OK;
}

uint32_t get_appfs_file_offset(int fd) {
    uint8_t pages[255];
	int pageCt=0;
	int page=fd;
	do {
		pages[pageCt++]=page;
		page=appfsMeta[appfsActiveMeta].page[page].next;
	} while (page!=0);
	ESP_LOGI(TAG, "File %d has %d pages.", fd, pageCt);
	
	if (pageCt>50) {
		ESP_LOGE(TAG, "appfsBlMmap: file too big to mmap");
		return -1;
	}
	
	bootloader_munmap(appfsMeta);
	
	return appfsPartOffset+((pages[0]+1)*APPFS_SECTOR_SZ);
}

void* appfsBlMmap(int fd) {
	//We want to mmap() the pages of the file into memory. However, to do that we need to kill the mmap for the 
	//meta info. To do this, we collect the pages before unmapping the meta info.
	uint8_t pages[255];
	int pageCt=0;
	int page=fd;
	do {
		pages[pageCt++]=page;
		page=appfsMeta[appfsActiveMeta].page[page].next;
	} while (page!=0);
	ESP_LOGI(TAG, "File %d has %d pages.", fd, pageCt);
	
	if (pageCt>50) {
		ESP_LOGE(TAG, "appfsBlMmap: file too big to mmap");
		return NULL;
	}
	
	//Okay, we have our info.
	bootloader_munmap(appfsMeta);
	//Bootloader_mmap only allows mapping of one consecutive memory range. We need more than that, so we essentially
	//replicate the function here.
	
	Cache_Read_Disable(0);
	Cache_Flush(0);
	for (int i=0; i<pageCt; i++) {
		ESP_LOGI(TAG, "Mapping flash addr %X to mem addr %X for page %d", appfsPartOffset+((pages[i]+1)*APPFS_SECTOR_SZ), MMU_BLOCK0_VADDR+(i*APPFS_SECTOR_SZ), pages[i]);
		int e = cache_flash_mmu_set(0, 0, MMU_BLOCK0_VADDR+(i*APPFS_SECTOR_SZ), 
						appfsPartOffset+((pages[i]+1)*APPFS_SECTOR_SZ), 64, 1);
		if (e != 0) {
			ESP_LOGE(TAG, "cache_flash_mmu_set failed: %d", e);
			Cache_Read_Enable(0);
			return NULL;
		}
	}
	Cache_Read_Enable(0);
	return (void *)(MMU_BLOCK0_VADDR);
}

void appfsBlMunmap() {
	/* Full MMU reset */
	Cache_Read_Disable(0);
	Cache_Flush(0);
	mmu_init(0);
	//Map meta page
	appfsMeta=bootloader_mmap(appfsPartOffset, APPFS_SECTOR_SZ);
}

#else //so if !BOOTLOADER_BUILD

//Modifies the header in hdr to the correct crc and writes it to meta info no metano.
//Assumes the serial etc is in order already, and the header section for metano has been erased.
static esp_err_t writeHdr(AppfsHeader *hdr, int metaNo) {
	hdr->crc32=0;
	uint32_t crc=0;
	crc=crc32_le(crc, (const uint8_t *)hdr, APPFS_META_DESC_SZ);
	for (int j=0; j<APPFS_PAGES; j++) {
		crc=crc32_le(crc, (const uint8_t *)&appfsMeta[metaNo].page[j], APPFS_META_DESC_SZ);
	}
	hdr->crc32=crc;
	return esp_partition_write(appfsPart, metaNo*APPFS_META_SZ, hdr, sizeof(AppfsHeader));
}

//Kill all existing filesystem metadata and re-initialize the fs.
static esp_err_t initializeFs() {
	esp_err_t r;
	//Kill management sector
	r=esp_partition_erase_range(appfsPart, 0, APPFS_SECTOR_SZ);
	if (r!=ESP_OK) return r;
	//All the data pages are now set to 'free'. Add a header that makes the entire mess valid.
	AppfsHeader hdr;
	memset(&hdr, 0xff, sizeof(hdr));
	memcpy(hdr.magic, APPFS_MAGIC, 8);
	hdr.serial=0;
	//Mark pages outside of partition as invalid.
	int lastPage=(appfsPart->size/APPFS_SECTOR_SZ);
	for (int j=lastPage; j<APPFS_PAGES; j++) {
		AppfsPageInfo pi;
		memset(&pi, 0xff, sizeof(pi));
		pi.used=APPFS_ILLEGAL;
		r=esp_partition_write(appfsPart, 0*APPFS_META_SZ+(j+1)*APPFS_META_DESC_SZ, &pi, sizeof(pi));
		if (r!=ESP_OK) return r;
	}
	writeHdr(&hdr, 0);
	ESP_LOGI(TAG, "Re-initialized appfs: %d pages", lastPage);
	//Officially, we should also write the CRCs... we don't do this here because during the
	//runtime of this, the CRCs aren't checked and when the device reboots, it'll re-initialize
	//the fs anyway.
	appfsActiveMeta=0;
	return ESP_OK;
}


esp_err_t appfsInit(int type, int subtype) {
	esp_err_t r;
	//Compile-time sanity check on size of structs
	_Static_assert(sizeof(AppfsHeader)==APPFS_META_DESC_SZ, "sizeof AppfsHeader != 128bytes");
	_Static_assert(sizeof(AppfsPageInfo)==APPFS_META_DESC_SZ, "sizeof AppfsPageInfo != 128bytes");
	_Static_assert(sizeof(AppfsMeta)==APPFS_META_SZ, "sizeof AppfsMeta != APPFS_META_SZ");
	//Find the indicated partition
	appfsPart=esp_partition_find_first(type, subtype, NULL);
	if (!appfsPart) return ESP_ERR_NOT_FOUND;
	//Memory map the appfs header so we can Do Stuff with it
	r=esp_partition_mmap(appfsPart, 0, APPFS_SECTOR_SZ, SPI_FLASH_MMAP_DATA, (const void**)&appfsMeta, &appfsMetaMmapHandle);
	if (r!=ESP_OK) return r;
	if (findActiveMeta()!=ESP_OK) {
		//No valid metadata half-sector found. Initialize the first sector.
		ESP_LOGE(TAG, "No valid meta info found. Re-initializing fs.");
		initializeFs();
	}
	ESP_LOGD(TAG, "Initialized.");
	return ESP_OK;
}


static esp_err_t writePageInfo(int newMeta, int page, AppfsPageInfo *pi) {
	return esp_partition_write(appfsPart, newMeta*APPFS_META_SZ+(page+1)*APPFS_META_DESC_SZ, pi, sizeof(AppfsPageInfo));
}


//This essentially writes a new meta page without any references to the file indicated.
esp_err_t appfsDeleteFile(const char *filename) {
	esp_err_t r;
	int next=-1;
	int newMeta;
	AppfsHeader hdr;
	AppfsPageInfo pi;
	//See if we actually need to do something
	if (!appfsExists(filename)) return 0;
	//Create a new management sector
	newMeta=(appfsActiveMeta+1)%APPFS_META_CNT;
	r=esp_partition_erase_range(appfsPart, newMeta*APPFS_META_SZ, APPFS_META_SZ);
	if (r!=ESP_OK) return r;
	//Prepare header
	memcpy(&hdr, &appfsMeta[appfsActiveMeta].hdr, sizeof(hdr));
	hdr.serial++;
	hdr.crc32=0;
	for (int j=0; j<APPFS_PAGES; j++) {
		int needDelete=0;
		//Grab old page info from current meta sector
		memcpy(&pi, &appfsMeta[appfsActiveMeta].page[j], sizeof(pi));
		if (next==-1) {
			if (pi.used==APPFS_USE_DATA && strcmp(pi.name, filename)==0) {
				needDelete=1;
				next=pi.next;
			}
		} else if (next==0) {
			//File is killed entirely. No need to look for anything.
		} else {
			//Look for next sector of file
			if (j==next) {
				needDelete=1;
				next=pi.next;
			}
		}
		if (needDelete) {
			//Page info is 0xff anyway. No need to explicitly write that.
		} else {
			r=writePageInfo(newMeta, j, &pi);
			if (r!=ESP_OK) return r;
		}
	}
	r=writeHdr(&hdr, newMeta);
	appfsActiveMeta=newMeta;
	return r;
}


//This essentially writes a new meta page with the name changed.
//If a file is found with the name, we also delete it.
esp_err_t appfsRename(const char *from, const char *to) {
	esp_err_t r;
	int newMeta;
	AppfsHeader hdr;
	AppfsPageInfo pi;
	//See if we actually need to do something
	if (!appfsExists(from)) return ESP_FAIL;
	//Create a new management sector
	newMeta=(appfsActiveMeta+1)%APPFS_META_CNT;
	r=esp_partition_erase_range(appfsPart, newMeta*APPFS_META_SZ, APPFS_META_SZ);
	if (r!=ESP_OK) return r;
	//Prepare header
	memcpy(&hdr, &appfsMeta[appfsActiveMeta].hdr, sizeof(hdr));
	hdr.serial++;
	hdr.crc32=0;
	int nextDelete=-1;
	for (int j=0; j<APPFS_PAGES; j++) {
		//Grab old page info from current meta sector
		memcpy(&pi, &appfsMeta[appfsActiveMeta].page[j], sizeof(pi));
		int needDelete=0;
		if (nextDelete==-1 && pi.used==APPFS_USE_DATA && strcmp(pi.name, to)==0) {
			//First page of the dest file. We need to delete this!
			nextDelete=pi.next;
			needDelete=1;
		} else if (nextDelete==j) {
			//A page in the file to be deleted.
			nextDelete=pi.next;
			needDelete=1;
		} else if (pi.used==APPFS_USE_DATA && strcmp(pi.name, from)==0) {
			//Found old name. Rename to new.
			strncpy(pi.name, to, sizeof(pi.name));
			pi.name[sizeof(pi.name)-1]=0;
		}
		//If hdr needs deletion, leave it at 0xfffff...
		if (!needDelete) {
			r=writePageInfo(newMeta, j, &pi);
			if (r!=ESP_OK) return r;
		}
	}
	r=writeHdr(&hdr, newMeta);
	appfsActiveMeta=newMeta;
	return r;
}


//Allocate space for a new file. Will kill any existing files if needed.
//Warning: may kill old file but not create new file if new file won't fit on fs, even with old file removed.
//ToDo: in that case, fail before deleting file.
esp_err_t appfsCreateFile(const char *filename, size_t size, appfs_handle_t *handle) {
	esp_err_t r;
	//If there are any references to this file, kill 'em.
	appfsDeleteFile(filename);
	ESP_LOGD(TAG, "Creating new file '%s'", filename);

	//Figure out what pages to reserve for the file, and the next link structure.
	uint8_t nextForPage[APPFS_PAGES]; //Next page if used for file, APPFS_PAGES if not
	//Mark all pages as unused for file.
	for (int j=0; j<APPFS_PAGES; j++) nextForPage[j]=APPFS_PAGES;
	//Find pages where we can store data for the file.
	int first=-1, prev=-1;
	int sizeLeft=size;
	for (int j=0; j<APPFS_PAGES; j++) {
		if (appfsMeta[appfsActiveMeta].page[j].used==APPFS_USE_FREE && page_in_part(j)) {
			ESP_LOGD(TAG, "Using page %d...", j);
			if (prev==-1) {
				first=j; //first free page; save to store name here.
			} else {
				nextForPage[prev]=j; //mark prev page to go here
			}
			nextForPage[j]=0; //end of file... for now.
			prev=j;
			sizeLeft-=APPFS_SECTOR_SZ;
			if (sizeLeft<=0) break;
		}
	}

	if (sizeLeft>0) {
		//Eek! Can't allocate enough space!
		ESP_LOGD(TAG, "Not enough free space!");
		return ESP_ERR_NO_MEM;
	}

	//Re-write a new meta page but with file allocated
	int newMeta=(appfsActiveMeta+1)%APPFS_META_CNT;
	ESP_LOGD(TAG, "Re-writing meta data to meta page %d...", newMeta);
	r=esp_partition_erase_range(appfsPart, newMeta*APPFS_META_SZ, APPFS_META_SZ);
	if (r!=ESP_OK) return r;
	//Prepare header
	AppfsHeader hdr;
	memcpy(&hdr, &appfsMeta[appfsActiveMeta].hdr, sizeof(hdr));
	hdr.serial++;
	hdr.crc32=0;
	for (int j=0; j<APPFS_PAGES; j++) {
		AppfsPageInfo pi;
		if (nextForPage[j]!=APPFS_PAGES) {
			//This is part of the file. Rewrite page data to indicate this.
			memset(&pi, 0xff, sizeof(pi));
			if (j==first) {
				//First page. Copy name and size.
				strcpy(pi.name, filename);
				pi.size=size;
			}
			pi.used=APPFS_USE_DATA;
			pi.next=nextForPage[j];
		} else {
			//Grab old page info from current meta sector
			memcpy(&pi, &appfsMeta[appfsActiveMeta].page[j], sizeof(pi));
		}
		if (pi.used!=APPFS_USE_FREE) {
			r=writePageInfo(newMeta, j, &pi);
			if (r!=ESP_OK) return r;
		}
	}
	//Write header and make active.
	r=writeHdr(&hdr, newMeta);
	appfsActiveMeta=newMeta;
	if (handle) *handle=first;
	ESP_LOGD(TAG, "Re-writing meta data done.");
	return r;
}

esp_err_t appfsMmap(appfs_handle_t fd, size_t offset, size_t len, const void** out_ptr, 
									spi_flash_mmap_memory_t memory, spi_flash_mmap_handle_t* out_handle) {
	esp_err_t r;
	int page=(int)fd;
	if (!appfsFdValid(page)) return ESP_ERR_NOT_FOUND;
	ESP_LOGD(TAG, "Mmapping file %s, offset %d, size %d", appfsMeta[appfsActiveMeta].page[page].name, offset, len);
	if (appfsMeta[appfsActiveMeta].page[page].size < (offset+len)) {
		ESP_LOGD(TAG, "Can't map file: trying to map byte %d in file of len %d\n", (offset+len), appfsMeta[appfsActiveMeta].page[page].size);
		return ESP_ERR_INVALID_SIZE;
	}
	int dataStartPage=(appfsPart->address/SPI_FLASH_MMU_PAGE_SIZE)+1;
	while (offset >= APPFS_SECTOR_SZ) {
		page=appfsMeta[appfsActiveMeta].page[page].next;
		offset-=APPFS_SECTOR_SZ;
		ESP_LOGD(TAG, "Skipping a page (to page %d), remaining offset 0x%X", page, offset);
	}

	int *pages=alloca(sizeof(int)*((len/APPFS_SECTOR_SZ)+1));
	int nopages=0;
	size_t mappedlen=0;
	while(len>mappedlen) {
		pages[nopages++]=page+dataStartPage;
		ESP_LOGD(TAG, "Mapping page %d (part offset %d).", page, dataStartPage);
		page=appfsMeta[appfsActiveMeta].page[page].next;
		mappedlen+=APPFS_SECTOR_SZ;
	}

	r=spi_flash_mmap_pages(pages, nopages, memory, out_ptr, out_handle);
	if (r!=ESP_OK) {
		ESP_LOGD(TAG, "Can't map file: pi_flash_mmap_pages returned %d\n", r);
		return r;
	}
	*out_ptr=((uint8_t*)*out_ptr)+offset;
	return ESP_OK;
}

void appfsMunmap(spi_flash_mmap_handle_t handle) {
	spi_flash_munmap(handle);
}

//Just mmaps and memcpys the data. Maybe not the fastest ever, but hey, if you want that you should mmap 
//and read from the flash cache memory area yourself.
esp_err_t appfsRead(appfs_handle_t fd, size_t start, void *buf, size_t len) {
	const void *flash;
	spi_flash_mmap_handle_t handle;
	esp_err_t r=appfsMmap(fd, start, len, &flash, SPI_FLASH_MMAP_DATA, &handle);
	if (r!=ESP_OK) return r;
	memcpy(buf, flash, len);
	spi_flash_munmap(handle);
	return ESP_OK;
}


esp_err_t appfsErase(appfs_handle_t fd, size_t start, size_t len) {
	esp_err_t r;
	int page=(int)fd;
	if (!appfsFdValid(page)) return ESP_ERR_NOT_FOUND;
	//Bail out if trying to erase past the file end.
	//Allow erases past the end of the file but still within the page reserved for the file.
	int roundedSize=(appfsMeta[appfsActiveMeta].page[page].size+(APPFS_SECTOR_SZ-1))&(~(APPFS_SECTOR_SZ-1));
	if (roundedSize < (start+len)) {
		return ESP_ERR_INVALID_SIZE;
	}

	//Find initial page
	while (start >= APPFS_SECTOR_SZ) {
		page=appfsMeta[appfsActiveMeta].page[page].next;
		start-=APPFS_SECTOR_SZ;
	}
	//Page now is the initial page. Start is the offset into the page we need to start at.

	while (len>0) {
		size_t size=len;
		//Make sure we do not go over a page boundary
		if ((size+start)>APPFS_SECTOR_SZ) size=APPFS_SECTOR_SZ-start;
		ESP_LOGD(TAG, "Erasing page %d offset 0x%X size 0x%X", page, start, size);
		r=esp_partition_erase_range(appfsPart, (page+1)*APPFS_SECTOR_SZ+start, size);
		if (r!=ESP_OK) return r;
		page=appfsMeta[appfsActiveMeta].page[page].next;
		len-=size;
		start=0; //offset is not needed anymore
	}
	return ESP_OK;
}

esp_err_t appfsWrite(appfs_handle_t fd, size_t start, uint8_t *buf, size_t len) {
	esp_err_t r;
	int page=(int)fd;
	if (!appfsFdValid(page)) return ESP_ERR_NOT_FOUND;
	if (appfsMeta[appfsActiveMeta].page[page].size < (start+len)) {
		return ESP_ERR_INVALID_SIZE;
	}

	while (start > APPFS_SECTOR_SZ) {
		page=appfsMeta[appfsActiveMeta].page[page].next;
		start-=APPFS_SECTOR_SZ;
	}
	while (len>0) {
		size_t size=len;
		if (size+start>APPFS_SECTOR_SZ) size=APPFS_SECTOR_SZ-start;
		ESP_LOGD(TAG, "Writing to page %d offset %d size %d", page, start, size);
		r=esp_partition_write(appfsPart, (page+1)*APPFS_SECTOR_SZ+start, buf, size);
		if (r!=ESP_OK) return r;
		page=appfsMeta[appfsActiveMeta].page[page].next;
		len-=size;
		buf+=size;
		start=0;
	}
	return ESP_OK;
}

void appfsDump() {
	printf("AppFsDump: ..=free XX=illegal no=next page\n");
	for (int i=0; i<16; i++) printf("%02X-", i);
	printf("\n");
	for (int i=0; i<APPFS_PAGES; i++) {
		if (!page_in_part(i)) {
			printf("  ");
		} else if (appfsMeta[appfsActiveMeta].page[i].used==APPFS_USE_FREE) {
			printf("..");
		} else if (appfsMeta[appfsActiveMeta].page[i].used==APPFS_USE_DATA) {
			printf("%02X", appfsMeta[appfsActiveMeta].page[i].next);
		} else if (appfsMeta[appfsActiveMeta].page[i].used==APPFS_ILLEGAL) {
			printf("XX");
		} else {
			printf("??");
		}
		if ((i&15)==15) {
			printf("\n");
		} else {
			printf(" ");
		}
	}
	printf("\n");
	for (int i=0; i<APPFS_PAGES; i++) {
		if (appfsMeta[appfsActiveMeta].page[i].used==APPFS_USE_DATA && appfsMeta[appfsActiveMeta].page[i].name[0]!=0xff) {
			printf("File %s starts at page %d\n", appfsMeta[appfsActiveMeta].page[i].name, i);
		}
	}
}


esp_err_t appfsGetCurrentApp(appfs_handle_t *ret_app) {
	//Grab offset of this function in appfs
	size_t phys_offs = spi_flash_cache2phys(appfsGetCurrentApp);
	phys_offs -= appfsPart->address;

	int page=(phys_offs/APPFS_SECTOR_SZ)-1;
	if (page<0 || page>=APPFS_PAGES) {
		return ESP_ERR_NOT_FOUND;
	}

	//Find first sector for this page.
	int tries=APPFS_PAGES; //make sure this loop always exits
	while (appfsMeta[appfsActiveMeta].page[page].name[0]==0xff) {
		int i;
		for (i=0; i<APPFS_PAGES; i++) {
			if (appfsMeta[appfsActiveMeta].page[i].next==page) {
				page=i;
				break;
			}
		}
		//See if what we have still makes sense.
		if (tries==0 || i>=APPFS_PAGES) return ESP_ERR_NOT_FOUND;
		tries--;
	}

	//Okay, found!
	*ret_app=page;
	return ESP_OK;
}



#endif