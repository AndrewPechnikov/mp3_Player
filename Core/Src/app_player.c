#include "app_player.h"
#include "app_sync.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

char playlist[MAX_SONGS][80];
int total_songs = 0;
int current_song_index = 0;
volatile uint8_t file_opened = 0;
volatile uint16_t adc_volume = 4095;
volatile uint8_t skip_track = 0;
volatile int8_t track_direction = 1;
volatile uint8_t is_paused = 0;
uint8_t force_ui_update = 0;
uint8_t pending_resume = 0;
int window_start = 0;

FATFS fs;
FIL fil;
FRESULT fres;

uint8_t readBuf[READ_BUF_SIZE];
int16_t pcmBuf[PCM_BUF_SIZE];
volatile uint8_t dma_half_ready = 0;
volatile uint8_t dma_full_ready = 0;

void App_FS_Init(void)
{
	App_FatFs_Lock();
	fres = f_mount(&fs, "", 1);
	App_FatFs_Unlock();

	if (fres != FR_OK) {
		printf("Mount error: %d\r\n", fres);
		Error_Handler();
	}
}

FRESULT App_ScanFiles(char *path)
{
	FRESULT res;
	DIR dir;
	FILINFO fno;

	App_FatFs_Lock();
	res = f_opendir(&dir, path);
	App_FatFs_Unlock();

	if (res != FR_OK) {
		return res;
	}

	while (1) {
		App_FatFs_Lock();
		res = f_readdir(&dir, &fno);
		App_FatFs_Unlock();

		if (res != FR_OK || fno.fname[0] == 0) {
			break;
		}

		if (fno.fattrib & AM_DIR) {
			continue;
		}

		char *ext = strrchr(fno.fname, '.');
		if (!ext || strcasecmp(ext, ".mp3") != 0) {
			continue;
		}

		if (total_songs >= MAX_SONGS) {
			break;
		}

		strcpy(playlist[total_songs], fno.fname);
		printf("Found song: %s\r\n", playlist[total_songs]);
		total_songs++;
	}

	App_FatFs_Lock();
	f_closedir(&dir);
	App_FatFs_Unlock();
	return res;
}

void App_Open_Next_Song(void)
{
	uint8_t opened;
	uint8_t ui_pending;

	App_Player_Lock();
	opened = file_opened;
	ui_pending = force_ui_update;
	App_Player_Unlock();

	if (opened != 0 || total_songs == 0 || ui_pending != 0) {
		return;
	}

	printf("Loading track: %s\r\n", playlist[current_song_index]);

	App_FatFs_Lock();
	fres = f_open(&fil, playlist[current_song_index], FA_READ);
	App_FatFs_Unlock();

	if (fres == FR_OK) {
		App_UI_RequestUpdate();
		return;
	}

	printf("Error %d: Cannot open file. Skipping track...\r\n", fres);

	App_Player_Lock();
	current_song_index += track_direction;
	if (current_song_index >= total_songs) {
		current_song_index = 0;
	} else if (current_song_index < 0) {
		current_song_index = total_songs - 1;
	}
	App_Player_Unlock();

	osDelay(100);
}

void App_DefaultTask_Init(void)
{
	App_Display_Init();
	printf("\r\n--- SD Card & OLED Test (FreeRTOS) ---\r\n");

	App_FS_Init();
	App_ScanFiles("/");

	if (total_songs == 0) {
		printf("No MP3 files found!\r\n");
		App_UI_RequestUpdate();
		return;
	}

	printf("Found %d songs. Preparing UI and opening file...\r\n", total_songs);

	App_FatFs_Lock();
	fres = f_open(&fil, playlist[current_song_index], FA_READ);
	App_FatFs_Unlock();

	if (fres != FR_OK) {
		printf("File open error: %d\r\n", fres);
		Error_Handler();
	}

	printf("File opened OK\r\n");
	App_UI_RequestUpdate();
}

void App_DefaultTask_Loop(void)
{
	App_Volume_Poll();
	App_Button_Poll();
	App_Open_Next_Song();

	(void)osSemaphoreAcquire(uiSemHandle, APP_UI_POLL_MS);
	App_UI_Update();
}
