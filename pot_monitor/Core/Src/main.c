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
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <stdarg.h>
#include "usbd_cdc_if.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define ADC_MAX_VALUE   4095U   /* 12-bit resolution */
#define VREF_MV         3300U   /* VDDA = 3.3 V */

/* --- 최대/최소 캘리브레이션 (터미널 키: '0'=PA0만, '1'=PA1만, 'c'=둘 다 동시) ---
 *   1구간: 가변저항을 한쪽 끝에 고정 → 구간 평균값
 *   2구간: 반대쪽 끝에 고정        → 구간 평균값
 * 두 값 중 작은 쪽이 min, 큰 쪽이 max. (순서 무관)
 * 'c'(동시)일 때는 두 손잡이를 같은 타이밍에 끝으로 돌려 놓으면 된다.
 * 각 구간 = 제외 시간(손잡이 옮기는 동안, 평균에 안 넣음) + 측정 시간(평균에 넣음).
 */
#define CALIB_SETTLE_MS     2000U   /* 구간 앞부분: 평균에서 제외 (손잡이 이동 시간) */
#define CALIB_MEASURE_MS    5000U   /* 구간 뒷부분: 실제 측정·평균 */
#define CALIB_PHASE_MS      (CALIB_SETTLE_MS + CALIB_MEASURE_MS)   /* 각 구간 총 길이 = 7초 */
#define CALIB_SAMPLE_MS     10U     /* 구간 내 샘플 주기 */
#define CALIB_REPORT_MS     500U    /* 진행 상황 출력 주기 */

/* --- 노이즈 필터 (중앙값3 → EMA) ---
 * 1단: 최근 3샘플의 중앙값 → 단발 스파이크 제거
 * 2단: 1차 저역통과 filt += (med - filt)/8 → 잔노이즈 평활 (실측: σ 5.8 → 약 1.5)
 * 출력의 flt0/flt1이 필터 결과. 반응 속도를 높이려면 SHIFT를 2(=/4)로 낮출 것. */
#define FILT_EMA_SHIFT      3U      /* EMA 분모 = 2^3 = 8 */

/* --- 클럭 자동 설정 ---
 * 보드 크리스탈(HSE) 주파수를 부팅 시 직접 측정해서 PLL을 맞춘다.
 * 목표: SYSCLK 84 MHz, USB 48 MHz.  (PLLN=336, PLLP=4, PLLQ=7, PLLM=HSE[MHz])
 */
#define TARGET_SYSCLK_HZ    84000000U   /* 현재 84 MHz. 바꿀 때는 아래 조합표 값으로 PLL 두 곳도 함께 */
/* --- 주파수 변경용 유효 조합표 (USB 48 MHz 유지 필수) ---
 * 규칙: SYSCLK = PLLN÷PLLP, USB = PLLN÷PLLQ = 48  (VCO 입력 1 MHz 기준)
 * PLLN/PLLP/PLLQ는 SystemClock_Config_Auto()의 2)블록과 4)블록에 "같은 값"으로 넣을 것.
 *
 *  SYSCLK        TARGET_SYSCLK_HZ    PLLN   PLLP             PLLQ
 *  84 MHz(현재)  84000000U           336    RCC_PLLP_DIV4    7
 *  72 MHz        72000000U           288    RCC_PLLP_DIV4    6
 *  64 MHz        64000000U           384    RCC_PLLP_DIV6    8
 *  60 MHz        60000000U           240    RCC_PLLP_DIV4    5
 *  56 MHz        56000000U           336    RCC_PLLP_DIV6    7
 *  48 MHz        48000000U           192    RCC_PLLP_DIV4    4
 *  42 MHz        42000000U           336    RCC_PLLP_DIV8    7
 *
 * 제약: SYSCLK ≤ 84 MHz, PLLN(=VCO) 192~432 → 위 조합 외 임의 값은 불가.
 * 변경 후: Ctrl+B → flash.bat → 터미널에서 'i' 키로 sysclk 확인.
 */
