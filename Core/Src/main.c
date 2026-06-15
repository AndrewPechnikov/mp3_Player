/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "fonts.h"
#include "mp3dec.h"
#include "ssd1306.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define MAX_SONGS 20
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

I2C_HandleTypeDef hi2c1;

I2S_HandleTypeDef hi2s2;
DMA_HandleTypeDef hdma_spi2_tx;

SPI_HandleTypeDef hspi1;

UART_HandleTypeDef huart1;

/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = { .name = "defaultTask",
		.stack_size = 1024 * 4, .priority = (osPriority_t) osPriorityNormal, };
/* Definitions for AudioTask */
osThreadId_t AudioTaskHandle;
const osThreadAttr_t AudioTask_attributes = { .name = "AudioTask", .stack_size =
		2048 * 4, .priority = (osPriority_t) osPriorityHigh, };
/* Definitions for dmaSem */
osSemaphoreId_t dmaSemHandle;
const osSemaphoreAttr_t dmaSem_attributes = { .name = "dmaSem" };
/* USER CODE BEGIN PV */

char playlist[MAX_SONGS][80]; // Масив імен файлів

int total_songs = 0;
int current_song_index = 0;
volatile uint8_t file_opened = 0;
volatile uint16_t adc_volume = 4095;
volatile uint8_t skip_track = 0;
volatile int8_t track_direction = 1;

volatile uint8_t is_paused = 0;       // Флаг паузи
uint32_t btn_last_press_time = 0;     // Час останнього натискання
uint8_t btn_click_count = 0;          // Кількість кліків
uint8_t btn_is_pressed = 0;           // Поточний стан кнопки
const uint32_t MULTICLICK_TIMEOUT = 400; // Час (мс), протягом якого чекаємо наступний клік
int window_start = 0;
const int MAX_LINES = 4;
uint8_t force_ui_update = 0;

uint8_t pending_resume = 0; // Прапорець для відкладеного зняття з паузи

// Перенесені сюди структури тепер захищені від переповнення стеку:
FATFS fs;
FIL fil;
FRESULT fres;
UINT bytesRead;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_I2S2_Init(void);
static void MX_I2C1_Init(void);
static void MX_ADC1_Init(void);
void StartDefaultTask(void *argument);
void StartAudioTask(void *argument);

/* USER CODE BEGIN PFP */

FRESULT ScanFiles(char*);
void I2S_Reconfig(uint32_t);
void display_init();
void fs_init();
void volume_control();
void button_control();
void open_next_song();
void interface_control();

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

int _write(int file, char *ptr, int len) {
	HAL_UART_Transmit(&huart1, (uint8_t*) ptr, len, 100);
	return len;
}
#define READ_BUF_SIZE 4096
#define PCM_BUF_SIZE  (1152 * 2 * 2)

uint8_t readBuf[READ_BUF_SIZE];
int16_t pcmBuf[PCM_BUF_SIZE];

