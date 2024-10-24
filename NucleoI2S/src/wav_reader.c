#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/fs.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/i2c.h>

#include <ff.h>
#include "wav_reader.h"

/* ----- module registers ----- */
LOG_MODULE_REGISTER(wav_reader, LOG_LEVEL_INF);

/* ----- definitions ----- */

/*
 *  Note the fatfs library is able to mount only strings inside _VOLUME_STRS
 *  in ffconf.h
 */
#define DISK_DRIVE_NAME "SD"
#define DISK_MOUNT_PT   "/" DISK_DRIVE_NAME ":"
#define FS_RET_OK       FR_OK

#define MAX_PATH          128
#define SOME_FILE_NAME    "some.dat"
#define SOME_DIR_NAME     "some"
#define SOME_REQUIRED_LEN MAX(sizeof(SOME_FILE_NAME), sizeof(SOME_DIR_NAME))

/* ----- private static variables and types ----- */
static FATFS fat_fs;
static FIL wav_file;
static FILINFO fno;
static FRESULT res;

/* mounting info */
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
};
static const char *disk_mount_pt = DISK_MOUNT_PT;

/* ----- private function declarations ----- */

/* ----- function definitions ----- */
void read_wav_file(const char *file_name)
{
	WavHeader wav_header;
	UINT bytes_read;

	// Mount the filesystem
	mp.mnt_point = disk_mount_pt;
	res = fs_mount(&mp);
	if (res != FR_OK) {
		LOG_ERR("Failed to mount filesystem: %d", res);
		return;
	}

	/* Combine the mount point and fname for a full path */
	const char *separator = "/";
	size_t newSize = strlen(mp.mnt_point) + strlen(separator) + strlen(file_name) + 1;
	char *fpath = malloc(newSize);
	if (fpath == NULL) {
		LOG_ERR("FAIL: could not combine mount point (%s) with fname (%s)", mp.mnt_point,
			file_name);
	}
	strcpy(fpath, mp.mnt_point);
	strcat(fpath, separator);
	strcat(fpath, file_name);

	// Open the WAV file
	res = fs_open(&wav_file, fpath, FS_O_READ);
	if (res != FR_OK) {
		LOG_ERR("Failed to open file: %d", res);
		free(fpath);
		return;
	}

	// Read the WAV file header
	res = fs_read(&wav_file, &wav_header, sizeof(WavHeader));
	if (res < 0) {
		LOG_ERR("Failed to read WAV header: %d", res);
		fs_close(&wav_file);
		free(fpath);
		return;
	}

	// Check if it's a valid WAV file
	if (strncmp(wav_header.chunk_id, "RIFF", 4) != 0 ||
	    strncmp(wav_header.format, "WAVE", 4) != 0) {
		LOG_ERR("Invalid WAV file format");
		fs_close(&wav_file);
		free(fpath);
		return;
	}

	LOG_INF("WAV File Info:");
	LOG_INF("  Sample Rate: %u Hz", wav_header.sample_rate);
	LOG_INF("  Channels: %u", wav_header.num_channels);
	LOG_INF("  Bits per Sample: %u", wav_header.bits_per_sample);

	// Read WAV data (subchunk2) here, process the audio samples as needed

	// If the subchunk2 is not DATA, then read past this chunk
	uint8_t buffer[512]; // Adjust size based on your needs
	if (strncmp(wav_header.subchunk2_id, "DATA", 4) != 0) {
		int32_t subchunk2_sz = wav_header.subchunk2_size;

		// Keep reading until we reach the end
		while (subchunk2_sz > 0) {
			int rd_sz = (subchunk2_sz < sizeof(buffer)) ? subchunk2_sz : sizeof(buffer);
			res = fs_read(&wav_file, buffer, rd_sz);
			if (res < 0) {
				LOG_ERR("Failed to skip past subchunk2");
				fs_close(&wav_file);
				free(fpath);
				return;
			}
			subchunk2_sz -= res;
		}
	}

	// Should be on the DATA subchunk now
	SubchunkHeader subchunk_header;
	res = fs_read(&wav_file, &subchunk_header, sizeof(subchunk_header));
	if (res < 0) {
		LOG_ERR("Error reading WAV data: %d", res);
		free(fpath);
	}

	res = fs_read(&wav_file, buffer, 12);
	if (res < 0) {
		LOG_ERR("Error reading WAV data: %d", res);
		free(fpath);
	}

	int16_t *vals = (int16_t *)buffer;
	for (int i = 0; i < 6; i++) {
		LOG_INF("vals: %d", vals[i]);
	}

	// Close the file
	fs_close(&wav_file);
	free(fpath);
}

/* List dir entry by path
 *
 * @param path Absolute path to list
 *
 * @return Negative errno code on error, number of listed entries on
 *         success.
 */
void lsdir(void)
{
	const char *path = disk_mount_pt;
	struct fs_dir_t dirp;
	static struct fs_dirent entry;
	int count = 0;

	// Mount the filesystem
	mp.mnt_point = disk_mount_pt;
	res = fs_mount(&mp);
	if (res != FR_OK) {
		LOG_ERR("Failed to mount filesystem: %d", res);
		return;
	}

	fs_dir_t_init(&dirp);

	/* Verify fs_opendir() */
	res = fs_opendir(&dirp, path);
	if (res) {
		printk("Error opening dir %s [%d]\n", path, res);
		return;
	}

	printk("\nListing dir %s ...\n", path);
	for (;;) {
		/* Verify fs_readdir() */
		res = fs_readdir(&dirp, &entry);

		/* entry.name[0] == 0 means end-of-dir */
		if (res || entry.name[0] == 0) {
			break;
		}

		if (entry.type == FS_DIR_ENTRY_DIR) {
			printk("[DIR ] %s\n", entry.name);
		} else {
			printk("[FILE] %s (size = %zu)\n", entry.name, entry.size);
		}
		count++;
	}

	/* Verify fs_closedir() */
	fs_closedir(&dirp);
	if (res == 0) {
		res = count;
	}
	fs_unmount(&mp);

	return;
}