#define HSE_MEAS_RTCPRE     25U     /* 측정용 분주: HSE_RTC = HSE / 25 */
#define HSE_MEAS_CAPTURES   64U     /* 캡처 횟수 (IC 프리스케일러 8 → 512 에지) */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

/* USER CODE BEGIN PV */
/* raw = ADC가 읽은 원시값 (12비트, 0~4095).
 *       핀 전압에 비례: 0 V → 0, 3.3 V → 4095. 가변저항 위치를 나타내는 기본 값.
 * mv  = raw를 사람이 읽기 쉬운 전압 [밀리볼트]로 환산한 값.
 *       mv = raw × 3300 ÷ 4095  (0~3300 mV = 0~3.3 V). 표시용일 뿐 원본은 raw. */
volatile uint16_t adc_raw = 0;      /* PA0 (ADC1_IN0) 원시값, 0 ~ 4095 */
volatile uint32_t adc_mv  = 0;      /* PA0 전압 [mV], 0 ~ 3300 */

volatile uint16_t adc_raw1 = 0;     /* PA1 (ADC1_IN1) 원시값, 0 ~ 4095 — 두 번째 가변저항 */
volatile uint32_t adc_mv1  = 0;     /* PA1 전압 [mV], 0 ~ 3300 */

volatile uint16_t adc_flt  = 0;     /* PA0 필터(중앙값3→EMA) 출력 */
volatile uint16_t adc_flt1 = 0;     /* PA1 필터 출력 */

volatile uint16_t adc_min0    = 0;          /* PA0 캘리브레이션 결과 (calib_done0=1일 때 유효) */
volatile uint16_t adc_max0    = ADC_MAX_VALUE;
volatile uint8_t  calib_done0 = 0;

volatile uint16_t adc_min1    = 0;          /* PA1 캘리브레이션 결과 (calib_done1=1일 때 유효) */
volatile uint16_t adc_max1    = ADC_MAX_VALUE;
volatile uint8_t  calib_done1 = 0;

volatile uint8_t  cdc_last_cmd = 0;         /* 터미널에서 마지막으로 받은 명령 문자 (usbd_cdc_if.c가 씀) */

volatile uint32_t hse_hz_measured = 0;      /* 측정된 HSE 주파수 [Hz], 0이면 HSE 미사용 */
volatile uint8_t  clk_src_is_hse  = 0;      /* 1: PLL 소스 HSE, 0: HSI (USB 불안정 가능) */
static char       tx_buf[128];              /* USB CDC 송신 버퍼 */

extern USBD_HandleTypeDef hUsbDeviceFS;     /* usb_device.c */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
/* USER CODE BEGIN PFP */
static uint16_t ADC_ReadChannel(uint32_t channel, uint32_t sampling_time);
static uint16_t Median3(uint16_t a, uint16_t b, uint16_t c);
static uint16_t Filter_Update(uint8_t ch, uint16_t raw);
static void     ADC_AverageFor(uint32_t duration_ms, uint8_t step, uint8_t ch_mask,
                               uint16_t *avg0, uint16_t *avg1);
