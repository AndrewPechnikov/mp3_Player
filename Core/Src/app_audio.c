/**
 * @file app_audio.c
 * @brief MP3 decode, I2S/DMA playback, volume scaling, and track transitions.
 */

#include "app_player.h"
#include "app_sync.h"
#include "main.h"
#include "mp3dec.h"
#include <stdio.h>
#include <string.h>

extern I2S_HandleTypeDef hi2s2;
extern osSemaphoreId_t dmaSemHandle;

static uint32_t current_freq = 0;

/** Override I2SPR; HAL prescaler is wrong on this board (I2SDIV=16, ODD=0). */
static void App_Apply_I2S_Prescaler(void)
{
	SPI2->I2SPR = 0x0010U;
}

/** Clear DMA ready flags and drain pending dmaSem tokens. */
static void App_Dma_Sync_Reset(void)
{
	dma_half_ready = 0;
	dma_full_ready = 0;
	while (osSemaphoreAcquire(dmaSemHandle, 0) == osOK) {
	}
}

/** Map MP3 sample rate to nearest HAL I2S_AUDIOFREQ constant. */
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

/** Re-init I2S for the track sample rate and apply board prescaler fix. */
static void App_Configure_I2S(uint32_t sample_rate)
{
	if (sample_rate != current_freq) {
		printf("Switching frequency to: %d Hz\r\n", sample_rate);
		hi2s2.Init.AudioFreq = App_Map_Sample_Rate(sample_rate);
		current_freq = sample_rate;
	}

	HAL_I2S_DeInit(&hi2s2);
	if (HAL_I2S_Init(&hi2s2) != HAL_OK) {
		printf("I2S Re-Init Failed!\r\n");
		Error_Handler();
	}

	App_Apply_I2S_Prescaler();
}

/** Decode one MP3 frame into target_pcm; return 1 on success. */
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

/** Scale PCM samples by adc_volume (0..4095). */
static void App_Apply_Volume(int16_t *target_pcm, int sample_count)
{
	uint16_t volume = adc_volume;

	for (int i = 0; i < sample_count; i++) {
		int32_t sample = target_pcm[i];
		target_pcm[i] = (int16_t)((sample * volume) >> 15);
	}
}

/** Read more MP3 data from SD when the parse buffer runs low. */
static void App_Refill_Read_Buffer(uint8_t **read_ptr, int *bytes_left)
{
	UINT br = 0;

	if (*bytes_left >= 2048) {
		return;
	}

	memmove(readBuf, *read_ptr, *bytes_left);
	*read_ptr = readBuf;

	App_Lock();
	f_read(&fil, readBuf + *bytes_left, READ_BUF_SIZE - *bytes_left, &br);
	App_Unlock();

	*bytes_left += br;
}

/** Return 1 if file EOF and remaining MP3 bytes are exhausted. */
static uint8_t App_Is_End_Of_File(int bytes_left)
{
	uint8_t eof;

	if (bytes_left >= 200) {
		return 0;
	}

	App_Lock();
	eof = f_eof(&fil);
	App_Unlock();

	return eof != 0;
}

/** Stop DMA, close file, advance index; UI task opens the next file. */
static void App_Stop_And_Advance(int8_t direction)
{
	HAL_I2S_DMAStop(&hi2s2);
	App_Dma_Sync_Reset();

	App_Lock();
	f_close(&fil);

	current_song_index += direction;
	if (current_song_index >= total_songs) {
		current_song_index = 0;
	}
	if (current_song_index < 0) {
		current_song_index = total_songs - 1;
	}

	file_opened = 0;
	App_Unlock();

	osDelay(200);
}

/** Decode initial MP3 frames into both PCM half-buffers before DMA start. */
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

/** Fill one DMA half-buffer with decoded (and volume-scaled) PCM. */
static void App_Fill_Dma_Buffer(HMP3Decoder decoder, uint8_t **read_ptr,
		int *bytes_left, MP3FrameInfo *info)
{
	int16_t *target_pcm;
	int samples_filled = 0;
	uint8_t half_ready = dma_half_ready;
	uint8_t full_ready = dma_full_ready;

	if (half_ready) {
		target_pcm = pcmBuf;
		dma_half_ready = 0;
	} else if (full_ready) {
		target_pcm = pcmBuf + PCM_HALF_BUF_SIZE;
		dma_full_ready = 0;
	} else {
		return;
	}

	while (samples_filled < PCM_HALF_BUF_SIZE && *bytes_left > 0) {
		int16_t *frame_dst = target_pcm + samples_filled;

		if (!App_Decode_Frame(decoder, read_ptr, bytes_left, frame_dst)) {
			break;
		}

		MP3GetLastFrameInfo(decoder, info);
		App_Apply_Volume(frame_dst, info->outputSamps);
		samples_filled += info->outputSamps;
	}

	if (samples_filled < PCM_HALF_BUF_SIZE) {
		memset(target_pcm + samples_filled, 0,
				(PCM_HALF_BUF_SIZE - samples_filled) * sizeof(int16_t));
	}
}

