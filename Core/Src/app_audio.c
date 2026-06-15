#include "app_player.h"
#include "app_sync.h"
#include "main.h"
#include "mp3dec.h"
#include <stdio.h>
#include <string.h>

extern I2S_HandleTypeDef hi2s2;
extern osSemaphoreId_t dmaSemHandle;

static uint32_t current_freq = 0;

static uint32_t App_Map_Sample_Rate(uint32_t sample_rate)
{
	if (sample_rate >= 48000) {
		return I2S_AUDIOFREQ_48K;
	}
	if (sample_rate >= 44100) {
		return I2S_AUDIOFREQ_44K;
	}
	if (sample_rate >= 32000) {
		return I2S_AUDIOFREQ_32K;
	}
	return I2S_AUDIOFREQ_44K;
}

static void App_Configure_I2S(uint32_t sample_rate)
{
	if (sample_rate != current_freq) {
		printf("Switching frequency to: %d Hz\r\n", sample_rate);
		hi2s2.Init.AudioFreq = App_Map_Sample_Rate(sample_rate);
		current_freq = sample_rate;
	}

	printf("Resetting I2S2 configuration...\r\n");
	HAL_I2S_DeInit(&hi2s2);
	if (HAL_I2S_Init(&hi2s2) != HAL_OK) {
		printf("I2S Re-Init Failed!\r\n");
		Error_Handler();
	}

	SPI2->I2SPR = 0x0010;
}

static int App_Decode_Frame(HMP3Decoder decoder, uint8_t **read_ptr,
		int *bytes_left, int16_t *target_pcm)
{
	while (*bytes_left > 0) {
		int offset = MP3FindSyncWord(*read_ptr, *bytes_left);
		if (offset < 0) {
			*bytes_left = 0;
			return 0;
		}

		*read_ptr += offset;
		*bytes_left -= offset;

		int err = MP3Decode(decoder, read_ptr, bytes_left, target_pcm, 0);
		if (err == ERR_MP3_NONE) {
			return 1;
		}

		(*read_ptr)++;
		(*bytes_left)--;
	}

	return 0;
}

static void App_Apply_Volume(int16_t *target_pcm, int sample_count)
{
	for (int i = 0; i < sample_count; i++) {
		int32_t sample = target_pcm[i];
		target_pcm[i] = (int16_t)((sample * adc_volume) >> 15);
	}
}

static void App_Pad_Pcm_Buffer(int16_t *target_pcm, int output_samples)
{
	if (output_samples < PCM_HALF_BUF_SIZE) {
		memset(target_pcm + output_samples, 0,
				(PCM_HALF_BUF_SIZE - output_samples) * sizeof(int16_t));
	}
}

static void App_Refill_Read_Buffer(uint8_t **read_ptr, int *bytes_left)
{
	UINT br = 0;
	uint8_t eof = 0;

	if (*bytes_left >= 2048) {
		return;
	}

	memmove(readBuf, *read_ptr, *bytes_left);
	*read_ptr = readBuf;

	App_FatFs_Lock();
	f_read(&fil, readBuf + *bytes_left, READ_BUF_SIZE - *bytes_left, &br);
	eof = f_eof(&fil);
	App_FatFs_Unlock();

	*bytes_left += br;
	(void)eof;
}

static uint8_t App_Is_End_Of_File(int bytes_left)
{
	uint8_t eof;

	App_FatFs_Lock();
	eof = f_eof(&fil);
	App_FatFs_Unlock();

	return eof && bytes_left < 200;
}

static void App_Skip_Track(HMP3Decoder decoder)
{
	int8_t direction;

	printf("Skipping track...\r\n");

	HAL_I2S_DMAStop(&hi2s2);
	dma_half_ready = 0;
	dma_full_ready = 0;

	MP3FreeDecoder(decoder);

	App_FatFs_Lock();
	f_close(&fil);
	App_FatFs_Unlock();

	App_Player_Lock();
	direction = track_direction;
	current_song_index += direction;
	if (current_song_index >= total_songs) {
		current_song_index = 0;
	}
	if (current_song_index < 0) {
		current_song_index = total_songs - 1;
	}
	skip_track = 0;
	file_opened = 0;
	App_Player_Unlock();

	osDelay(200);
}

