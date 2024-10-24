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
#define FS_RET_OK FR_OK

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
static int cmd_g_fs_read(const struct shell *shell, size_t argc, char *argv[]);

/* ----- function definitions ----- */
void read_wav_file(const char *file_path) 
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

    // Open the WAV file
    res = f_open(&wav_file, file_path, FA_READ);
    if (res != FR_OK) {
        LOG_ERR("Failed to open file: %d", res);
        return;
    }

    // Read the WAV file header
    res = f_read(&wav_file, &wav_header, sizeof(WavHeader), &bytes_read);
    if (res != FR_OK || bytes_read != sizeof(WavHeader)) {
        LOG_ERR("Failed to read WAV header: %d", res);
        f_close(&wav_file);
        return;
    }

    // Check if it's a valid WAV file
    if (strncmp(wav_header.chunk_id, "RIFF", 4) != 0 || strncmp(wav_header.format, "WAVE", 4) != 0) {
        LOG_ERR("Invalid WAV file format");
        f_close(&wav_file);
        return;
    }

    LOG_INF("WAV File Info:");
    LOG_INF("  Sample Rate: %u Hz", wav_header.sample_rate);
    LOG_INF("  Channels: %u", wav_header.num_channels);
    LOG_INF("  Bits per Sample: %u", wav_header.bits_per_sample);

    // Read WAV data (subchunk2) here, process the audio samples as needed
    uint8_t buffer[512]; // Adjust size based on your needs
    while ((res = f_read(&wav_file, buffer, sizeof(buffer), &bytes_read)) == FR_OK && bytes_read > 0) {
        // Process the audio data in `buffer`
        LOG_INF("Read %d bytes of audio data", bytes_read);
    }

    if (res != FR_OK) {
        LOG_ERR("Error reading WAV data: %d", res);
    }

    // Close the file
    f_close(&wav_file);
}

// int main(void)
// {
// 	/* Create shell cmds for fs */
// 	SHELL_STATIC_SUBCMD_SET_CREATE(g_fs_cmds,
// 				       SHELL_CMD_ARG(read, NULL,
// 						     "Read contents from a file\n"
// 						     "usage:\n"
// 						     "$ fs read <FNAME>\n"
// 						     "example:\n"
// 						     "$ fs read test.txt\n",
// 						     cmd_g_fs_read, 2, 0),
// 				       SHELL_SUBCMD_SET_END);
// 	SHELL_CMD_REGISTER(fs, &g_fs_cmds, "Access the SD FAT filesystem", NULL);

// 	mp.mnt_point = disk_mount_pt;

// 	int res = fs_mount(&mp);

// 	if (res == FS_RET_OK) {
// 		printk("Disk mounted.\n");
// 		/* Try to unmount and remount the disk */
// 		res = fs_unmount(&mp);
// 		if (res != FS_RET_OK) {
// 			printk("Error unmounting disk\n");
// 			return res;
// 		}
// 		res = fs_mount(&mp);
// 		if (res != FS_RET_OK) {
// 			printk("Error remounting disk\n");
// 			return res;
// 		}

// 		if (lsdir(disk_mount_pt) == 0) {
// #ifdef CONFIG_FS_SAMPLE_CREATE_SOME_ENTRIES
// 			if (create_some_entries(disk_mount_pt)) {
// 				lsdir(disk_mount_pt);
// 			}
// #endif
// 		}
// 	} else {
// 		printk("Error mounting disk.\n");
// 	}

// 	fs_unmount(&mp);

// 	while (1) {
// 		k_sleep(K_MSEC(1000));
// 	}

// 	return 0;
// }

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

static int cmd_g_fs_read(const struct shell *shell, size_t argc, char *argv[])
{
	int rc;
	struct fs_dirent dirent;
	struct fs_file_t file;

	/* Combine the mount point and fname for a full path */
	const char *separator = "/";
	size_t newSize = strlen(mp.mnt_point) + strlen(separator) + strlen(argv[1]) + 1;
	char *fpath = malloc(newSize);
	if (fpath == NULL) {
		shell_fprintf(shell, SHELL_ERROR,
			      "FAIL: could not combine mount point (%s) with fname (%s)",
			      mp.mnt_point, argv[1]);
		return 1;
	}
	strcpy(fpath, mp.mnt_point);
	strcat(fpath, separator);
	strcat(fpath, argv[1]);
	shell_fprintf(shell, SHELL_NORMAL, "full filepath=%s\n", fpath);

	/* Mount the filesystem */
	res = fs_mount(&mp);
	if (res != FS_RET_OK) {
		printk("Error remounting disk\n");
		free(fpath);
		return res;
	}

	/* Open the file */
	fs_file_t_init(&file);
	rc = fs_open(&file, fpath, FS_O_READ);
	if (rc < 0) {
		shell_fprintf(shell, SHELL_ERROR, "FAIL: open %s: %d", fpath, rc);
		free(fpath);
		return rc;
	}

	/* Get file length */
	rc = fs_stat(fpath, &dirent);
	if (rc < 0) {
		shell_fprintf(shell, SHELL_ERROR, "FAIL: could not get filesize");
		free(fpath);
		fs_close(&file); // Close file if opened
		return rc;
	}
	shell_fprintf(shell, SHELL_NORMAL, "Filesize = %d\n", dirent.size);

	/* Allocate a byte array of the size of the file */
	size_t file_size = dirent.size;
	uint8_t *byte_array = malloc(file_size);
	if (byte_array == NULL) {
		shell_fprintf(shell, SHELL_ERROR,
			      "FAIL: could not allocate memory for file contents");
		free(fpath);
		fs_close(&file); // Close file if opened
		return 1;
	}

	/* Read the file into the byte array */
	rc = fs_read(&file, byte_array, file_size);
	if (rc < 0) {
		shell_fprintf(shell, SHELL_ERROR, "FAIL: read %s: [rd:%d]", fpath, rc);
		free(byte_array);
		free(fpath);
		fs_close(&file); // Close file if opened
		return rc;
	}

	shell_fprintf(shell, SHELL_NORMAL, "Read %d bytes from file.\n", rc);

	// Assuming you want to print the byte array as a string, ensure null termination
	if (file_size < UINT8_MAX) {
		byte_array[file_size] = '\0'; // Null-terminate for string display
	}

	shell_fprintf(shell, SHELL_NORMAL, "contents:\n%s\n", (char *)byte_array);

	/* Clean up */
	free(byte_array);
	fs_close(&file); // Close the file after reading
	fs_unmount(&mp);
	free(fpath);

	return 0;
}