/** Process all pending DMA half/full complete events in one pass. */
static void App_Service_Dma_Buffers(HMP3Decoder decoder, uint8_t **read_ptr,
		int *bytes_left, MP3FrameInfo *info)
{
	while (dma_half_ready || dma_full_ready) {
		App_Fill_Dma_Buffer(decoder, read_ptr, bytes_left, info);
	}
}

/** Handle one command from cmdQueue; return 1 to exit playback loop. */
static uint8_t App_Handle_Cmd(PlayerCmd_t cmd, HMP3Decoder decoder)
{
	switch (cmd) {
	case PLAYER_CMD_PAUSE:
		HAL_I2S_DMAPause(&hi2s2);
		break;
	case PLAYER_CMD_RESUME:
		HAL_I2S_DMAResume(&hi2s2);
		break;
	case PLAYER_CMD_NEXT:
		printf("Skipping track...\r\n");
		MP3FreeDecoder(decoder);
		App_Stop_And_Advance(1);
		return 1;
	case PLAYER_CMD_PREV:
		printf("Skipping track...\r\n");
		MP3FreeDecoder(decoder);
		App_Stop_And_Advance(-1);
		return 1;
	default:
		break;
	}
	return 0;
}

/** Main decode loop: wait for DMA, refill buffers, handle commands/EOF. */
static void App_Playback_Loop(HMP3Decoder decoder, uint8_t *read_ptr,
		int bytes_left)
{
	MP3FrameInfo info;
	PlayerCmd_t cmd;

	while (1) {
		while (App_WaitCmd(&cmd, 0) == osOK) {
			if (App_Handle_Cmd(cmd, decoder)) {
				return;
			}
		}

		if (osSemaphoreAcquire(dmaSemHandle, osWaitForever) != osOK) {
			continue;
		}

		App_Service_Dma_Buffers(decoder, &read_ptr, &bytes_left, &info);
		App_Refill_Read_Buffer(&read_ptr, &bytes_left);

		if (App_Is_End_Of_File(bytes_left)) {
			printf("End of file. Switching...\r\n");
			MP3FreeDecoder(decoder);
			App_Stop_And_Advance(1);
			return;
		}
	}
}

/** AudioTask entry: wait for START, decode, and stream PCM over I2S DMA. */
void App_AudioTask_Run(void)
{
	PlayerCmd_t cmd;

	for (;;) {
		/* Block until UI opens a file and sends START. */
		do {
			if (App_WaitCmd(&cmd, osWaitForever) != osOK) {
				continue;
			}
		} while (cmd != PLAYER_CMD_START);

		HMP3Decoder decoder = MP3InitDecoder();
		if (!decoder) {
			printf("MP3 decoder init failed\r\n");
			Error_Handler();
		}

		uint8_t *read_ptr = readBuf;
		int bytes_left = 0;
		UINT bytes_read = 0;

		App_Lock();
		f_read(&fil, readBuf, READ_BUF_SIZE, &bytes_read);
		App_Unlock();
		bytes_left = bytes_read;

		printf("Pre-loading buffers...\r\n");
		App_Preload_Pcm_Buffers(decoder, &read_ptr, &bytes_left);

		MP3FrameInfo info;
		MP3GetLastFrameInfo(decoder, &info);
		printf("Bitrate: %d, Hz: %d, Chans: %d\r\n",
				info.bitrate, info.samprate, info.nChans);

		App_Configure_I2S(info.samprate);
		App_Dma_Sync_Reset();

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

/** DMA half-transfer complete: first PCM half is free to fill. */
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
	(void)hi2s;
	dma_half_ready = 1;
	osSemaphoreRelease(dmaSemHandle);
}

/** DMA transfer complete: second PCM half is free to fill. */
void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
	(void)hi2s;
	dma_full_ready = 1;
	osSemaphoreRelease(dmaSemHandle);
}
