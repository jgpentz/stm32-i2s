/*
 * Copyright (c) 2019 Tavish Naruka <tavishnaruka@gmail.com>
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * Copyright (c) 2023 Antmicro <www.antmicro.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Sample which uses the filesystem API and SDHC driver */
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

#if defined(CONFIG_FAT_FILESYSTEM_ELM)

#include <ff.h>

/*
 *  Note the fatfs library is able to mount only strings inside _VOLUME_STRS
 *  in ffconf.h
 */
#define DISK_DRIVE_NAME "SD"
#define DISK_MOUNT_PT   "/" DISK_DRIVE_NAME ":"

static FATFS fat_fs;
/* mounting info */
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
};

#elif defined(CONFIG_FILE_SYSTEM_EXT2)

#include <zephyr/fs/ext2.h>

#define DISK_DRIVE_NAME "SDMMC"
#define DISK_MOUNT_PT   "/ext"

static struct fs_mount_t mp = {
	.type = FS_EXT2,
	.flags = FS_MOUNT_FLAG_NO_FORMAT,
	.storage_dev = (void *)DISK_DRIVE_NAME,
	.mnt_point = "/ext",
};

#endif

#if defined(CONFIG_FAT_FILESYSTEM_ELM)
#define FS_RET_OK FR_OK
#else
#define FS_RET_OK 0
#endif

LOG_MODULE_REGISTER(main);

#define MAX_PATH          128
#define SOME_FILE_NAME    "some.dat"
#define SOME_DIR_NAME     "some"
#define SOME_REQUIRED_LEN MAX(sizeof(SOME_FILE_NAME), sizeof(SOME_DIR_NAME))

static int lsdir(const char *path);
static int cmd_g_fs_read(const struct shell *shell, size_t argc, char *argv[]);
void aic3120Init();
#ifdef CONFIG_FS_SAMPLE_CREATE_SOME_ENTRIES
static bool create_some_entries(const char *base_path)
{
	char path[MAX_PATH];
	struct fs_file_t file;
	int base = strlen(base_path);

	fs_file_t_init(&file);

	if (base >= (sizeof(path) - SOME_REQUIRED_LEN)) {
		LOG_ERR("Not enough concatenation buffer to create file paths");
		return false;
	}

	LOG_INF("Creating some dir entries in %s", base_path);
	strncpy(path, base_path, sizeof(path));

	path[base++] = '/';
	path[base] = 0;
	strcat(&path[base], SOME_FILE_NAME);

	if (fs_open(&file, path, FS_O_CREATE) != 0) {
		LOG_ERR("Failed to create file %s", path);
		return false;
	}
	fs_close(&file);

	path[base] = 0;
	strcat(&path[base], SOME_DIR_NAME);

	if (fs_mkdir(path) != 0) {
		LOG_ERR("Failed to create dir %s", path);
		/* If code gets here, it has at least successes to create the
		 * file so allow function to return true.
		 */
	}
	return true;
}
#endif

static const char *disk_mount_pt = DISK_MOUNT_PT;

int main(void)
{
	aic3120Init();
	/* Create shell cmds for fs */
	SHELL_STATIC_SUBCMD_SET_CREATE(g_fs_cmds,
				       SHELL_CMD_ARG(read, NULL,
						     "Read contents from a file\n"
						     "usage:\n"
						     "$ fs read <FNAME>\n"
						     "example:\n"
						     "$ fs read test.txt\n",
						     cmd_g_fs_read, 2, 0),
				       SHELL_SUBCMD_SET_END);
	SHELL_CMD_REGISTER(fs, &g_fs_cmds, "Access the SD FAT filesystem", NULL);

	/* raw disk i/o */
	do {
		static const char *disk_pdrv = DISK_DRIVE_NAME;
		uint64_t memory_size_mb;
		uint32_t block_count;
		uint32_t block_size;

		if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_CTRL_INIT, NULL) != 0) {
			LOG_ERR("Storage init ERROR!");
			break;
		}

		if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_COUNT, &block_count)) {
			LOG_ERR("Unable to get sector count");
			break;
		}
		LOG_INF("Block count %u", block_count);

		if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_GET_SECTOR_SIZE, &block_size)) {
			LOG_ERR("Unable to get sector size");
			break;
		}
		printk("Sector size %u\n", block_size);

		memory_size_mb = (uint64_t)block_count * block_size;
		printk("Memory Size(MB) %u\n", (uint32_t)(memory_size_mb >> 20));

		if (disk_access_ioctl(disk_pdrv, DISK_IOCTL_CTRL_DEINIT, NULL) != 0) {
			LOG_ERR("Storage deinit ERROR!");
			break;
		}
	} while (0);

	mp.mnt_point = disk_mount_pt;

	int res = fs_mount(&mp);

	if (res == FS_RET_OK) {
		printk("Disk mounted.\n");
		/* Try to unmount and remount the disk */
		res = fs_unmount(&mp);
		if (res != FS_RET_OK) {
			printk("Error unmounting disk\n");
			return res;
		}
		res = fs_mount(&mp);
		if (res != FS_RET_OK) {
			printk("Error remounting disk\n");
			return res;
		}

		if (lsdir(disk_mount_pt) == 0) {
#ifdef CONFIG_FS_SAMPLE_CREATE_SOME_ENTRIES
			if (create_some_entries(disk_mount_pt)) {
				lsdir(disk_mount_pt);
			}
#endif
		}
	} else {
		printk("Error mounting disk.\n");
	}

	fs_unmount(&mp);

	while (1) {
		k_sleep(K_MSEC(1000));
	}

	return 0;
}