volatile uint8_t dma_half_ready = 0;
volatile uint8_t dma_full_ready = 0;

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s) {
	dma_half_ready = 1;
	osSemaphoreRelease(dmaSemHandle);
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s) {
	dma_full_ready = 1;
	osSemaphoreRelease(dmaSemHandle);
}

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void) {

	/* USER CODE BEGIN 1 */

	/* USER CODE END 1 */

	/* MCU Configuration--------------------------------------------------------*/

	/* Reset of all peripherals, Initializes the Flash interface and the Systick. */
	HAL_Init();

	/* USER CODE BEGIN Init */

	/* USER CODE END Init */

	/* Configure the system clock */
	SystemClock_Config();

	/* USER CODE BEGIN SysInit */

	/* USER CODE END SysInit */

	/* Initialize all configured peripherals */
	MX_GPIO_Init();
	MX_DMA_Init();
	MX_SPI1_Init();
	MX_USART1_UART_Init();
	MX_FATFS_Init();
	MX_I2S2_Init();
	MX_I2C1_Init();
	MX_ADC1_Init();
	/* USER CODE BEGIN 2 */

	/* USER CODE END 2 */

	/* Init scheduler */
	osKernelInitialize();

	/* USER CODE BEGIN RTOS_MUTEX */
	/* add mutexes, ... */
	/* USER CODE END RTOS_MUTEX */

	/* Create the semaphores(s) */
	/* creation of dmaSem */
	dmaSemHandle = osSemaphoreNew(1, 1, &dmaSem_attributes);

	/* USER CODE BEGIN RTOS_SEMAPHORES */
	/* add semaphores, ... */
	/* USER CODE END RTOS_SEMAPHORES */

	/* USER CODE BEGIN RTOS_TIMERS */
	/* start timers, add new ones, ... */
	/* USER CODE END RTOS_TIMERS */

	/* USER CODE BEGIN RTOS_QUEUES */
	/* add queues, ... */
	/* USER CODE END RTOS_QUEUES */

	/* Create the thread(s) */
	/* creation of defaultTask */
	defaultTaskHandle = osThreadNew(StartDefaultTask, NULL,
			&defaultTask_attributes);

	/* creation of AudioTask */
	AudioTaskHandle = osThreadNew(StartAudioTask, NULL, &AudioTask_attributes);

	/* USER CODE BEGIN RTOS_THREADS */
	/* add threads, ... */
	/* USER CODE END RTOS_THREADS */

	/* USER CODE BEGIN RTOS_EVENTS */
	/* add events, ... */
	/* USER CODE END RTOS_EVENTS */

	/* Start scheduler */
	osKernelStart();

	/* We should never get here as control is now taken by the scheduler */

	/* Infinite loop */
	/* USER CODE BEGIN WHILE */

	while (1) {


	}
	/* USER CODE END WHILE */

	/* USER CODE BEGIN 3 */

	/* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 * @retval None
 */
void SystemClock_Config(void) {
	RCC_OscInitTypeDef RCC_OscInitStruct = { 0 };
	RCC_ClkInitTypeDef RCC_ClkInitStruct = { 0 };

	/** Configure the main internal regulator output voltage
	 */
	__HAL_RCC_PWR_CLK_ENABLE();
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

	/** Initializes the RCC Oscillators according to the specified parameters
	 * in the RCC_OscInitTypeDef structure.
	 */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_ON;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLM = 25;
	RCC_OscInitStruct.PLL.PLLN = 168;
	RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
	RCC_OscInitStruct.PLL.PLLQ = 4;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
		Error_Handler();
	}

	/** Initializes the CPU, AHB and APB buses clocks
	 */
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
			| RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK) {
		Error_Handler();
	}
}

/**
 * @brief ADC1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_ADC1_Init(void) {

	/* USER CODE BEGIN ADC1_Init 0 */

	/* USER CODE END ADC1_Init 0 */

	ADC_ChannelConfTypeDef sConfig = { 0 };

	/* USER CODE BEGIN ADC1_Init 1 */

	/* USER CODE END ADC1_Init 1 */

	/** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
	 */
	hadc1.Instance = ADC1;
	hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
	hadc1.Init.Resolution = ADC_RESOLUTION_12B;
	hadc1.Init.ScanConvMode = DISABLE;
	hadc1.Init.ContinuousConvMode = DISABLE;
	hadc1.Init.DiscontinuousConvMode = DISABLE;
	hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
	hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
	hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
	hadc1.Init.NbrOfConversion = 1;
	hadc1.Init.DMAContinuousRequests = DISABLE;
	hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
	if (HAL_ADC_Init(&hadc1) != HAL_OK) {
		Error_Handler();
	}

	/** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
	 */
	sConfig.Channel = ADC_CHANNEL_0;
	sConfig.Rank = 1;
	sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN ADC1_Init 2 */

	/* USER CODE END ADC1_Init 2 */

}

/**
 * @brief I2C1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_I2C1_Init(void) {

	/* USER CODE BEGIN I2C1_Init 0 */

	/* USER CODE END I2C1_Init 0 */

	/* USER CODE BEGIN I2C1_Init 1 */

	/* USER CODE END I2C1_Init 1 */
	hi2c1.Instance = I2C1;
	hi2c1.Init.ClockSpeed = 400000;
	hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
	hi2c1.Init.OwnAddress1 = 0;
	hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
	hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
	hi2c1.Init.OwnAddress2 = 0;
	hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
	hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
	if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN I2C1_Init 2 */

	/* USER CODE END I2C1_Init 2 */

}

