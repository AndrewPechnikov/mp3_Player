#include "app_player.h"
#include "app_sync.h"
#include "main.h"
#include <stdio.h>

extern ADC_HandleTypeDef hadc1;
extern I2S_HandleTypeDef hi2s2;

static uint32_t btn_last_press_time = 0;
static uint8_t btn_click_count = 0;
static uint8_t btn_is_pressed = 0;

void App_Volume_Poll(void)
{
	HAL_ADC_Start(&hadc1);
	if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
		adc_volume = HAL_ADC_GetValue(&hadc1);
	}
}

static void App_Handle_Button_Action(void)
{
	uint8_t opened;

	App_Player_Lock();
	opened = file_opened;

	if (btn_click_count == 1 && opened == 1) {
		is_paused = !is_paused;

		if (is_paused) {
			HAL_I2S_DMAPause(&hi2s2);
			printf("Paused\r\n");
			App_Player_Unlock();
			App_UI_RequestUpdate();
		} else {
			printf("Preparing to resume...\r\n");
			pending_resume = 1;
			App_Player_Unlock();
			App_UI_RequestUpdate();
		}
	} else if (btn_click_count == 2 && opened == 1) {
		track_direction = 1;
		skip_track = 1;
		is_paused = 0;
		printf("Next Track\r\n");
		App_Player_Unlock();
	} else if (btn_click_count >= 3 && opened == 1) {
		track_direction = -1;
		skip_track = 1;
		is_paused = 0;
		printf("Prev Track\r\n");
		App_Player_Unlock();
	} else {
		App_Player_Unlock();
	}

	btn_click_count = 0;
}

void App_Button_Poll(void)
{
	uint8_t current_state = HAL_GPIO_ReadPin(BTN_NEXT_GPIO_Port, BTN_NEXT_Pin);

	if (current_state == GPIO_PIN_SET && !btn_is_pressed) {
		btn_is_pressed = 1;
		btn_click_count++;
		btn_last_press_time = HAL_GetTick();
		osDelay(20);
	} else if (current_state == GPIO_PIN_RESET && btn_is_pressed) {
		btn_is_pressed = 0;
		osDelay(20);
	}

	if (btn_click_count > 0
			&& (HAL_GetTick() - btn_last_press_time) > MULTICLICK_TIMEOUT) {
		App_Handle_Button_Action();
	}
}
