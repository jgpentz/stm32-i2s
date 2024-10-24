#ifndef WAV_READER_H_
#define WAV_READER_H_

#include <ff.h> // FatFS headers

// WAV file header structure
typedef struct {
	char chunk_id[4]; // "RIFF"
	uint32_t chunk_size;
	char format[4];       // "WAVE"
	char subchunk1_id[4]; // "fmt "
	uint32_t subchunk1_size;
	uint16_t audio_format;
	uint16_t num_channels;
	uint32_t sample_rate;
	uint32_t byte_rate;
	uint16_t block_align;
	uint16_t bits_per_sample;
	char subchunk2_id[4];
	uint32_t subchunk2_size;
} WavHeader;

typedef struct {
	char subchunk_id[4]; // "data"
	uint32_t subchunk_size;
} SubchunkHeader;

// Function to read a WAV file
void read_wav_file(const char *file_name);

void lsdir(void);

#endif /* WAV_READER_H_ */
