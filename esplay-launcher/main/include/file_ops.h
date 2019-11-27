#pragma once

#include <dirent.h>
#include <stdbool.h>
#include <sys/types.h>

/** Information associated to each directory entry */
typedef struct Entry {
	char *name;   /** File name */
	off_t size;   /** File size in bytes */
	mode_t mode;  /** Filetype and permissions */
	time_t mtime; /** Modifictaion time. */
} Entry;

/** FileType enumeration used to open files */
typedef enum FileType {
	FileTypeNone,
	FileTypeFolder,
	FileTypeMP3,
	FileTypeOGG,
	FileTypeMOD,
	FileTypeWAV,
	FileTypeFLAC,
	FileTypeGME,

	FileTypeJPEG,
	FileTypePNG,
	FileTypeGIF,
	FileTypeBMP,

	FileTypeNES,
	FileTypeGB,
	FileTypeGBC,
	FileTypeSMS,
	FileTypeCOL,
	FileTypeGG,
} FileType;

/** List all entries of given cwd without fetching file properties. */
int fops_list_dir(Entry **entries, const char *cwd);

/** Fetch status information using stat for every entry. */
int fops_stat_entries(Entry *entries, const size_t n_entries, const char *cwd);

/** Fetch status information for a single entry. */
int fops_stat_entry(Entry *entries, const char *cwd);

/** Free entries. */
void fops_free_entries(Entry **entries, int n_entires);

/** Given a filename find out the file type  */
FileType fops_determine_filetype(Entry *entry);