static void     ADC_Calibrate(uint8_t ch_mask);
static void     CDC_Printf(const char *fmt, ...);
static uint32_t HSE_MeasureHz(void);
static void     SystemClock_Config_Auto(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN SysInit */
  /* CubeMX가 생성한 SystemClock_Config()는 호출하지 않고(Project Manager에서 호출 비활성),
   * HSE 주파수를 실측해 PLL을 맞추는 버전을 대신 사용한다. */
  SystemClock_Config_Auto();
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_ADC1_Init();
  MX_USB_DEVICE_Init();
  /* USER CODE BEGIN 2 */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 터미널에서 온 명령 처리 */
    switch (cdc_last_cmd)
    {
      case 'c': case 'C':                   /* 둘 다 동시에 */
        cdc_last_cmd = 0;
        ADC_Calibrate(0x03U);
        break;
      case '0':                             /* PA0만 */
        cdc_last_cmd = 0;
        ADC_Calibrate(0x01U);
        break;
      case '1':                             /* PA1만 */
        cdc_last_cmd = 0;
        ADC_Calibrate(0x02U);
        break;
      case 'i': case 'I':
        cdc_last_cmd = 0;
        CDC_Printf("info: hse=%lu kHz (%s), sysclk=%lu Hz, calib0=%s calib1=%s\r\n",
                   (unsigned long)(hse_hz_measured / 1000U),
                   clk_src_is_hse ? "PLL from HSE" : "PLL from HSI",
                   (unsigned long)SystemCoreClock,
                   calib_done0 ? "done" : "not yet",
                   calib_done1 ? "done" : "not yet");
        break;
      default:
        cdc_last_cmd = 0;
        break;
    }

    adc_raw  = ADC_ReadChannel(ADC_CHANNEL_0, ADC_SAMPLETIME_84CYCLES);  /* PA0: 1번 가변저항 */
    adc_raw1 = ADC_ReadChannel(ADC_CHANNEL_1, ADC_SAMPLETIME_84CYCLES);  /* PA1: 2번 가변저항 */
    adc_flt  = Filter_Update(0U, adc_raw);
    adc_flt1 = Filter_Update(1U, adc_raw1);
    adc_mv   = ((uint32_t)adc_raw  * VREF_MV) / ADC_MAX_VALUE;
    adc_mv1  = ((uint32_t)adc_raw1 * VREF_MV) / ADC_MAX_VALUE;

    /* USB 가상 COM 포트로 출력 (포트가 안 열려 있으면 조용히 건너뜀)
     * min/max는 캘리브레이션 전에는 ---- 로 표시. 키: 0=PA0, 1=PA1, c=둘 다 */
    {
      char s0[48];
      char s1[48];

      if (calib_done0)
      {
        snprintf(s0, sizeof(s0), "raw0=%4u flt0=%4u min0=%4u max0=%4u",
                 (unsigned)adc_raw, (unsigned)adc_flt,
                 (unsigned)adc_min0, (unsigned)adc_max0);
      }
      else
      {
        snprintf(s0, sizeof(s0), "raw0=%4u flt0=%4u min0=---- max0=----",
                 (unsigned)adc_raw, (unsigned)adc_flt);
      }

      if (calib_done1)
      {
        snprintf(s1, sizeof(s1), "raw1=%4u flt1=%4u min1=%4u max1=%4u",
                 (unsigned)adc_raw1, (unsigned)adc_flt1,
                 (unsigned)adc_min1, (unsigned)adc_max1);
      }
      else
      {
        snprintf(s1, sizeof(s1), "raw1=%4u flt1=%4u min1=---- max1=----",
                 (unsigned)adc_raw1, (unsigned)adc_flt1);
      }

      CDC_Printf("%s | %s\r\n", s0, s1);
    }

    HAL_Delay(10);     /* 샘플·출력 주기 10 ms = 초당 100회 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

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
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV8;
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
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_3CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/**
  * @brief  지정한 ADC1 채널을 한 번 변환하여 12비트 결과를 반환한다 (폴링 방식).
  *         채널을 매번 다시 설정하므로 여러 채널을 번갈아 읽을 수 있다.
  * @param  channel        ADC_CHANNEL_0(PA0), ADC_CHANNEL_1(PA1), ...
  * @param  sampling_time  ADC_SAMPLETIME_xCYCLES (채널 전환 시 84 이상 권장)
  * @retval 0 ~ 4095. 실패 시 0.
  */
static uint16_t ADC_ReadChannel(uint32_t channel, uint32_t sampling_time)
{
  ADC_ChannelConfTypeDef sConfig = {0};
  uint16_t value = 0;

  sConfig.Channel      = channel;
  sConfig.Rank         = 1;
  sConfig.SamplingTime = sampling_time;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    return 0;
  }

  if (HAL_ADC_Start(&hadc1) != HAL_OK)
  {
    return 0;
  }
  if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
  {
    value = (uint16_t)HAL_ADC_GetValue(&hadc1);
  }
  HAL_ADC_Stop(&hadc1);
  return value;
}

