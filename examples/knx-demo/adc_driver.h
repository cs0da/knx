#pragma once
#include <Arduino.h>
extern "C" {
  #include "stm32l0xx_hal.h"
}

typedef struct {
  float    vdda;         // computed VDDA from VREFINT [V]
  float    v_main;       // main rail [V] from PA3 + divider ratio
  float    v_pa3;        // PA3 node voltage [V]
  float    v_shunt_mon;  // PA4 shunt monitor output [V]
  float    v_pa5;        // PA5 measured voltage [V]
  float    i_shunt;      // computed current [A]
  uint16_t raw_pa3;      // averaged raw counts
  uint16_t raw_pa4;
  uint16_t raw_pa5;
  uint16_t raw_vref;
} AdcBatchResult;

// Initialize ADC on PA3 (CH3), PA4 (CH4), PA5 (CH5) and VREFINT with TIM6 trigger + DMA (NORMAL).
// The result computation is incremental/non-blocking via adcPollProcess().
// NOTE: 'groupsPerBatch' = number of 4-sample groups [PA3,PA4,PA5,VREF] to capture per batch.
void adcInitDualWithVref_NonBlocking(uint32_t sampleRateHz,
                                     uint16_t groupsPerBatch,
                                     uint32_t samplingTime,
                                     float dividerRatioInv,
                                     float shuntOhms,
                                     float monitorGain);

// Start a new batch acquisition (fills DMA buffer once). Returns immediately.
void adcRequestBatch();

// Call frequently in loop(): processes the DMA buffer incrementally (non-blocking).
void adcPollProcess();

// True when a fully processed, compensated result is ready to read.
bool adcResultAvailable();

// Get the last completed result; clears the ready flag.
AdcBatchResult adcGetLastResult();
