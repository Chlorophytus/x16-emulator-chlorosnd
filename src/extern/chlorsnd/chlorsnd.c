// ChlorSND DSP
// Copyright (c) 2025 Roland Metivier
// All rights reserved. License: 3-clause BSD

#include "chlorsnd.h"
#include "chlorsnd_logarithm.h"

// NOTE: Preliminary/prototype specification.

// Direct form 2, non-transposed
struct chlorsnd_biquad_stage {
  int16_t pri_coefficients[5];
  int32_t pri_delays_L[2];
  int32_t pri_delays_R[2];
};

// Envelope Generator that lets you store amplitudes
struct chlorsnd_envg {
  uint8_t control;
  // 0x0001
  int8_t multiplier;
  // 0x0002
  uint8_t hold_point;
  // 0x0003
  uint8_t stride;
  // 0x0080
  uint8_t entries[CHLORSND_ENVG_MEMORY];
  // 0x0100
  // --------
  uint8_t pri_current_entry;
  uint8_t pri_current_stride;
  uint8_t pri_step_queued;
};

struct chlorsnd_channel {
  // 0x0000
  uint8_t control;

  // 0x0002
  int8_t volume_L;
  // 0x0003
  int8_t volume_R;
  // 0x0004
  uint16_t accumulator_max;
  // 0x0006
  uint16_t envg_used_amplitude;
  // 0x0007
  uint8_t envg_used_biquads_coeffs[10];

  // 0x0018
  uint16_t loop_start;
  // 0x001A
  uint16_t loop_end;

  // 0x0020
  struct chlorsnd_biquad_stage biquad_stages[2];

  // 0x0028
  // --------
  uint16_t pri_accumulator;
  uint16_t pri_wavetable_read_ptr;
};

struct chlorsnd_dsp {
  // 0x0000
  uint8_t control;
  // 0x0001
  uint8_t channel_select_mask;
  // 0x0002
  uint16_t envg_select_mask;
  // 0x0004
  uint16_t write_ptr;
  // 0x0006
  uint16_t write_data;

  // 0x0008: Channel 0 control register
  // 0x0009: Channel 1 control register
  // 0x000A: Channel 2 control register
  // 0x000B: Channel 3 control register
  // 0x000C: Channel 4 control register
  // 0x000D: Channel 5 control register
  // 0x000E: Channel 6 control register
  // 0x000F: Channel 7 control register

  // 0x0010: ENVG  0 control register
  // 0x0011: ENVG  1 control register
  // 0x0012: ENVG  2 control register
  // 0x0013: ENVG  3 control register
  // 0x0014: ENVG  4 control register
  // 0x0015: ENVG  5 control register
  // 0x0016: ENVG  6 control register
  // 0x0017: ENVG  7 control register
  // 0x0018: ENVG  8 control register
  // 0x0019: ENVG  9 control register
  // 0x001A: ENVG 10 control register
  // 0x001B: ENVG 11 control register
  // 0x001C: ENVG 12 control register
  // 0x001D: ENVG 13 control register
  // 0x001E: ENVG 14 control register
  // 0x001F: ENVG 15 control register

  struct chlorsnd_envg envgs[16];
  struct chlorsnd_channel channels[8];
  // --------
  int8_t pri_sample_ram[CHLORSND_SAMPLE_MEMORY];
};

static struct chlorsnd_dsp chlorsnd;

void chlorsnd_init() { memset(chlorsnd, 0, sizeof chlorsnd); }

void chlorsnd_poke_envg(uint16_t reg, uint8_t value) {
  uint32_t which = 0;
  for (uint32_t m = 1; m != (1 << 16); m <<= 1) {
    if ((m & chlorsnd.envg_select_mask) != 0) {
      switch (reg) {
      case 0x0000: {
        chlorsnd.envgs[which].control = value;
        break;
      }
      case 0x0001: {
        chlorsnd.envgs[which].multiplier = *(int8_t *)&value;
        break;
      }
      case 0x0002: {
        chlorsnd.envgs[which].hold_point = value;
        break;
      }
      case 0x0003: {
        chlorsnd.envgs[which].stride = value;
        break;
      }
      default: {
        if (reg >= CHLORSND_ENVG_MEMORY_OFFSET && reg < (CHLORSND_ENVG_MEMORY + CHLORSND_ENVG_MEMORY_OFFSET)) {
          chlorsnd.envgs[which].entries[reg - CHLORSND_ENVG_MEMORY_OFFSET] = value;
        }
        break;
      }
      }
    }
    which++;
  }
}