/**
  * @brief  USB CDC로 printf 형식 출력. 이전 전송이 끝나길 최대 20 ms 기다린 뒤 보낸다.
  *         포트가 열려 있지 않으면(호스트가 읽지 않으면) 조용히 버린다 → 블로킹되지 않음.
  */
static void CDC_Printf(const char *fmt, ...)
{
  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
  va_list  ap;
  int      len;
  uint32_t t0;

  if ((hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED) || (hcdc == NULL))
  {
    return;                                       /* USB 미연결 */
  }

  /* tx_buf는 전송 중 USB 코어가 직접 읽으므로, 이전 전송이 끝난 뒤에만 덮어쓴다 */
  t0 = HAL_GetTick();
  while (hcdc->TxState != 0U)
  {
    if ((HAL_GetTick() - t0) > 20U) { return; }   /* 호스트가 안 읽고 있음 → 이번 줄 포기 */
  }

  va_start(ap, fmt);
  len = vsnprintf(tx_buf, sizeof(tx_buf), fmt, ap);
  va_end(ap);

  if (len > 0)
  {
    if (len > (int)sizeof(tx_buf) - 1) { len = (int)sizeof(tx_buf) - 1; }
    (void)CDC_Transmit_FS((uint8_t *)tx_buf, (uint16_t)len);
  }
}

/**
  * @brief  세 값의 중앙값. 단발성 스파이크 제거용.
  */
static uint16_t Median3(uint16_t a, uint16_t b, uint16_t c)
{
  uint16_t hi = (a > b) ? a : b;
  uint16_t lo = (a < b) ? a : b;
  uint16_t m  = (c < hi) ? c : hi;   /* min(max(a,b), c) */
  return (m > lo) ? m : lo;          /* max(min(a,b), 위 값) = 중앙값 */
}

/**
  * @brief  2단 노이즈 필터: 최근 3샘플 중앙값 → EMA(1/2^FILT_EMA_SHIFT).
  *         채널별(ch 0/1) 상태를 내부에 유지한다. 처음 3샘플은 워밍업으로 그대로 통과.
  * @retval 필터링된 값 (0 ~ 4095)
  */
static uint16_t Filter_Update(uint8_t ch, uint16_t raw)
{
  static uint16_t buf[2][3];
  static uint8_t  pos[2]  = {0, 0};
  static uint8_t  fill[2] = {0, 0};
  static int32_t  acc[2];            /* EMA 누적기 = 출력값 << FILT_EMA_SHIFT */
  uint16_t med;

  buf[ch][pos[ch]] = raw;
  pos[ch] = (uint8_t)((pos[ch] + 1U) % 3U);

  if (fill[ch] < 3U)                 /* 워밍업: 버퍼가 찰 때까지 원본 통과 */
  {
    fill[ch]++;
    acc[ch] = (int32_t)raw << FILT_EMA_SHIFT;
    return raw;
  }

  med = Median3(buf[ch][0], buf[ch][1], buf[ch][2]);
  acc[ch] += (int32_t)med - (acc[ch] >> FILT_EMA_SHIFT);
  return (uint16_t)(acc[ch] >> FILT_EMA_SHIFT);
}

/**
  * @brief  duration_ms 동안 두 채널을 반복 읽어 각각의 평균을 돌려준다.
  *         처음 CALIB_SETTLE_MS 는 손잡이 이동 시간으로 보고 평균에서 제외하며,
  *         CALIB_REPORT_MS 마다 남은 시간과 현재값을 터미널에 출력한다.
  * @param  step     진행 표시용 구간 번호 (1 또는 2)
  * @param  ch_mask  진행 표시용 대상: bit0=PA0, bit1=PA1
  * @param  avg0/avg1  각 채널의 구간 평균값 출력 (0 ~ 4095)
  */
