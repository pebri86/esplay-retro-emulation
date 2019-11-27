#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h> /* chdir(), getcwd(), read(), close(), ... */

#include <file_ops.h>

// TODO: Add different comparisons and let user decide how to sort listing
/* Comparison used to sort listing entries. */
static int entry_cmp(const void *a, const void *b)
{
	int isdir1, isdir2, cmpdir;
	const Entry *r1 = a;
	const Entry *r2 = b;
	isdir1 = S_ISDIR(r1->mode);
	isdir2 = S_ISDIR(r2->mode);
	cmpdir = isdir2 - isdir1;
	return cmpdir ? cmpdir : strcoll(r1->name, r2->name);
}

/** List all entries of given cwd. */
int fops_list_dir(Entry **entriesp, const char *cwd)
{
	DIR *dp;
	struct dirent *ep;
	Entry *entries;
	int i, n;

	if (!(dp = opendir(cwd)))
		return -1;
#ifdef SIM
	n = -2; /* We don't want the entries "." and "..". */
#else
	n = 0; /* esp-idf newlib doesn't list . and .. */
#endif
	while (readdir(dp))
		n++;
	if (n == 0) {
		closedir(dp);
		return 0;
	}
	rewinddir(dp);
	entries = malloc(n * sizeof(*entries));
	i = 0;
	while ((ep = readdir(dp))) {
		// TODO: Error handling
		/* Skip "." and ".." entries */
		if (!strncmp(ep->d_name, ".", 2) || !strncmp(ep->d_name, "..", 3))
			continue;

		const size_t fname_size = strlen(ep->d_name) + 1;
		entries[i].name = malloc(fname_size);
		strncpy(entries[i].name, ep->d_name, fname_size);

		entries[i].size = 0;
		entries[i].mtime = 0;
		entries[i].mode = 0;
		if (ep->d_type == DT_REG) {
			entries[i].mode = S_IFREG;
		} else if (ep->d_type == DT_DIR) {
			entries[i].mode = S_IFDIR;
		}

		i++;
	}
	n = i; /* Ignore unused space in array caused by filters. */
	qsort(entries, n, sizeof(*entries), entry_cmp);
	closedir(dp);
	*entriesp = entries;
	return n;
}

int fops_stat_entry(Entry *entry, const char *cwd)
{
	char path[PATH_MAX];
	struct stat statbuf;
	snprintf(path, PATH_MAX, "%s/%s", cwd, entry->name);
	if (stat(path, &statbuf) != 0) {
		perror("stat");
		return -1;
	}
	entry->size = statbuf.st_size;
	entry->mode = statbuf.st_mode;
	entry->mtime = statbuf.st_mtime;
	return 0;
}

int fops_stat_entries(Entry *entries, const size_t n_entries, const char *cwd)
{
	char path[PATH_MAX];
	struct stat statbuf;
	for (size_t i = 0; i < n_entries; i++) {
		Entry *entry = &entries[i];
		snprintf(path, PATH_MAX, "%s/%s", cwd, entry->name);
		// Try to collect more info
		if (stat(path, &statbuf) != 0) {
			// TODO: How to signal error better?
			perror("stat");
			continue;
		}
		entry->size = statbuf.st_size;
		entry->mode = statbuf.st_mode;
		entry->mtime = statbuf.st_mtime;
	}
	return 0;
}

/** Free memroy from given entries. */
void fops_free_entries(Entry **entries, int n_entires)
{
	int i;

	for (i = 0; i < n_entires; i++)
		free((*entries)[i].name);
	if (n_entires > 0)
		free(*entries);
	*entries = NULL;
}

FileType fops_determine_filetype(Entry *entry)
{

	// TODO: Use regex or something else?
	const char *filename = entry->name;
	size_t len = strlen(filename);
	if (len < 4) {
		return FileTypeNone;
	}

	// Common music formats
	if (!strncasecmp("mp3", &filename[len - 3], 3)) {
		return FileTypeMP3;
	} else if (!strncasecmp("ogg", &filename[len - 3], 3)) {
		return FileTypeOGG;
	} else if (!strncasecmp("xm", &filename[len - 2], 2)) {
		return FileTypeMOD;
	} else if (!strncasecmp("mod", &filename[len - 3], 3)) {
		return FileTypeMOD;
	} else if (!strncasecmp("s3m", &filename[len - 3], 3)) {
		return FileTypeMOD;
	} else if (!strncasecmp("it", &filename[len - 2], 2)) {
		return FileTypeMOD;
	} else if (!strncasecmp("wav", &filename[len - 3], 3)) {
		return FileTypeWAV;
	} else if (!strncasecmp("flac", &filename[len - 4], 4)) {
		return FileTypeFLAC;

	// Images
	} else if (!strncasecmp("jpeg", &filename[len - 4], 4) || !strncasecmp("jpg", &filename[len - 3], 3)) {
		return FileTypeJPEG;
	} else if (!strncasecmp("png", &filename[len - 3], 3)) {
		return FileTypePNG;
	} else if (!strncasecmp("bmp", &filename[len - 3], 3)) {
		return FileTypeBMP;
	} else if (!strncasecmp("gif", &filename[len - 3], 3)) {
		return FileTypeGIF;

	// Emulators
	} else if (!strncasecmp("gb", &filename[len - 2], 2)) {
		return FileTypeGB;
	} else if (!strncasecmp("gbc", &filename[len - 3], 3)) {
		return FileTypeGBC;
	} else if (!strncasecmp("sms", &filename[len - 3], 3)) {
		return FileTypeSMS;
	} else if (!strncasecmp("nes", &filename[len - 3], 3)) {
		return FileTypeNES;
	} else if (!strncasecmp("col", &filename[len - 3], 3)) {
		return FileTypeCOL;
	} else if (!strncasecmp("gg", &filename[len - 2], 2)) {
		return FileTypeGG;

	// GME: Game music emu
	} else if (!strncasecmp("sap", &filename[len - 3], 3)) {
		return FileTypeGME;
	} else if (!strncasecmp("spc", &filename[len - 3], 3)) {
		return FileTypeGME;
	} else if (!strncasecmp("gbs", &filename[len - 3], 3)) {
		return FileTypeGME;
	} else if (!strncasecmp("ay", &filename[len - 2], 2)) {
		return FileTypeGME;
	} else if (!strncasecmp("hes", &filename[len - 3], 3)) {
		return FileTypeGME;
	} else if (!strncasecmp("kss", &filename[len - 3], 3)) {
		return FileTypeGME;
	} else if (!strncasecmp("nsf", &filename[len - 3], 3)) {
		return FileTypeGME;
	} else if (!strncasecmp("nsfe", &filename[len - 4], 4)) {
		return FileTypeGME;
	} else if (!strncasecmp("vgm", &filename[len - 4], 4)) {
		return FileTypeGME;
	} else if (!strncasecmp("vgmz", &filename[len - 4], 4)) {
		return FileTypeGME;
	}

	return FileTypeNone;
}