void chlorsnd_poke_channel(uint16_t reg, uint8_t value) {
  bool hi = (reg % 2) == 1;
  uint32_t which = 0;
  for (uint32_t m = 1; m != (1 << 8); m <<= 1) {
    if ((m & chlorsnd.channel_select_mask) != 0) {
      switch (reg) {
      case 0x0000: {
        chlorsnd.channels[which].control = value;
        if ((chlorsnd.channels[which].control & 0x02) != 0) {
          chlorsnd.channels[which].accumulator_max = 0;
        }
        if ((chlorsnd.channels[which].control & 0x04) != 0) {
          chlorsnd.channels[which].loop_start = 0;
        }
        if ((chlorsnd.channels[which].control & 0x08) != 0) {
          chlorsnd.channels[which].loop_end = 0;
        }
        break;
      }
      case 0x0002: {
        chlorsnd.channels[which].volume_L = *(int8_t *)&value;
        break;
      }
      case 0x0003: {
        chlorsnd.channels[which].volume_R = *(int8_t *)&value;
        break;
      }
      case 0x0004:
      case 0x0005: {
        chlorsnd.channels[which].accumulator_max |=
            (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
        break;
      }
      case 0x0006: {
        chlorsnd.channels[which].envg_used_amplitude = value;
        break;
      }
      case 0x0007:
      case 0x0008:
      case 0x0009:
      case 0x000A:
      case 0x000B:
      case 0x000C:
      case 0x000D:
      case 0x000E:
      case 0x000F:
      case 0x0010: {
        chlorsnd.channels[which].envg_used_biquads_coeffs[reg - 7] = value % 16;
        break;
      }
      case 0x0018:
      case 0x0019: {
        chlorsnd.channels[which].loop_start |=
            (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
        break;
      }
      case 0x001A:
      case 0x001B: {
        chlorsnd.channels[which].loop_end |=
            (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
        break;
      }
      default: {
        break;
      }
      }
    }
    which++;
  }
}

void chlorsnd_poke(uint16_t reg, uint8_t value) {
  bool hi = (reg % 2) == 1;
  switch (reg) {
  case 0x0000: {
    chlorsnd.control = value;
    if ((chlorsnd.control & 0x01) != 0) {
      // ENVG mask should be reset
      chlorsnd.envg_select_mask = 0;
    }
    if ((chlorsnd.control & 0x02) != 0) {
      // MUX write pointer should be reset
      chlorsnd.write_ptr = 0;
    }
    if ((chlorsnd.control & 0x04) != 0) {
      // MUX write data should be reset
      chlorsnd.write_data = 0;
    }
    if ((chlorsnd.control & 0x10) != 0) {
      // Write Enable ENVG
      chlorsnd_poke_envg(chlorsnd.write_ptr, value);
    }
    if ((chlorsnd.control & 0x20) != 0) {
      // Write Enable Channel
      chlorsnd_poke_channel(chlorsnd.write_ptr, value);
    }
    if ((chlorsnd.control & 0x80) != 0) {
      // Write Enable Sample RAM
      chlorsnd.pri_sample_ram[chlorsnd.write_ptr % CHLORSND_SAMPLE_MEMORY] =
          *(int8_t *)&value;
    }
    break;
  }
  case 0x0001: {
    chlorsnd.channel_select_mask = value;
    break;
  }
  case 0x0002:
  case 0x0003: {
    chlorsnd.envg_select_mask |=
        (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
    break;
  }
  case 0x0004:
  case 0x0005: {
    chlorsnd.write_ptr |=
        (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
    break;
  }
  case 0x0006:
  case 0x0007: {
    chlorsnd.write_data |=
        (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
    break;
  }
  default: {
    break;
  }
  }
}

uint8_t chlorsnd_peek(uint16_t reg) {
  switch (reg) {
  case 0x0000: {
    return chlorsnd.control;
  }
  case 0x0008:
  case 0x0009:
  case 0x000A:
  case 0x000B:
  case 0x000C:
  case 0x000D:
  case 0x000E:
  case 0x000F: {
    return chlorsnd.channels[reg - 8].control;
  }
  case 0x0010:
  case 0x0011:
  case 0x0012:
  case 0x0013:
  case 0x0014:
  case 0x0015:
  case 0x0016:
  case 0x0017:
  case 0x0018:
  case 0x0019:
  case 0x001A:
  case 0x001B:
  case 0x001C:
  case 0x001D:
  case 0x001E:
  case 0x001F: {
    return chlorsnd.envgs[reg - 16].control;
  }
  default: {
    return 0xff;
  }
  }
}

void chlorsnd_destroy() {}

int32_t saturate_into_int16(int32_t what) {
  if(what > 32767) {
    return 32767;
  }

  if(what < -32767) {
    return -32767;
  }

  return what;
}

int16_t chlorsnd_render_biquad(chlorsnd_biquad_stage *s, int16_t sample,
                               bool is_right) {
  // Assign delays
  int32_t a = 0;
  int32_t b = 0;
  if (is_right) {
    a -= s->pri_delays_R[1] * (s->pri_coefficients[1] << 8);
    a >>= 16;
    a = saturate_into_int16(a);
    a -= s->pri_delays_R[0] * (s->pri_coefficients[0] << 8);
    a >>= 16;
    a = saturate_into_int16(a);
    a += sample;
    a = saturate_into_int16(a);

    b += s->pri_delays_R[1] * (s->pri_coefficients[4] << 8);
    b >>= 16;
    b = saturate_into_int16(b);
    b += s->pri_delays_R[0] * (s->pri_coefficients[3] << 8);
    b >>= 16;
    b = saturate_into_int16(b);
    b += a * (s->pri_coefficients[2] << 8);
    b >>= 16;
    b = saturate_into_int16(b);

    s->pri_delays_R[1] = s->pri_delays_R[0];
    s->pri_delays_R[0] = a;
  } else {
    a -= s->pri_delays_L[1] * (s->pri_coefficients[1] << 8);
    a >>= 16;
    a = saturate_into_int16(a);
    a -= s->pri_delays_L[0] * (s->pri_coefficients[0] << 8);
    a >>= 16;
    a = saturate_into_int16(a);
    a += sample;
    a = saturate_into_int16(a);

    b += s->pri_delays_L[1] * (s->pri_coefficients[4] << 8);
    b >>= 16;
    b = saturate_into_int16(b);
    b += s->pri_delays_L[0] * (s->pri_coefficients[3] << 8);
    b >>= 16;
    b = saturate_into_int16(b);
    b += a * (s->pri_coefficients[2] << 8);
    b >>= 16;
    b = saturate_into_int16(b);

    s->pri_delays_L[1] = s->pri_delays_L[0];
    s->pri_delays_L[0] = a;
  }
  
  return saturate_into_int16(b);
}

int16_t chlorsnd_render_envg(uint32_t e, bool use_exponent) {
  bool key_reset = (chlorsnd.envgs[e].control & 0x01) != 0;
  if (key_reset) {
    chlorsnd.envgs[e].control ^= 0x01;
    chlorsnd.envgs[e].pri_current_entry = 0;
  }

  if (chlorsnd.envgs[e].pri_step_queued &&
      (chlorsnd.envgs[e].pri_current_entry < CHLORSND_ENVG_MEMORY)) {
    bool key_on = (chlorsnd.envgs[e].control & 0x02) != 0;
    if (chlorsnd.envgs[e].pri_current_stride <
        chlorsnd.envgs[e].pri_current_stride) {
      chlorsnd.envgs[e].pri_current_stride++;
    } else if (!key_on || (chlorsnd.envgs[e].pri_current_entry !=
                           chlorsnd.envgs[e].hold_point)) {
      chlorsnd.envgs[e].pri_current_entry++;
      chlorsnd.envgs[e].pri_current_stride = 0;
    }
    chlorsnd.envgs[e].pri_step_queued = 0;
  }

  uint8_t entry = chlorsnd.envgs[e].pri_current_entry;
  int16_t current = chlorsnd.envgs[e].multiplier;

  if (use_exponent) {
    // When using an envelope generator for an amplitude, it is exponential.
    current *= ENVG_LOGARITHM[chlorsnd.envgs[e].entries[entry]];
  } else {
    // When using an envelope generator for a filter, it is linear.
    current *= chlorsnd.envgs[e].entries[entry];
  }

  return current;
}

int16_t chlorsnd_render_channel(uint32_t c, bool is_right) {
  bool key_reset = (chlorsnd.channels[c].control & 0x01) != 0;
  if (key_reset || (chlorsnd.channels[c].pri_wavetable_read_ptr >
                    (chlorsnd.channels[c].loop_end % CHLORSND_SAMPLE_MEMORY))) {
    chlorsnd.channels[c].pri_wavetable_read_ptr =
        chlorsnd.channels[c].loop_start;
    chlorsnd.channels[c].pri_wavetable_read_ptr %= CHLORSND_SAMPLE_MEMORY;
  }
  chlorsnd.channels[c].control = 0x00;

  int16_t sample =
      chlorsnd.pri_sample_ram[chlorsnd.channels[c].pri_wavetable_read_ptr];
  sample *= 256;

  if (chlorsnd.channels[c].pri_wavetable_read_ptr)
    // Step sample
    if (is_right) {
      sample *= chlorsnd.channels[c].volume_R;

      if (chlorsnd.channels[c].pri_accumulator == 0) {
        chlorsnd.channels[c].pri_wavetable_read_ptr++;
        chlorsnd.channels[c].pri_wavetable_read_ptr %= CHLORSND_SAMPLE_MEMORY;

        chlorsnd.channels[c].pri_accumulator =
            chlorsnd.channels[c].accumulator_max;
      } else {
        chlorsnd.channels[c].pri_accumulator--;
      }
    } else {
      sample *= chlorsnd.channels[c].volume_L;
    }

  // Render filter coeffs by dedicated ENVG
  int16_t coeffs[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  for (uint32_t i = 0; i < 10; i++) {
    coeffs[i] = chlorsnd_render_envg(
        chlorsnd.channels[c].envg_used_biquads_coeffs[i], false);
  }
  for (uint32_t f0_c; f0_c < 5; f0_c++) {
    chlorsnd.channels[c].biquad_stages[0].pri_coefficients[f0_c] =
        coeffs[f0_c + 0];
  }
  for (uint32_t f1_c; f1_c < 5; f1_c++) {
    chlorsnd.channels[c].biquad_stages[1].pri_coefficients[f1_c] =
        coeffs[f1_c + 5];
  }

  // Step filters
  sample = chlorsnd_render_biquad(&chlorsnd.channels[c].biquad_stages[0],
                                  sample, is_right);
  sample = chlorsnd_render_biquad(&chlorsnd.channels[c].biquad_stages[1],
                                  sample, is_right);

  // Render amplitude by dedicated ENVG
  int16_t envg_amplitude =
      chlorsnd_render_envg(chlorsnd.channels[c].envg_used_amplitude, true);
  sample /= 256;
  sample *= envg_amplitude;

  // Key should be reset.
  if (key_reset) {
    chlorsnd.channels[c].control ^= 0x01;
  }

  if ((chlorsnd.channels[c].control & 0x80) != 0) {
    chlorsnd.channels[c].control ^= 0x80;
  }
}

void chlorsnd_render(int16_t *stereo_buffer, uint32_t samples) {
  // Some control registers get reset for peeking
  chlorsnd.control = 0x00;

  for (uint32_t i = 0; i < samples; i++) {
    int16_t L = 0;
    int16_t R = 0;

    // Pre-step ENVGs. Important so we don't step any twice.
    uint16_t envg_step_mask = 0;
    for (uint32_t c = 0; c < 8; c++) {
      uint32_t previous_envgs_used[11] = {
          chlorsnd.channels[c].envg_used_amplitude,
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          0,
          0};

      for (uint32_t e = 1; e < 11; e++) {
        previous_envgs_used[e] =
            chlorsnd.channels[c].envg_used_biquads_coeffs[e - 1];
      }

      for (uint32_t e = 0; e < 11; e++) {
        envg_step_mask |= (1 << previous_envgs_used[e]);
      }
    }

    // Step ENVGs.
    for (uint32_t e = 0; e < 16; e++) {
      if ((envg_step_mask & (1 << e)) != 0) {
        chlorsnd.envgs[e].pri_step_queued = 1;
      }
    }

    for (uint32_t c = 0; c < 8; c++) {
      L += chlorsnd_render_channel(c, false) / 8;
      R += chlorsnd_render_channel(c, true) / 8;
    }

    // LEFT CHANNEL
    stereo_buffer[(i * 2) + 0] = L;

    // RIGHT CHANNEL
    stereo_buffer[(i * 2) + 1] = R;
  }
}