static void ADC_AverageFor(uint32_t duration_ms, uint8_t step, uint8_t ch_mask,
                           uint16_t *avg0, uint16_t *avg1)
{
  uint32_t sum0  = 0;
  uint32_t sum1  = 0;
  uint32_t count = 0;
  uint32_t start = HAL_GetTick();
  uint32_t last_report = 0;
  uint32_t elapsed;

  while ((elapsed = HAL_GetTick() - start) < duration_ms)
  {
    adc_raw  = ADC_ReadChannel(ADC_CHANNEL_0, ADC_SAMPLETIME_84CYCLES);
    adc_raw1 = ADC_ReadChannel(ADC_CHANNEL_1, ADC_SAMPLETIME_84CYCLES);
    adc_mv   = ((uint32_t)adc_raw  * VREF_MV) / ADC_MAX_VALUE;
    adc_mv1  = ((uint32_t)adc_raw1 * VREF_MV) / ADC_MAX_VALUE;

    if (elapsed >= CALIB_SETTLE_MS)
    {
      sum0 += adc_raw;
      sum1 += adc_raw1;
      count++;
    }

    if ((elapsed - last_report) >= CALIB_REPORT_MS)
    {
      uint32_t left = duration_ms - elapsed;
      last_report = elapsed;
      CDC_Printf("CALIB %u/2 [%s]: hold at %s end ... %lu.%lus left  raw0=%4u raw1=%4u%s\r\n",
                 (unsigned)step,
                 (ch_mask == 3U) ? "PA0+PA1" : ((ch_mask == 1U) ? "PA0" : "PA1"),
                 (step == 1U) ? "ONE" : "the OTHER",
                 (unsigned long)(left / 1000U), (unsigned long)((left % 1000U) / 100U),
                 (unsigned)adc_raw, (unsigned)adc_raw1,
                 (elapsed < CALIB_SETTLE_MS) ? "  (settling)" : "");
    }

    HAL_Delay(CALIB_SAMPLE_MS);
  }

  *avg0 = (count > 0U) ? (uint16_t)(sum0 / count) : 0U;
  *avg1 = (count > 0U) ? (uint16_t)(sum1 / count) : 0U;
}

/**
  * @brief  두 구간(각 CALIB_PHASE_MS)의 평균으로 min/max를 결정한다.
  *         1구간과 2구간에 어느 쪽 끝을 잡아도 상관없다 (작은 값이 min).
  * @param  ch_mask  bit0=PA0, bit1=PA1. 3이면 두 채널을 한 번의 절차로 동시에 캘리브레이션.
  */
static void ADC_Calibrate(uint8_t ch_mask)
{
  const char *name = (ch_mask == 3U) ? "PA0+PA1" : ((ch_mask == 1U) ? "PA0" : "PA1");
  uint16_t a0, a1, b0, b1;

  CDC_Printf("CALIB [%s] start: turn the pot(s) to ONE end and hold.\r\n", name);
  ADC_AverageFor(CALIB_PHASE_MS, 1U, ch_mask, &a0, &a1);

  CDC_Printf("CALIB [%s]: now turn to the OTHER end and hold.\r\n", name);
  ADC_AverageFor(CALIB_PHASE_MS, 2U, ch_mask, &b0, &b1);

  if ((ch_mask & 0x01U) != 0U)
  {
    adc_min0 = (a0 < b0) ? a0 : b0;
    adc_max0 = (a0 < b0) ? b0 : a0;
    calib_done0 = 1U;
    CDC_Printf("CALIB done PA0: min=%u  max=%u  (span=%u)\r\n",
               (unsigned)adc_min0, (unsigned)adc_max0, (unsigned)(adc_max0 - adc_min0));
  }
  if ((ch_mask & 0x02U) != 0U)
  {
    adc_min1 = (a1 < b1) ? a1 : b1;
    adc_max1 = (a1 < b1) ? b1 : a1;
    calib_done1 = 1U;
    CDC_Printf("CALIB done PA1: min=%u  max=%u  (span=%u)\r\n",
               (unsigned)adc_min1, (unsigned)adc_max1, (unsigned)(adc_max1 - adc_min1));
  }
}

