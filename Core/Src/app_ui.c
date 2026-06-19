/**
 * @file app_ui.c
 * @brief SSD1306 playlist display, scroll window, and playback status icons.
 */

#include "app_player.h"
#include "app_sync.h"
#include "main.h"
#include "ssd1306.h"
#include "fonts.h"
#include <stdio.h>
#include <string.h>

/** Bring up OLED and show a blank screen. */
void App_Display_Init(void)
{
	SSD1306_Init();
	SSD1306_Clear();
	SSD1306_Fill(0);
	SSD1306_UpdateScreen();
	osDelay(100);
}

/** Keep current track visible within the 4-line window. */
static void App_Update_Scroll_Window(void)
{
	App_Lock();
	if (current_song_index < window_start) {
		window_start = current_song_index;
	} else if (current_song_index >= window_start + MAX_LINES) {
		window_start = current_song_index - MAX_LINES + 1;
	}
	App_Unlock();
}

/** Render playlist header, track list, and play/pause marker. */
static void App_Draw_Playlist_Screen(void)
{
	uint8_t paused;
	int song_index;
	int scroll_start;

	App_Lock();
	paused = is_paused;
	song_index = current_song_index;
	scroll_start = window_start;
	App_Unlock();

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

/** Update screen when requested; send START/RESUME commands to audio. */
void App_UI_Update(void)
{
	uint8_t needs_update;
	uint8_t send_start = 0;
	uint8_t send_resume = 0;

	if (total_songs == 0) {
		App_Lock();
		needs_update = force_ui_update;
		if (needs_update) {
			force_ui_update = 0;
		}
		App_Unlock();

		if (needs_update) {
			SSD1306_Clear();
			SSD1306_GotoXY(0, 20);
			SSD1306_Puts("No MP3 Found!", &Font_7x10, SSD1306_COLOR_WHITE);
			SSD1306_UpdateScreen();
		}
		return;
	}

	App_Update_Scroll_Window();

	App_Lock();
	needs_update = force_ui_update;
	if (!needs_update) {
		App_Unlock();
		return;
	}
	force_ui_update = 0;
	App_Unlock();

	App_Draw_Playlist_Screen();

	App_Lock();
	if (file_opened == 0) {
		file_opened = 1;
		send_start = 1;
	} else if (!is_paused) {
		send_resume = 1;
	}
	App_Unlock();

	if (send_start) {
		App_SendCmd(PLAYER_CMD_START);
		printf("Play started\r\n");
	} else if (send_resume) {
		App_SendCmd(PLAYER_CMD_RESUME);
		printf("Resumed\r\n");
	}
}
