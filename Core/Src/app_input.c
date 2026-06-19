/**
 * @file app_input.c
 * @brief User input: volume potentiometer (ADC) and multi-click button.
 */

#include "app_player.h"
#include "app_sync.h"
#include "main.h"
#include <stdio.h>

extern ADC_HandleTypeDef hadc1;

static uint32_t btn_last_press_time = 0;
static uint8_t btn_click_count = 0;
static uint8_t btn_is_pressed = 0;

/** Sample ADC and store raw value in adc_volume. */
void App_Volume_Poll(void)
{
	HAL_ADC_Start(&hadc1);
	if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
		adc_volume = HAL_ADC_GetValue(&hadc1);
	}
}

/** Execute action after multi-click timeout (1=pause, 2=next, 3+=prev). */
static void App_Handle_Button_Action(void)
{
	uint8_t opened;

	App_Lock();
	opened = file_opened;

	if (btn_click_count == 1 && opened == 1) {
		if (is_paused) {
			is_paused = 0;
			App_Unlock();
			App_UI_RequestUpdate();
			/* Resume DMA after UI redraw (see App_UI_Update). */
		} else {
			is_paused = 1;
			App_Unlock();
			App_SendCmd(PLAYER_CMD_PAUSE);
			App_UI_RequestUpdate();
			printf("Paused\r\n");
		}
	} else if (btn_click_count == 2 && opened == 1) {
		is_paused = 0;
		App_Unlock();
		App_SendCmd(PLAYER_CMD_NEXT);
		printf("Next Track\r\n");
	} else if (btn_click_count >= 3 && opened == 1) {
		is_paused = 0;
		App_Unlock();
		App_SendCmd(PLAYER_CMD_PREV);
		printf("Prev Track\r\n");
	} else {
		App_Unlock();
	}

	btn_click_count = 0;
}

/** Detect presses/releases and dispatch action after MULTICLICK_TIMEOUT. */
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