/**
 * @brief I2S2 Initialization Function
 * @param None
 * @retval None
 */
static void MX_I2S2_Init(void) {

	/* USER CODE BEGIN I2S2_Init 0 */

	/* USER CODE END I2S2_Init 0 */

	/* USER CODE BEGIN I2S2_Init 1 */

	/* USER CODE END I2S2_Init 1 */
	hi2s2.Instance = SPI2;
	hi2s2.Init.Mode = I2S_MODE_MASTER_TX;
	hi2s2.Init.Standard = I2S_STANDARD_PHILIPS;
	hi2s2.Init.DataFormat = I2S_DATAFORMAT_16B;
	hi2s2.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
	hi2s2.Init.AudioFreq = I2S_AUDIOFREQ_48K;
	hi2s2.Init.CPOL = I2S_CPOL_LOW;
	hi2s2.Init.ClockSource = I2S_CLOCK_PLL;
	hi2s2.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
	if (HAL_I2S_Init(&hi2s2) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN I2S2_Init 2 */

	/* USER CODE END I2S2_Init 2 */

}

/**
 * @brief SPI1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_SPI1_Init(void) {

	/* USER CODE BEGIN SPI1_Init 0 */

	/* USER CODE END SPI1_Init 0 */

	/* USER CODE BEGIN SPI1_Init 1 */

	/* USER CODE END SPI1_Init 1 */
	/* SPI1 parameter configuration*/
	hspi1.Instance = SPI1;
	hspi1.Init.Mode = SPI_MODE_MASTER;
	hspi1.Init.Direction = SPI_DIRECTION_2LINES;
	hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
	hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
	hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
	hspi1.Init.NSS = SPI_NSS_SOFT;
	hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
	hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
	hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
	hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
	hspi1.Init.CRCPolynomial = 10;
	if (HAL_SPI_Init(&hspi1) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN SPI1_Init 2 */

	/* USER CODE END SPI1_Init 2 */

}

/**
 * @brief USART1 Initialization Function
 * @param None
 * @retval None
 */
static void MX_USART1_UART_Init(void) {

	/* USER CODE BEGIN USART1_Init 0 */

	/* USER CODE END USART1_Init 0 */

	/* USER CODE BEGIN USART1_Init 1 */

	/* USER CODE END USART1_Init 1 */
	huart1.Instance = USART1;
	huart1.Init.BaudRate = 115200;
	huart1.Init.WordLength = UART_WORDLENGTH_8B;
	huart1.Init.StopBits = UART_STOPBITS_1;
	huart1.Init.Parity = UART_PARITY_NONE;
	huart1.Init.Mode = UART_MODE_TX_RX;
	huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart1.Init.OverSampling = UART_OVERSAMPLING_16;
	if (HAL_UART_Init(&huart1) != HAL_OK) {
		Error_Handler();
	}
	/* USER CODE BEGIN USART1_Init 2 */

	/* USER CODE END USART1_Init 2 */

}

/**
 * Enable DMA controller clock
 */
static void MX_DMA_Init(void) {

	/* DMA controller clock enable */
	__HAL_RCC_DMA1_CLK_ENABLE();

	/* DMA interrupt init */
	/* DMA1_Stream4_IRQn interrupt configuration */
	HAL_NVIC_SetPriority(DMA1_Stream4_IRQn, 5, 0);
	HAL_NVIC_EnableIRQ(DMA1_Stream4_IRQn);

}

/**
 * @brief GPIO Initialization Function
 * @param None
 * @retval None
 */
static void MX_GPIO_Init(void) {
	GPIO_InitTypeDef GPIO_InitStruct = { 0 };
	/* USER CODE BEGIN MX_GPIO_Init_1 */

	/* USER CODE END MX_GPIO_Init_1 */

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(SD_CS_GPIO_Port, SD_CS_Pin, GPIO_PIN_SET);

	/*Configure GPIO pins : BTN_PREV_Pin BTN_NEXT_Pin */
	GPIO_InitStruct.Pin = BTN_PREV_Pin | BTN_NEXT_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

	/*Configure GPIO pin : SD_CS_Pin */
	GPIO_InitStruct.Pin = SD_CS_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(SD_CS_GPIO_Port, &GPIO_InitStruct);

	/* USER CODE BEGIN MX_GPIO_Init_2 */

	/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

FRESULT ScanFiles(char *path) {
	FRESULT res;
	DIR dir;
	FILINFO fno;

	res = f_opendir(&dir, path);
	if (res == FR_OK) {
		while (1) {
			res = f_readdir(&dir, &fno);
			if (res != FR_OK || fno.fname[0] == 0)
				break;
			if (!(fno.fattrib & AM_DIR)) {
				char *ext = strrchr(fno.fname, '.');
				if (ext && (strcasecmp(ext, ".mp3") == 0)) {
					if (total_songs < MAX_SONGS) {
						strcpy(playlist[total_songs], fno.fname);
						printf("Found song: %s\r\n", playlist[total_songs]);
						total_songs++;
					}
				}
			}
		}
		f_closedir(&dir);
	}
	return res;
}

void display_init(){
	SSD1306_Init();
	SSD1306_Clear();
	SSD1306_Fill(0);
	SSD1306_UpdateScreen();
	osDelay(100);
}

void fs_init(){
	fres = f_mount(&fs, "", 1);
	if (fres != FR_OK) {
		printf("Mount error: %d\r\n", fres);
		Error_Handler();
	}
}

void volume_control(){
	HAL_ADC_Start(&hadc1);
			if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) {
				adc_volume = HAL_ADC_GetValue(&hadc1);
			}
}

void button_control(){

	uint8_t current_state = HAL_GPIO_ReadPin(BTN_NEXT_GPIO_Port,
			BTN_NEXT_Pin);

			// Перевірка на натискання (якщо кнопка підтягнута до землі, активний рівень SET)
			if (current_state == GPIO_PIN_SET && !btn_is_pressed) {
				btn_is_pressed = 1;
				btn_click_count++;
				btn_last_press_time = HAL_GetTick();
				osDelay(20); // Програмний антибрязкіт (debounce)
			}
			// Перевірка на відпускання
			else if (current_state == GPIO_PIN_RESET && btn_is_pressed) {
				btn_is_pressed = 0;
				osDelay(20); // Програмний антибрязкіт
			}

			// Якщо є кліки і час очікування (400 мс) закінчився - приймаємо рішення
			if (btn_click_count > 0
					&& (HAL_GetTick() - btn_last_press_time) > MULTICLICK_TIMEOUT) {

				if (btn_click_count == 1 && file_opened == 1) {
					// --- 1 КЛІК: ПАУЗА / ПЛЕЙ ---
					is_paused = !is_paused;
					if (is_paused) {
						// Ставимо на паузу ОДРАЗУ
						HAL_I2S_DMAPause(&hi2s2);
						printf("Paused\r\n");
						force_ui_update = 1;
					} else {
						// Для ПЛЕЙ: готуємо екран, але DMA не запускаємо
						printf("Preparing to resume...\r\n");
						force_ui_update = 1;
						pending_resume = 1; // Даємо сигнал запустити музику ПІСЛЯ відмальовки
					}
				} else if (btn_click_count == 2 && file_opened == 1) {
					// --- 2 КЛІКИ: НАСТУПНИЙ ТРЕК ---
					track_direction = 1;
					skip_track = 1;
					is_paused = 0; // Знімаємо паузу для нового треку
					printf("Next Track\r\n");

				} else if (btn_click_count >= 3 && file_opened == 1) {
					// --- 3 КЛІКИ: ПОПЕРЕДНІЙ ТРЕК ---
					track_direction = -1;
					skip_track = 1;
					is_paused = 0; // Знімаємо паузу для нового треку
					printf("Prev Track\r\n");
				}

				btn_click_count = 0; // Обнуляємо лічильник після обробки
			}

}


void open_next_song(){

	if (file_opened == 0 && total_songs > 0 && force_ui_update == 0) {
				printf("Loading track: %s\r\n", playlist[current_song_index]);
				fres = f_open(&fil, playlist[current_song_index], FA_READ);

				if (fres == FR_OK) {
					force_ui_update = 1; // Знову командуємо оновити екран ДО старту
				} else {
					// ЗАХИСТ ВІД ЗАВИСАННЯ: Файл битий або не відкривається
					printf("Error %d: Cannot open file. Skipping track...\r\n",
							fres);

					// Автоматично крокуємо далі в тому ж напрямку
					current_song_index += track_direction;

					// Перевіряємо межі списку
					if (current_song_index >= total_songs) {
						current_song_index = 0;
					} else if (current_song_index < 0) {
						current_song_index = total_songs - 1;
					}

					osDelay(100); // Невелика затримка перед спробою відкрити наступний
				}
			}

}

void interface_control(){

	if (total_songs > 0) {
				// Рахуємо вікно видимості (Вертикальний скрол)
				if (current_song_index < window_start) {
					window_start = current_song_index; // Зсув вгору
					printf("UP");
				} else if (current_song_index >= window_start + MAX_LINES) {
					window_start = current_song_index - MAX_LINES + 1; // Зсув вниз
					printf("DOWN");
				}

				if (force_ui_update) {
					force_ui_update = 0;

					SSD1306_Clear();
					SSD1306_GotoXY(0, 0);
					SSD1306_Puts(" -PLAYLIST- ", &Font_11x18, SSD1306_COLOR_WHITE);

					int lines_to_draw =
							(total_songs < MAX_LINES) ? total_songs : MAX_LINES;
					char display_buf[20];

					for (int i = 0; i < lines_to_draw; i++) {
						int actual_index = window_start + i;
						SSD1306_GotoXY(0, 18 + (i * 11));

						strncpy(display_buf, playlist[actual_index], 15);
						display_buf[15] = '\0';

						// ЗАХИСНИЙ ФІЛЬТР: замінюємо не-ASCII символи на '?'
						for (int k = 0; display_buf[k] != '\0'; k++) {
							if ((unsigned char) display_buf[k] < 32
									|| (unsigned char) display_buf[k] > 126) {
								display_buf[k] = '?';
							}
						}

						// НОВА ФІШКА: Малюємо курсор або знак ПАУЗИ
						if (actual_index == current_song_index) {
							if (is_paused) {
								SSD1306_Puts("||", &Font_7x10, SSD1306_COLOR_WHITE); // Пауза
							} else {
								SSD1306_Puts("> ", &Font_7x10, SSD1306_COLOR_WHITE); // Грає
							}
						} else {
							SSD1306_Puts("  ", &Font_7x10, SSD1306_COLOR_WHITE);
						}
						SSD1306_Puts(display_buf, &Font_7x10, SSD1306_COLOR_WHITE);
					}

					SSD1306_UpdateScreen();

					// ОБОВ'ЯЗКОВО ПОВЕРТАЄМО ЗАТРИМКУ!
					// Вона дає час шині I2C повністю передати кадр у пам'ять дисплея
					osDelay(50);

					file_opened = 1;

					// Якщо ми чекали на зняття з паузи - запускаємо аудіо ТІЛЬКИ ТЕПЕР,
					// коли екран вже 100% відмалювався
					if (pending_resume) {
						pending_resume = 0;
						HAL_I2S_DMAResume(&hi2s2);
						printf("Resumed\r\n");
					}
				}
			}

}
/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartDefaultTask */
/**
 * @brief  Function implementing the defaultTask thread.
 * @param  argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument) {
	/* USER CODE BEGIN 5 */

	display_init();
	printf("\r\n--- SD Card & OLED Test (FreeRTOS) ---\r\n");
	fs_init();
	ScanFiles("/");

	// --- ЗМІННІ ДЛЯ ПРОКРУТКИ ТА UI ---


	if (total_songs > 0) {
		printf("Found %d songs. Preparing UI and opening file...\r\n",
				total_songs);

		// ВІДКРИВАЄМО ФАЙЛ, АЛЕ НЕ ЗАПУСКАЄМО ПЛЕЄР ОДРАЗУ
		fres = f_open(&fil, playlist[current_song_index], FA_READ);
		if (fres != FR_OK) {
			printf("File open error: %d\r\n", fres);
			Error_Handler();
		} else {
			printf("File opened OK\r\n");
			force_ui_update = 1; // Даємо команду намалювати інтерфейс
		}
	} else {
		printf("No MP3 files found!\r\n");
		SSD1306_Clear();
		SSD1306_GotoXY(0, 20);
		SSD1306_Puts("No MP3 Found!", &Font_7x10, SSD1306_COLOR_WHITE);
		SSD1306_UpdateScreen();
	}

	/* Infinite loop */
	for (;;) {
		// 1. ОПИТУВАННЯ АЦП (ПОТЕНЦІОМЕТРА)
		volume_control();
		// 2. ОПИТУВАННЯ ОДНІЄЇ КНОПКИ (Мультіклік)
		button_control();
		// 3. ПЕРЕМИКАННЯ ФАЙЛІВ
		open_next_song();
		// 4. ЛОГІКА ВІДОБРАЖЕННЯ (UI)
		interface_control();

	}
	/* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartAudioTask */
/**
 * @brief Function implementing the AudioTask thread.
 * @param argument: Not used
 * @retval None
 */
/* USER CODE END Header_StartAudioTask */
void StartAudioTask(void *argument) {
	/* USER CODE BEGIN StartAudioTask */
	/* Infinite loop */
	for (;;) {
		while (file_opened == 0) {
			osDelay(10);
		}

		HMP3Decoder hMP3Decoder = MP3InitDecoder();
		if (!hMP3Decoder) {
			printf("MP3 decoder init failed\r\n");
			Error_Handler();
		}

		uint8_t *readPtr = readBuf;
		int bytesLeft = 0;

		// Первинне читання
		f_read(&fil, readBuf, READ_BUF_SIZE, &bytesRead);
		bytesLeft = bytesRead;

		printf("Pre-loading buffers...\r\n");

		// --- ЗАРЯДЖАЄМО ПЕРШУ ПОЛОВИНУ ---
		int offset = MP3FindSyncWord(readPtr, bytesLeft);
		if (offset >= 0) {
			readPtr += offset;
			bytesLeft -= offset;
			MP3Decode(hMP3Decoder, &readPtr, &bytesLeft, pcmBuf, 0);
		}

		// --- ЗАРЯДЖАЄМО ДРУГУ ПОЛОВИНУ ---
		offset = MP3FindSyncWord(readPtr, bytesLeft);
		if (offset >= 0) {
			readPtr += offset;
			bytesLeft -= offset;
			MP3Decode(hMP3Decoder, &readPtr, &bytesLeft,
					pcmBuf + (PCM_BUF_SIZE / 2), 0);
		}

		MP3FrameInfo info;
		MP3GetLastFrameInfo(hMP3Decoder, &info);
		printf("Bitrate: %d, Hz: %d, Chans: %d\r\n", info.bitrate,
				info.samprate, info.nChans);

		static uint32_t current_freq = 0;

		// 1. НАЛАШТУВАННЯ ЧАСТОТИ
		if (info.samprate != current_freq) {
			printf("Switching frequency to: %d Hz\r\n", info.samprate);

			uint32_t hal_freq = I2S_AUDIOFREQ_44K;
			if (info.samprate >= 48000) {
				hal_freq = I2S_AUDIOFREQ_48K;
				printf("48K");
			} else if (info.samprate >= 44100) {
				hal_freq = I2S_AUDIOFREQ_44K;
				printf("44K");
			} else if (info.samprate >= 32000) {
				hal_freq = I2S_AUDIOFREQ_32K;
				printf("32K");
			}

			hi2s2.Init.AudioFreq = hal_freq;

			current_freq = info.samprate;
		}

		printf("Resetting I2S2 configuration...\r\n");

		HAL_I2S_DeInit(&hi2s2);
		if (HAL_I2S_Init(&hi2s2) != HAL_OK) {
			printf("I2S Re-Init Failed!\r\n");
			Error_Handler();
		}

		SPI2->I2SPR = 0x0010;

		dma_half_ready = 0;
		dma_full_ready = 0;

		printf("Starting I2S DMA...\r\n");

		HAL_StatusTypeDef dma_status = HAL_I2S_Transmit_DMA(&hi2s2,
				(uint16_t*) pcmBuf, PCM_BUF_SIZE);
		if (dma_status != HAL_OK) {
			printf("DMA Start Failed! Error: %d\r\n", dma_status);
			Error_Handler();
		}

		while (1) {
			if (skip_track) {
				printf("Skipping track...\r\n");

				// 1. Тільки зупиняємо DMA
				HAL_I2S_DMAStop(&hi2s2);

				// 2. Очищуємо прапорці, щоб вони не смикали семафор під час закриття файлу
				dma_half_ready = 0;
				dma_full_ready = 0;

				MP3FreeDecoder(hMP3Decoder);
				f_close(&fil);

				current_song_index += track_direction;
				if (current_song_index >= total_songs)
					current_song_index = 0;
				if (current_song_index < 0)
					current_song_index = total_songs - 1;

				skip_track = 0;
				file_opened = 0;

				osDelay(200);

				break; // Виходимо, щоб зовнішній цикл перезапустив процес
			}

			// --- 2. ПРИРОДНІЙ КІНЕЦЬ ФАЙЛУ ---
			if (bytesLeft < 2048) {
				memmove(readBuf, readPtr, bytesLeft);
				readPtr = readBuf;
				UINT br = 0;
				f_read(&fil, readBuf + bytesLeft, READ_BUF_SIZE - bytesLeft,
						&br);
				bytesLeft += br;

				if (f_eof(&fil) && bytesLeft < 200) {
					printf("End of file. Switching...\r\n");
					HAL_I2S_DMAStop(&hi2s2);
					MP3FreeDecoder(hMP3Decoder);
					f_close(&fil);

					current_song_index++;
					if (current_song_index >= total_songs)
						current_song_index = 0;

					file_opened = 0;
					break;
				}
			}

			// --- 3. ДЕКОДУВАННЯ ТА ГУЧНІСТЬ ---
			if (osSemaphoreAcquire(dmaSemHandle, osWaitForever) == osOK) {
				if (dma_half_ready || dma_full_ready) {
					int16_t *target_pcm =
							dma_half_ready ?
									pcmBuf : (pcmBuf + PCM_BUF_SIZE / 2);
					int frame_decoded = 0;

					while (!frame_decoded && bytesLeft > 0) {
						int offset = MP3FindSyncWord(readPtr, bytesLeft);
						if (offset >= 0) {
							readPtr += offset;
							bytesLeft -= offset;
							int err = MP3Decode(hMP3Decoder, &readPtr,
									&bytesLeft, target_pcm, 0);

							if (err == ERR_MP3_NONE) {
								frame_decoded = 1;
								MP3GetLastFrameInfo(hMP3Decoder, &info);

								// === НОВА МАТЕМАТИКА ГУЧНОСТІ ===
								for (int i = 0; i < info.outputSamps; i++) {
									int32_t sample = target_pcm[i];
									// Спочатку застосовуємо потенціометр (масштаб 0-4095)
									sample = (sample * adc_volume) / 4095;
									// Далі обмежуємо максимальну гучність (твоя умова / 8)
									target_pcm[i] = (int16_t) (sample / 8);
								}
								// ================================

								if (info.outputSamps < (PCM_BUF_SIZE / 2)) {
									memset(target_pcm + info.outputSamps, 0,
											((PCM_BUF_SIZE / 2)
													- info.outputSamps)
													* sizeof(int16_t));
								}
							} else {
								readPtr++;
								bytesLeft--;
							}
						} else {
							bytesLeft = 0;
							break;
						}
					}

					if (!frame_decoded) {
						memset(target_pcm, 0,
								(PCM_BUF_SIZE / 2) * sizeof(int16_t));
					}

					if (dma_half_ready)
						dma_half_ready = 0;
					else
						dma_full_ready = 0;
				}
			}
		}
	}

	/* USER CODE END StartAudioTask */
}

/**
 * @brief  Period elapsed callback in non blocking mode
 * @note   This function is called  when TIM3 interrupt took place, inside
 * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
 * a global variable "uwTick" used as application time base.
 * @param  htim : TIM handle
 * @retval None
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	/* USER CODE BEGIN Callback 0 */

	/* USER CODE END Callback 0 */
	if (htim->Instance == TIM3) {
		HAL_IncTick();
	}
	/* USER CODE BEGIN Callback 1 */

	/* USER CODE END Callback 1 */
}

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void) {
	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1) {
	}
	/* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
