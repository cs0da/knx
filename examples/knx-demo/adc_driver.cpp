#include "adc_driver.h"

// ===== Calibration constants =====
#define VREFINT_CAL_ADDR   ((uint16_t*) (0x1FF80078UL))
#define VREFINT_CAL_VALUE  (*(VREFINT_CAL_ADDR))
#define VREFINT_CAL_VREF   (3.0f)

// ===== Channel map/order per group =====
#define ADC_NUM_CHANNELS   4
#define CH_IDX_PA3         0   // ADC_IN3
#define CH_IDX_PA4         1   // ADC_IN4
#define CH_IDX_PA5         2   // ADC_IN5
#define CH_IDX_VREF        3   // ADC_VREFINT

// How many DMA items to process per adcPollProcess() call
#ifndef ADC_PROCESS_SLICE
#define ADC_PROCESS_SLICE  32   // items (not groups); tune to your CPU load
#endif

// ===== Handles =====
static ADC_HandleTypeDef hadc1;
static DMA_HandleTypeDef hdma_adc1;
static TIM_HandleTypeDef htim6;

// ===== User-config params =====
static uint16_t g_groupsPerBatch = 0;
static float    g_dividerRatioInv  = 2.0f;
static float    g_shuntOhms        = 0.015f;
static float    g_monitorGain      = 50.0f;

// ===== DMA buffer (NORMAL mode) =====
#ifndef ADC_DMA_MAX_GROUPS
#define ADC_DMA_MAX_GROUPS 256
#endif
static uint16_t g_buf[ADC_NUM_CHANNELS * ADC_DMA_MAX_GROUPS];

// ===== Flags & processing state =====
static volatile bool g_dmaDone    = false;  // set in ISR on DMA complete
static bool           g_procBusy  = false;  // foreground processing active
static bool           g_resultReady = false;

// incremental processing indices & sums
static uint32_t g_totalItems   = 0;  // total DMA items to process
static uint32_t g_procIndex    = 0;  // next item index to process [0..g_totalItems)
static uint32_t g_sum_pa3      = 0;
static uint32_t g_sum_pa4      = 0;
static uint32_t g_sum_pa5      = 0;
static uint32_t g_sum_vref     = 0;

// last computed result
static AdcBatchResult g_last = {0};

// ===== Helpers =====
static uint32_t tim6ClockHz() { return HAL_RCC_GetPCLK1Freq(); }

static void timer6_config(uint32_t sampleRateHz) {
  __HAL_RCC_TIM6_CLK_ENABLE();

  uint32_t clk = tim6ClockHz();
  uint32_t target = 1000000UL;
  uint32_t presc = clk / target; if (!presc) presc = 1;
  uint32_t tick = clk / presc;
  uint32_t period = tick / sampleRateHz; if (!period) period = 1; period -= 1;

  htim6.Instance = TIM6;
  htim6.Init.Prescaler         = presc - 1;
  htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
  htim6.Init.Period            = period;
  htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  HAL_TIM_Base_Init(&htim6);

  TIM_MasterConfigTypeDef m = {0};
  m.MasterOutputTrigger = TIM_TRGO_UPDATE;
  m.MasterSlaveMode     = TIM_MASTERSLAVEMODE_DISABLE;
  HAL_TIMEx_MasterConfigSynchronization(&htim6, &m);
}

static void dma_init_normal() {
  __HAL_RCC_DMA1_CLK_ENABLE();
  hdma_adc1.Instance = DMA1_Channel1;
  hdma_adc1.Init.Direction           = DMA_PERIPH_TO_MEMORY;
  hdma_adc1.Init.PeriphInc           = DMA_PINC_DISABLE;
  hdma_adc1.Init.MemInc              = DMA_MINC_ENABLE;
  hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
  hdma_adc1.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
  hdma_adc1.Init.Mode                = DMA_NORMAL;      // single batch
  hdma_adc1.Init.Priority            = DMA_PRIORITY_LOW;
  HAL_DMA_Init(&hdma_adc1);
  __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);

  // Keep below UART priority (L0: 0..3 typical; but HAL allows 0..15; use higher number = lower prio)
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 7, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
  HAL_NVIC_SetPriority(ADC1_COMP_IRQn, 7, 0);
  HAL_NVIC_EnableIRQ(ADC1_COMP_IRQn);
}

