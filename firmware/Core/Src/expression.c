#include "expression.h"

#include "flash_midi_settings.h"
#include "midi_cmds.h"
#include "midi_defines.h"
#include "main.h"

#define EXP_PEDAL_COUNT        (2U)
#define EXP_SAMPLE_INTERVAL_MS (5U)
#define EXP_CHANGE_THRESHOLD   (2U)
#define EXP_DEADZONE_COUNTS    (8U)

extern ADC_HandleTypeDef hadc1;
#define EXP_ADC_HANDLE (&hadc1)
static const uint32_t kExpChannels[EXP_PEDAL_COUNT] = {ADC_CHANNEL_7, ADC_CHANNEL_8};
extern uint8_t f_sys_config_complete;
static const uint8_t kExpCcNumbers[EXP_PEDAL_COUNT] = {11U, 4U};

static uint8_t last_sent_value[EXP_PEDAL_COUNT];
static uint32_t next_sample_tick = 0U;

static uint16_t expression_read_channel(uint32_t channel)
{
  ADC_ChannelConfTypeDef sConfig = {0};
  sConfig.Channel = channel;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
  if (HAL_ADC_ConfigChannel(EXP_ADC_HANDLE, &sConfig) != HAL_OK) {
    return 0U;
  }

  if (HAL_ADC_Start(EXP_ADC_HANDLE) != HAL_OK) {
    return 0U;
  }

  if (HAL_ADC_PollForConversion(EXP_ADC_HANDLE, 10U) != HAL_OK) {
    HAL_ADC_Stop(EXP_ADC_HANDLE);
    return 0U;
  }

  uint16_t value = (uint16_t)HAL_ADC_GetValue(EXP_ADC_HANDLE);
  HAL_ADC_Stop(EXP_ADC_HANDLE);
  return value;
}

static uint8_t expression_adc_to_midi(uint16_t sample)
{
  if (sample <= EXP_DEADZONE_COUNTS) {
    sample = 0U;
  }

  if (sample > 4095U) {
    sample = 4095U;
  }

  uint32_t scaled = (uint32_t)sample * 127U;
  scaled += 2047U;
  scaled /= 4095U;
  if (scaled > 127U) {
    scaled = 127U;
  }
  return (uint8_t)scaled;
}

void expression_init(void)
{
  for (uint32_t i = 0; i < EXP_PEDAL_COUNT; i++) {
    last_sent_value[i] = 0xFFU;
  }
  next_sample_tick = 0U;
}

static uint8_t midi_channel(void)
{
  if (pGlobalSettings == NULL) {
    return 0U;
  }
  return pGlobalSettings[GLOBAL_SETTINGS_CHANNEL] & 0x0FU;
}

static uint8_t change_exceeds_threshold(uint8_t prev, uint8_t current)
{
  if (prev == 0xFFU) {
    return 1U;
  }
  uint8_t diff = (prev > current) ? (prev - current) : (current - prev);
  return (diff >= EXP_CHANGE_THRESHOLD) ? 1U : 0U;
}

void expression_task(void)
{
  if (!f_sys_config_complete) {
    return;
  }

  uint32_t now = HAL_GetTick();
  if (now < next_sample_tick) {
    return;
  }
  next_sample_tick = now + EXP_SAMPLE_INTERVAL_MS;
  uint8_t channel = midi_channel();

  for (uint32_t i = 0; i < EXP_PEDAL_COUNT; i++) {
    uint16_t raw = expression_read_channel(kExpChannels[i]);
    uint8_t midi_value = expression_adc_to_midi(raw);
    if (!change_exceeds_threshold(last_sent_value[i], midi_value)) {
      continue;
    }

    if (midiCmd_send_cc(channel, kExpCcNumbers[i], midi_value) == ERROR_BUFFERS_FULL) {
      continue;
    }

    last_sent_value[i] = midi_value;
  }
}
