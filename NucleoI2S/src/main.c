#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/shell/shell.h>
#include <zephyr/drivers/i2c.h>
#include <math.h>
#include <string.h>
#include "wav_reader.h"

/* ----- definitions ----- */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define AIC3120_I2C_ADDR                                                                           \
	0x18 // Replace with your actual I2C address
	     //
#define SAMPLE_FREQUENCY   44100
#define SINE_FREQ          440   // Frequency of the sine wave (440 Hz, A4)
#define AMPLITUDE          32767 // Amplitude for 16-bit audio (max value)
#define SAMPLE_BIT_WIDTH   (16U)
#define BYTES_PER_SAMPLE   sizeof(int16_t)
#define NUMBER_OF_CHANNELS (2U)

/* Such block length provides an echo with the delay of 100 ms. */
#define SAMPLES_PER_BLOCK ((SAMPLE_FREQUENCY / 40) * NUMBER_OF_CHANNELS)
#define INITIAL_BLOCKS    2
#define TIMEOUT           (2000U)

#define BLOCK_SIZE  (BYTES_PER_SAMPLE * SAMPLES_PER_BLOCK)
#define BLOCK_COUNT (INITIAL_BLOCKS + 32)
K_MEM_SLAB_DEFINE_STATIC(mem_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

/* ----- private static variables ----- */
static const struct device *dev_i2s;
static struct i2s_config i2s_cfg;
static bool stream_started = false;

static K_THREAD_STACK_DEFINE(tone_thread_stack, 1024); // Adjust stack size as needed
static struct k_thread tone_thread_data;

/* ----- private function declarations ----- */
void aic3120Init();
static bool configure_tx_streams(const struct device *dev_i2s, struct i2s_config *config);
static bool trigger_command(const struct device *dev_i2s, enum i2s_trigger_cmd cmd);

/* ----- function definitions ----- */
static void generate_sine_wave(int16_t *buffer, size_t samples_per_block, int sample_rate,
			       int frequency, int channels)
{
	static float phase = 0.0f;
	float phase_increment = 2.0f * M_PI * frequency / sample_rate;

	for (size_t i = 0; i < samples_per_block / channels; i++) {
		int16_t sample_value = (int16_t)(AMPLITUDE * sinf(phase));

		for (int ch = 0; ch < channels; ch++) {
			buffer[i * channels + ch] = sample_value;
		}

		phase += phase_increment;
		if (phase >= 2.0f * M_PI) {
			phase -= 2.0f * M_PI;
		}
	}
}

static bool configure_tx_streams(const struct device *dev_i2s, struct i2s_config *config)
{
	int ret;

	ret = i2s_configure(dev_i2s, I2S_DIR_TX, config);
	if (ret < 0) {
		printf("Failed to configure I2S stream\n");
		return false;
	}

	return true;
}

static bool trigger_command(const struct device *dev_i2s, enum i2s_trigger_cmd cmd)
{
	int ret;

	ret = i2s_trigger(dev_i2s, I2S_DIR_TX, cmd);
	if (ret < 0) {
		printk("Failed to trigger command %d on TX: %d\n", cmd, ret);
		return false;
	}

	return true;
}

void tone_thread(void *arg1, void *arg2, void *arg3)
{
	struct shell *shell = (struct shell *)arg1; // Pass shell pointer to thread
	int ret;

	bool trigger_stream = true;
	while (stream_started) {
		void *mem_block;
		int16_t *buffer;
		uint32_t block_size = BLOCK_SIZE;

		ret = k_mem_slab_alloc(&mem_slab, &mem_block, Z_TIMEOUT_TICKS(TIMEOUT));
		if (ret < 0) {
			shell_print(shell, "Failed to allocate TX block");
			break;
		}

		buffer = (int16_t *)mem_block;
		generate_sine_wave(buffer, SAMPLES_PER_BLOCK, SAMPLE_FREQUENCY, SINE_FREQ,
				   NUMBER_OF_CHANNELS);

		ret = i2s_write(dev_i2s, mem_block, block_size);
		if (ret < 0) {
			shell_print(shell, "Failed to write data: %d", ret);
			break;
		}

		if (trigger_stream) {
			ret = i2s_trigger(dev_i2s, I2S_DIR_TX, I2S_TRIGGER_START);
			if (ret < 0) {
				shell_print(shell, "Failed to start I2S stream: %d", ret);
				break;
			}
			trigger_stream =
				false; // Set false only after stream is triggered successfully
		}

		// Add a short sleep to yield control back to the OS
		k_sleep(K_MSEC(1)); // Adjust the duration as needed
	}

	if (!trigger_command(dev_i2s, I2S_TRIGGER_DROP)) {
		printk("Send I2S trigger DRAIN failed: %d", ret);
		return 0;
	}
	shell_print(shell, "thread closing down");
}

static int cmd_start_tone(const struct shell *shell, size_t argc, char **argv)
{
	if (stream_started) {
		shell_print(shell, "Tone already started");
		return 0;
	}

	shell_print(shell, "Starting tone...");
	stream_started = true;

	// Create a new thread to handle tone generation
	k_thread_create(&tone_thread_data, tone_thread_stack,
			K_THREAD_STACK_SIZEOF(tone_thread_stack), tone_thread, (void *)shell, NULL,
			NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);

	return 0;
}

// Add a command to stop the tone generation
static int cmd_stop_tone(const struct shell *shell, size_t argc, char **argv)
{
	if (!stream_started) {
		shell_print(shell, "Tone not started");
		return 0;
	}

	shell_print(shell, "Stopping tone...");
	stream_started = false; // Signal the thread to stop

	// You may need to handle joining the thread or cleanup if necessary

	return 0;
}

/* Shell command definitions */
SHELL_CMD_ARG_REGISTER(start_tone, NULL, "Start sine wave tone", cmd_start_tone, 1, 0);
SHELL_CMD_ARG_REGISTER(stop_tone, NULL, "Stop sine wave tone", cmd_stop_tone, 1, 0);

void main(void)
{
	lsdir();
	const char *fp = "lambadio.wav";
	read_wav_file(fp);
	dev_i2s = DEVICE_DT_GET(DT_NODELABEL(i2s2));

	if (!device_is_ready(dev_i2s)) {
		printk("I2S device not ready\n");
		return;
	}

	aic3120Init();          // Initialize the codec
	stream_started = false; // Ensure the stream is initially stopped

	i2s_cfg.word_size = 16U;
	i2s_cfg.channels = 2U;
	i2s_cfg.format = I2S_FMT_DATA_FORMAT_I2S;
	i2s_cfg.frame_clk_freq = SAMPLE_FREQUENCY;
	i2s_cfg.block_size = BLOCK_SIZE;
	i2s_cfg.timeout = TIMEOUT;
	i2s_cfg.options = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER;
	i2s_cfg.mem_slab = &mem_slab;

	if (!configure_tx_streams(dev_i2s, &i2s_cfg)) {
		printk("Failed to configure I2S stream\n");
	}

	k_msleep(1000); // Delay before starting

	printk("Shell initialized. Use 'start_tone' to start the tone and 'stop_tone' to stop "
	       "it.\n");
}

static int i2c_writeRegisterByte(const struct device *i2c_dev, uint8_t reg, uint8_t value)
{
	uint8_t data[2] = {reg, value};
	int ret = i2c_write(i2c_dev, data, sizeof(data), AIC3120_I2C_ADDR);

	// Error handling
	if (ret < 0) {
		printk("Failed to write to register 0x%02X: %d\n", reg, ret);
		return ret; // Return the error code
	}

	// printk("Wrote to register 0x%02X: 0x%02X\n", reg, value); // Log successful writes
	return 0; // Return success
}

void aic3120Init()
{
	// Initialize I2C device
	const struct device *i2c_dev;
	i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
	if (!i2c_dev) {
		printk("Failed to get I2C device");
		return;
	}

	// 1. Define starting point:
	i2c_writeRegisterByte(i2c_dev, 0, 0x00); // Set register page to 0
	i2c_writeRegisterByte(i2c_dev, 1, 0x01); // Initiate SW reset

	// 2. Program clock settings
	i2c_writeRegisterByte(i2c_dev, 4, 0x03);  // PLL_clkin = MCLK
	i2c_writeRegisterByte(i2c_dev, 6, 0x08);  // J = 8
	i2c_writeRegisterByte(i2c_dev, 7, 0x00);  // D = 0000
	i2c_writeRegisterByte(i2c_dev, 8, 0x00);  // D(7:0) = 0
	i2c_writeRegisterByte(i2c_dev, 5, 0x91);  // Power up PLL, P=1, R=1
						  //
	i2c_writeRegisterByte(i2c_dev, 11, 0x88); // Program and power up NDAC
	i2c_writeRegisterByte(i2c_dev, 12, 0x82); // Program and power up MDAC
	i2c_writeRegisterByte(i2c_dev, 13, 0x00); // DOSR = 128
	i2c_writeRegisterByte(i2c_dev, 14, 0x80); // DOSR(7:0) = 128
	i2c_writeRegisterByte(i2c_dev, 27, 0x00); // Program I2S word length and master mode
	i2c_writeRegisterByte(i2c_dev, 60, 0x10); // Select DAC DSP Processing Block PRB_P16
	i2c_writeRegisterByte(i2c_dev, 0, 0x08);
	i2c_writeRegisterByte(i2c_dev, 1, 0x04);
	i2c_writeRegisterByte(i2c_dev, 0, 0x80);

	// 3. Program analog blocks
	i2c_writeRegisterByte(i2c_dev, 0, 0x01);  // Set register page to 1
	i2c_writeRegisterByte(i2c_dev, 31, 0x04); // Program common-mode voltage
	i2c_writeRegisterByte(i2c_dev, 33, 0x4e); // Program headphone-specific depop settings
	i2c_writeRegisterByte(i2c_dev, 35, 0x40); // Route DAC output to HPOUT
	i2c_writeRegisterByte(i2c_dev, 40, 0x06); // Unmute HPOUT, set gain = 0 dB
	i2c_writeRegisterByte(i2c_dev, 42, 0x1c); // Unmute Class-D, set gain = 18 dB
	i2c_writeRegisterByte(i2c_dev, 31, 0x82); // HPOUT powered up
	i2c_writeRegisterByte(i2c_dev, 32, 0xc6); // Power-up Class-D drivers
	i2c_writeRegisterByte(i2c_dev, 36, 0x92); // Set HPOUT output analog volume
	i2c_writeRegisterByte(i2c_dev, 38, 0x92); // Set Class-D output analog volume

	// 5. Power up DAC
	i2c_writeRegisterByte(i2c_dev, 0, 0x00);  // Power up DAC
	i2c_writeRegisterByte(i2c_dev, 63, 0x94); // Power up DAC channels and set digital gain
	i2c_writeRegisterByte(i2c_dev, 65, 0xd4); // DAC gain = -22 dB
	i2c_writeRegisterByte(i2c_dev, 64, 0x04); // Unmute DAC
}