/**
  * @brief  TIM11 입력 캡처로 HSE 주파수를 측정한다.
  *         TIM11_OR.TI1_RMP=10 이면 CH1 입력에 HSE_RTC(= HSE / RTCPRE)가 내부 연결된다(측정 용도, RM0368).
  *         호출 전 조건: HSE 기동 완료, APB2 프리스케일러 1 (타이머 클럭 = PCLK2).
  * @retval 측정된 HSE 주파수 [Hz]. 실패 시 0.
  */
static uint32_t HSE_MeasureHz(void)
{
  uint32_t f_tim = HAL_RCC_GetPCLK2Freq();          /* APB2 presc=1 → TIM11 클럭 */
  uint32_t ticks = 0U;
  uint16_t prev;
  uint32_t t0;

  /* HSE_RTC = HSE / RTCPRE  (RTC 자체는 켜지 않으므로 백업 도메인 접근 불필요) */
  MODIFY_REG(RCC->CFGR, RCC_CFGR_RTCPRE, HSE_MEAS_RTCPRE << RCC_CFGR_RTCPRE_Pos);

  __HAL_RCC_TIM11_CLK_ENABLE();
  TIM11->CR1   = 0U;
  TIM11->PSC   = 0U;
  TIM11->ARR   = 0xFFFFU;
  TIM11->OR    = TIM_OR_TI1_RMP_1;                       /* TI1 ← HSE_RTC */
  TIM11->CCMR1 = TIM_CCMR1_CC1S_0 | TIM_CCMR1_IC1PSC;    /* CC1 입력(TI1), 8 에지마다 1회 캡처 */
  TIM11->CCER  = TIM_CCER_CC1E;                          /* 상승 에지 */
  TIM11->EGR   = TIM_EGR_UG;
  TIM11->SR    = 0U;
  TIM11->CR1   = TIM_CR1_CEN;

  t0 = HAL_GetTick();
  while ((TIM11->SR & TIM_SR_CC1IF) == 0U)
  {
    if ((HAL_GetTick() - t0) > 50U) { goto fail; }
  }
  prev = (uint16_t)TIM11->CCR1;                          /* CCR1 읽기 → CC1IF 클리어 */

  for (uint32_t i = 0U; i < HSE_MEAS_CAPTURES; i++)
  {
    while ((TIM11->SR & TIM_SR_CC1IF) == 0U)
    {
      if ((HAL_GetTick() - t0) > 50U) { goto fail; }
    }
    uint16_t now = (uint16_t)TIM11->CCR1;
    ticks += (uint16_t)(now - prev);                     /* 16비트 랩어라운드 안전 */
    prev = now;
  }

  TIM11->CR1 = 0U;
  __HAL_RCC_TIM11_CLK_DISABLE();

  if (ticks == 0U) { return 0U; }
  {
    /* f_in = edges * f_tim / ticks,  HSE = f_in * RTCPRE */
    uint64_t edges = (uint64_t)HSE_MEAS_CAPTURES * 8U;
    uint64_t hz    = (edges * (uint64_t)f_tim * HSE_MEAS_RTCPRE) / ticks;
    return (uint32_t)hz;
  }

fail:
  TIM11->CR1 = 0U;
  __HAL_RCC_TIM11_CLK_DISABLE();
  return 0U;
}

/**
  * @brief  HSE 주파수를 실측해 PLL을 자동 설정한다. 결과: SYSCLK 84 MHz, USB 48 MHz.
  *         순서: HSE 기동 → HSI-PLL 84 MHz 기동(측정용) → HSE 측정 → HSE-PLL로 교체.
  *         HSE가 없거나 측정 실패 시 HSI-PLL 84 MHz 유지 (USB는 정밀도 부족으로 불안정할 수 있음).
  */