static void gpio_init_pa3_pa4_pa5() {
  __HAL_RCC_GPIOA_CLK_ENABLE();
  GPIO_InitTypeDef g = {0};
  g.Mode = GPIO_MODE_ANALOG; g.Pull = GPIO_NOPULL;

  g.Pin = GPIO_PIN_3; HAL_GPIO_Init(GPIOA, &g); // PA3
  g.Pin = GPIO_PIN_4; HAL_GPIO_Init(GPIOA, &g); // PA4
  g.Pin = GPIO_PIN_5; HAL_GPIO_Init(GPIOA, &g); // PA5
}

static void adc_init_scan(uint32_t samplingTime) {
  // __HAL_RCC_HSI_ENABLE(); while (!__HAL_RCC_GET_FLAG(RCC_FLAG_HSIRDY)) {}
  __HAL_RCC_ADC1_CLK_ENABLE();
  HAL_ADCEx_EnableVREFINT();
  gpio_init_pa3_pa4_pa5();

  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler        = ADC_CLOCK_SYNC_PCLK_DIV2;   // or _DIV4 if needed
  hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode          = ADC_SCAN_DIRECTION_FORWARD; // L0 style
  hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;    // EOC per conversion (DMA gathers)
  hadc1.Init.LowPowerAutoWait      = DISABLE;
  hadc1.Init.ContinuousConvMode    = DISABLE;                // driven by TIM6
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv      = ADC_EXTERNALTRIGCONV_T6_TRGO;
  hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc1.Init.Overrun               = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.SamplingTime          = samplingTime;           // global on L0
  HAL_ADC_Init(&hadc1);

  ADC_ChannelConfTypeDef ch = {0};
  ch.Rank = ADC_RANK_CHANNEL_NUMBER;

  ch.Channel = ADC_CHANNEL_3;        HAL_ADC_ConfigChannel(&hadc1, &ch); // PA3
  ch.Channel = ADC_CHANNEL_4;        HAL_ADC_ConfigChannel(&hadc1, &ch); // PA4
  ch.Channel = ADC_CHANNEL_5;        HAL_ADC_ConfigChannel(&hadc1, &ch); // PA5
  ch.Channel = ADC_CHANNEL_VREFINT;  HAL_ADC_ConfigChannel(&hadc1, &ch); // VREF

  HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);
}

// ===== IRQs: tiny =====
extern "C" void DMA1_Channel1_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_adc1); }
extern "C" void ADC1_COMP_IRQHandler(void)     { HAL_ADC_IRQHandler(&hadc1);     }

// Called when the WHOLE DMA transfer is done:
extern "C" void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc) {
  if (hadc->Instance != ADC1) return;
  HAL_TIM_Base_Stop(&htim6);  // stop triggers
  g_dmaDone = true;           // signal foreground to start processing
}

// ===== Public API =====
void adcInitDualWithVref_NonBlocking(uint32_t sampleRateHz,
                                     uint16_t groupsPerBatch,
                                     uint32_t samplingTime,
                                     float dividerRatioInv,
                                     float shuntOhms,
                                     float monitorGain) {
  if (groupsPerBatch > ADC_DMA_MAX_GROUPS)
    groupsPerBatch = ADC_DMA_MAX_GROUPS;

  g_groupsPerBatch  = groupsPerBatch;
  g_dividerRatioInv = dividerRatioInv;
  g_shuntOhms       = shuntOhms;
  g_monitorGain     = monitorGain;

  timer6_config(sampleRateHz);
  adc_init_scan(samplingTime);
  dma_init_normal();

  // init state
  g_dmaDone = false;
  g_procBusy = false;
  g_resultReady = false;
}