/* List dir entry by path
 *
 * @param path Absolute path to list
 *
 * @return Negative errno code on error, number of listed entries on
 *         success.
 */
static int lsdir(const char *path)
{
	int res;
	struct fs_dir_t dirp;
	static struct fs_dirent entry;
	int count = 0;

	fs_dir_t_init(&dirp);

	/* Verify fs_opendir() */
	res = fs_opendir(&dirp, path);
	if (res) {
		printk("Error opening dir %s [%d]\n", path, res);
		return res;
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

	return res;
}

static int cmd_g_fs_read(const struct shell *shell, size_t argc, char *argv[])
{
	int rc;
	struct fs_dirent dirent;
	struct fs_file_t file;
	int res;

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

#define AIC3120_I2C_ADDR 0x18 // Replace with your actual I2C address

static const struct device *i2c_dev;

static int i2c_writeRegisterByte(uint8_t reg, uint8_t value)
{
	uint8_t data[2] = {reg, value};
	int ret = i2c_write(i2c_dev, data, sizeof(data), AIC3120_I2C_ADDR);

	// Error handling
	if (ret < 0) {
		LOG_ERR("Failed to write to register 0x%02X: %d", reg, ret);
		return ret; // Return the error code
	}

	LOG_INF("Wrote to register 0x%02X: 0x%02X", reg, value); // Log successful writes
	return 0;                                                // Return success
}

void aic3120Init()
{
	// Initialize I2C device
	i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
	if (!i2c_dev) {
		LOG_ERR("Failed to get I2C device");
		return;
	}

	// 1. Define starting point:
	i2c_writeRegisterByte(0, 0x00); // Set register page to 0
	i2c_writeRegisterByte(1, 0x01); // Initiate SW reset

	// 2. Program clock settings
	i2c_writeRegisterByte(4, 0x03);  // PLL_clkin = MCLK
	i2c_writeRegisterByte(6, 0x08);  // J = 8
	i2c_writeRegisterByte(7, 0x00);  // D = 0000
	i2c_writeRegisterByte(8, 0x00);  // D(7:0) = 0
	i2c_writeRegisterByte(5, 0x91);  // Power up PLL
	i2c_writeRegisterByte(11, 0x88); // Program and power up NDAC
	i2c_writeRegisterByte(12, 0x82); // Program and power up MDAC
	i2c_writeRegisterByte(13, 0x00); // DOSR = 128
	i2c_writeRegisterByte(14, 0x80); // DOSR(7:0) = 128
	i2c_writeRegisterByte(27, 0x00); // Program I2S word length and master mode
	i2c_writeRegisterByte(60, 0x10); // Select DAC DSP Processing Block PRB_P16
	i2c_writeRegisterByte(0, 0x08);
	i2c_writeRegisterByte(1, 0x04);
	i2c_writeRegisterByte(0, 0x80);

	// 3. Program analog blocks
	i2c_writeRegisterByte(0, 0x01);  // Set register page to 1
	i2c_writeRegisterByte(31, 0x04); // Program common-mode voltage
	i2c_writeRegisterByte(33, 0x4e); // Program headphone-specific depop settings
	i2c_writeRegisterByte(35, 0x40); // Route DAC output to HPOUT
	i2c_writeRegisterByte(40, 0x06); // Unmute HPOUT, set gain = 0 dB
	i2c_writeRegisterByte(42, 0x1c); // Unmute Class-D, set gain = 18 dB
	i2c_writeRegisterByte(31, 0x82); // HPOUT powered up
	i2c_writeRegisterByte(32, 0xc6); // Power-up Class-D drivers
	i2c_writeRegisterByte(36, 0x92); // Set HPOUT output analog volume
	i2c_writeRegisterByte(38, 0x92); // Set Class-D output analog volume

	// 5. Power up DAC
	i2c_writeRegisterByte(0, 0x00);  // Power up DAC
	i2c_writeRegisterByte(63, 0x94); // Power up DAC channels and set digital gain
	i2c_writeRegisterByte(65, 0xd4); // DAC gain = -22 dB
	i2c_writeRegisterByte(64, 0x04); // Unmute DAC
}