static void App_End_Of_Track(HMP3Decoder decoder)
{
	printf("End of file. Switching...\r\n");

	HAL_I2S_DMAStop(&hi2s2);
	MP3FreeDecoder(decoder);

	App_FatFs_Lock();
	f_close(&fil);
	App_FatFs_Unlock();

	App_Player_Lock();
	current_song_index++;
	if (current_song_index >= total_songs) {
		current_song_index = 0;
	}
	file_opened = 0;
	App_Player_Unlock();
}

static void App_Preload_Pcm_Buffers(HMP3Decoder decoder, uint8_t **read_ptr,
		int *bytes_left)
{
	int offset = MP3FindSyncWord(*read_ptr, *bytes_left);
	if (offset >= 0) {
		*read_ptr += offset;
		*bytes_left -= offset;
		MP3Decode(decoder, read_ptr, bytes_left, pcmBuf, 0);
	}

	offset = MP3FindSyncWord(*read_ptr, *bytes_left);
	if (offset >= 0) {
		*read_ptr += offset;
		*bytes_left -= offset;
		MP3Decode(decoder, read_ptr, bytes_left,
				pcmBuf + PCM_HALF_BUF_SIZE, 0);
	}
}

static void App_Fill_Dma_Buffer(HMP3Decoder decoder, uint8_t **read_ptr,
		int *bytes_left, MP3FrameInfo *info)
{
	int16_t *target_pcm = dma_half_ready ?
			pcmBuf : (pcmBuf + PCM_HALF_BUF_SIZE);

	if (!App_Decode_Frame(decoder, read_ptr, bytes_left, target_pcm)) {
		memset(target_pcm, 0, PCM_HALF_BUF_SIZE * sizeof(int16_t));
	} else {
		MP3GetLastFrameInfo(decoder, info);
		App_Apply_Volume(target_pcm, info->outputSamps);
		App_Pad_Pcm_Buffer(target_pcm, info->outputSamps);
	}

	if (dma_half_ready) {
		dma_half_ready = 0;
	} else {
		dma_full_ready = 0;
	}
}

static void App_Playback_Loop(HMP3Decoder decoder, uint8_t *read_ptr,
		int bytes_left)
{
	MP3FrameInfo info;
	uint8_t should_skip;

	while (1) {
		App_Player_Lock();
		should_skip = skip_track;
		App_Player_Unlock();

		if (should_skip) {
			App_Skip_Track(decoder);
			return;
		}

		App_Refill_Read_Buffer(&read_ptr, &bytes_left);

		if (App_Is_End_Of_File(bytes_left)) {
			App_End_Of_Track(decoder);
			return;
		}

		if (osSemaphoreAcquire(dmaSemHandle, osWaitForever) != osOK) {
			continue;
		}

		if (dma_half_ready || dma_full_ready) {
			App_Fill_Dma_Buffer(decoder, &read_ptr, &bytes_left, &info);
		}
	}
}

void App_AudioTask_Run(void)
{
	for (;;) {
		App_FileReady_Wait();

		HMP3Decoder decoder = MP3InitDecoder();
		if (!decoder) {
			printf("MP3 decoder init failed\r\n");
			Error_Handler();
		}

		uint8_t *read_ptr = readBuf;
		int bytes_left = 0;
		UINT bytes_read = 0;

		App_FatFs_Lock();
		f_read(&fil, readBuf, READ_BUF_SIZE, &bytes_read);
		App_FatFs_Unlock();
		bytes_left = bytes_read;

		printf("Pre-loading buffers...\r\n");
		App_Preload_Pcm_Buffers(decoder, &read_ptr, &bytes_left);

		MP3FrameInfo info;
		MP3GetLastFrameInfo(decoder, &info);
		printf("Bitrate: %d, Hz: %d, Chans: %d\r\n",
				info.bitrate, info.samprate, info.nChans);

		App_Configure_I2S(info.samprate);

		dma_half_ready = 0;
		dma_full_ready = 0;

		printf("Starting I2S DMA...\r\n");
		HAL_StatusTypeDef dma_status = HAL_I2S_Transmit_DMA(&hi2s2,
				(uint16_t *)pcmBuf, PCM_BUF_SIZE);
		if (dma_status != HAL_OK) {
			printf("DMA Start Failed! Error: %d\r\n", dma_status);
			Error_Handler();
		}

		App_Playback_Loop(decoder, read_ptr, bytes_left);
	}
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
	(void)hi2s;
	dma_half_ready = 1;
	osSemaphoreRelease(dmaSemHandle);
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
	(void)hi2s;
	dma_full_ready = 1;
	osSemaphoreRelease(dmaSemHandle);
}