static void SystemClock_Config_Auto(void)
{
  RCC_OscInitTypeDef osc = {0};
  RCC_ClkInitTypeDef clk = {0};
  uint8_t  hse_ok;
  uint32_t mhz;

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE2);

  /* 1) HSE 기동 시도 (크리스탈이 없으면 타임아웃 → hse_ok = 0) */
  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState       = RCC_HSE_ON;
  osc.PLL.PLLState   = RCC_PLL_NONE;
  hse_ok = (HAL_RCC_OscConfig(&osc) == HAL_OK) ? 1U : 0U;

  /* 2) HSI 기반 PLL로 먼저 84 MHz 기동 (16/16*336/4 = 84, /7 = 48) */
  osc.OscillatorType      = RCC_OSCILLATORTYPE_HSI;
  osc.HSIState            = RCC_HSI_ON;
  osc.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  osc.PLL.PLLState        = RCC_PLL_ON;
  osc.PLL.PLLSource       = RCC_PLLSOURCE_HSI;
  osc.PLL.PLLM            = 16;             /* 그대로 (HSI 16 MHz ÷ 16 = VCO 입력 1 MHz) */
  osc.PLL.PLLN            = 336;            /* SYSCLK = N÷P = 336÷4 = 84 MHz */
  osc.PLL.PLLP            = RCC_PLLP_DIV4;
  osc.PLL.PLLQ            = 7;              /* USB = N÷Q = 336÷7 = 48 MHz (필수) */
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) { Error_Handler(); }

  clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) { Error_Handler(); }

  if (hse_ok == 0U) { return; }                          /* HSE 없음 → HSI-PLL 유지 */

  /* 3) HSE 실측 → 정수 MHz 반올림 (HSI ±1% 오차로도 4~26 MHz 구분 충분) */
  hse_hz_measured = HSE_MeasureHz();
  mhz = (hse_hz_measured + 500000U) / 1000000U;
  if ((mhz < 4U) || (mhz > 26U))
  {
    hse_hz_measured = 0U;                                /* 측정 실패 → HSI-PLL 유지 */
    return;
  }

  /* 4) PLL 소스를 HSE로 교체: SYSCLK를 잠시 HSI로 돌린 뒤 PLL 재설정 */
  clk.ClockType    = RCC_CLOCKTYPE_SYSCLK;
  clk.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) { Error_Handler(); }

  osc.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  osc.HSEState       = RCC_HSE_ON;
  osc.PLL.PLLState   = RCC_PLL_ON;
  osc.PLL.PLLSource  = RCC_PLLSOURCE_HSE;
  osc.PLL.PLLM       = mhz;                              /* 그대로 (실측 크리스탈 MHz → VCO 입력 1 MHz) */
  osc.PLL.PLLN       = 336;                              /* 84 MHz — 위 2)블록과 반드시 같은 값 */
  osc.PLL.PLLP       = RCC_PLLP_DIV4;
  osc.PLL.PLLQ       = 7;                                /* USB 48 MHz */
  if (HAL_RCC_OscConfig(&osc) != HAL_OK) { Error_Handler(); }

  clk.ClockType      = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                     | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clk.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
  clk.AHBCLKDivider  = RCC_SYSCLK_DIV1;
  clk.APB1CLKDivider = RCC_HCLK_DIV2;
  clk.APB2CLKDivider = RCC_HCLK_DIV1;
  if (HAL_RCC_ClockConfig(&clk, FLASH_LATENCY_2) != HAL_OK) { Error_Handler(); }

  /* HAL은 컴파일 타임 상수 HSE_VALUE(hal_conf.h, 8 MHz)로 SystemCoreClock을 계산하므로
   * 실제 크리스탈이 8 MHz가 아니면 틀린다. 목표값으로 보정하고 SysTick을 다시 맞춘다. */
  SystemCoreClock = TARGET_SYSCLK_HZ;
  HAL_InitTick(TICK_INT_PRIORITY);
  clk_src_is_hse = 1U;
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
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