void adcRequestBatch() {
  g_dmaDone = false;
  g_procBusy = false;
  g_resultReady = false;
  g_sum_pa3 = g_sum_pa4 = g_sum_pa5 = g_sum_vref = 0;
  g_procIndex = 0;

  g_totalItems = (uint32_t)g_groupsPerBatch * ADC_NUM_CHANNELS;

  HAL_ADC_Stop_DMA(&hadc1);
  HAL_ADC_Start_DMA(&hadc1, (uint32_t*)g_buf, g_totalItems);

  __HAL_TIM_SET_COUNTER(&htim6, 0);
  HAL_TIM_Base_Start(&htim6);
}

// Non-blocking: processes at most ADC_PROCESS_SLICE items per call.
void adcPollProcess() {
  if (!g_dmaDone) return;

  if (!g_procBusy && !g_resultReady) {
    g_procBusy = true;
  }
  if (!g_procBusy || g_resultReady) return;

  uint32_t end = g_procIndex + ADC_PROCESS_SLICE;
  if (end > g_totalItems) end = g_totalItems;

  // Interleaved groups: [PA3, PA4, PA5, VREF, ...]
  for (; g_procIndex < end; ++g_procIndex) {
    uint16_t sample = g_buf[g_procIndex];
    switch (g_procIndex % ADC_NUM_CHANNELS) {
      case CH_IDX_PA3:  g_sum_pa3  += sample; break;
      case CH_IDX_PA4:  g_sum_pa4  += sample; break;
      case CH_IDX_PA5:  g_sum_pa5  += sample; break;
      case CH_IDX_VREF: g_sum_vref += sample; break;
    }
  }

  if (g_procIndex < g_totalItems) return;

  // Finished all items: compute averages and engineering units
  uint32_t n = g_groupsPerBatch;
  uint32_t avg_pa3  = (g_sum_pa3  + n/2) / n;
  uint32_t avg_pa4  = (g_sum_pa4  + n/2) / n;
  uint32_t avg_pa5  = (g_sum_pa5  + n/2) / n;
  uint32_t avg_vref = (g_sum_vref + n/2) / n;
  if (avg_pa3  > 4095) avg_pa3  = 4095;
  if (avg_pa4  > 4095) avg_pa4  = 4095;
  if (avg_pa5  > 4095) avg_pa5  = 4095;
  if (avg_vref > 4095) avg_vref = 4095;

  // VDDA from VREFINT
  float vdda = VREFINT_CAL_VREF * (float)VREFINT_CAL_VALUE / (float)avg_vref;

  // Convert raw to volts using true VDDA
  float v_pa3  = vdda * (float)avg_pa3 / 4095.0f;
  float v_pa4  = vdda * (float)avg_pa4 / 4095.0f;
  float v_pa5  = vdda * (float)avg_pa5 / 4095.0f;

  // Main rail and shunt math
  float v_main      = v_pa3 * g_dividerRatioInv;
  float v_shunt_mon = v_pa4;
  float v_shunt     = v_shunt_mon / g_monitorGain;
  float i_shunt     = v_shunt / g_shuntOhms;

  g_last.vdda        = vdda;
  g_last.v_main      = v_main;
  g_last.v_pa3       = v_pa3;
  g_last.v_shunt_mon = v_shunt_mon;
  g_last.v_pa5       = v_pa5;
  g_last.i_shunt     = i_shunt;

  g_last.raw_pa3     = (uint16_t)avg_pa3;
  g_last.raw_pa4     = (uint16_t)avg_pa4;
  g_last.raw_pa5     = (uint16_t)avg_pa5;
  g_last.raw_vref    = (uint16_t)avg_vref;

  g_procBusy    = false;
  g_resultReady = true;
}

bool adcResultAvailable() { return g_resultReady; }

AdcBatchResult adcGetLastResult() {
  g_resultReady = false;
  return g_last;
}
