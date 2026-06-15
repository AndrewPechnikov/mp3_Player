#include "app_player.h"
#include "app_sync.h"
#include "main.h"
#include "ssd1306.h"
#include "fonts.h"
#include <stdio.h>
#include <string.h>

extern I2S_HandleTypeDef hi2s2;

void App_Display_Init(void)
{
	SSD1306_Init();
	SSD1306_Clear();
	SSD1306_Fill(0);
	SSD1306_UpdateScreen();
	osDelay(100);
}

static void App_Update_Scroll_Window(void)
{
	App_Player_Lock();
	if (current_song_index < window_start) {
		window_start = current_song_index;
	} else if (current_song_index >= window_start + MAX_LINES) {
		window_start = current_song_index - MAX_LINES + 1;
	}
	App_Player_Unlock();
}

static void App_Draw_Playlist_Screen(void)
{
	uint8_t paused;
	int song_index;
	int scroll_start;

	App_Player_Lock();
	paused = is_paused;
	song_index = current_song_index;
	scroll_start = window_start;
	App_Player_Unlock();

	SSD1306_Clear();
	SSD1306_GotoXY(0, 0);
	SSD1306_Puts(" -PLAYLIST- ", &Font_11x18, SSD1306_COLOR_WHITE);

	int lines_to_draw = (total_songs < MAX_LINES) ? total_songs : MAX_LINES;
	char display_buf[20];

	for (int i = 0; i < lines_to_draw; i++) {
		int actual_index = scroll_start + i;
		SSD1306_GotoXY(0, 18 + (i * 11));

		strncpy(display_buf, playlist[actual_index], 15);
		display_buf[15] = '\0';

		for (int k = 0; display_buf[k] != '\0'; k++) {
			if ((unsigned char)display_buf[k] < 32
					|| (unsigned char)display_buf[k] > 126) {
				display_buf[k] = '?';
			}
		}

		if (actual_index == song_index) {
			if (paused) {
				SSD1306_Puts("||", &Font_7x10, SSD1306_COLOR_WHITE);
			} else {
				SSD1306_Puts("> ", &Font_7x10, SSD1306_COLOR_WHITE);
			}
		} else {
			SSD1306_Puts("  ", &Font_7x10, SSD1306_COLOR_WHITE);
		}

		SSD1306_Puts(display_buf, &Font_7x10, SSD1306_COLOR_WHITE);
	}

	SSD1306_UpdateScreen();
	osDelay(50);
}

void App_UI_Update(void)
{
	uint8_t needs_update;
	uint8_t resume_pending;

	if (total_songs == 0) {
		App_Player_Lock();
		needs_update = force_ui_update;
		if (needs_update) {
			force_ui_update = 0;
		}
		App_Player_Unlock();

		if (needs_update) {
			SSD1306_Clear();
			SSD1306_GotoXY(0, 20);
			SSD1306_Puts("No MP3 Found!", &Font_7x10, SSD1306_COLOR_WHITE);
			SSD1306_UpdateScreen();
		}
		return;
	}

	App_Update_Scroll_Window();

	App_Player_Lock();
	needs_update = force_ui_update;
	if (!needs_update) {
		App_Player_Unlock();
		return;
	}
	force_ui_update = 0;
	App_Player_Unlock();

	App_Draw_Playlist_Screen();

	uint8_t was_opened;

	App_Player_Lock();
	was_opened = file_opened;
	file_opened = 1;
	resume_pending = pending_resume;
	if (resume_pending) {
		pending_resume = 0;
	}
	App_Player_Unlock();

	if (!was_opened) {
		App_FileReady_Signal();
	}

	if (resume_pending) {
		HAL_I2S_DMAResume(&hi2s2);
		printf("Resumed\r\n");
	}
}
