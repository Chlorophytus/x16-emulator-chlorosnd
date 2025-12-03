// ChlorSND DSP
// Copyright (c) 2025 Roland Metivier
// All rights reserved. License: 3-clause BSD

#include "chlorsnd.h"
#include "chlorsnd_logarithm.h"

// NOTE: Preliminary/prototype specification.

// Direct form 2, non-transposed
struct chlorsnd_biquad_stage {
  int16_t pri_coefficients[5];
  int16_t pri_delays_L[3];
  int16_t pri_delays_R[3];
};

// Envelope Generator that lets you store amplitudes
struct chlorsnd_envg {
  // 0x0000
  int8_t multiplier;
  // 0x0001
  uint8_t hold_point;
  // 0x0002
  uint8_t stride;
  // 0x0008
  uint8_t entries[40];
  // 0x0030
  // --------
  uint8_t pri_current_entry;
  uint8_t pri_current_stride;
};

struct chlorsnd_channel {
  // 0x0000
  uint16_t control;

  // 0x0002
  int8_t volume_L;
  // 0x0003
  int8_t volume_R;
  // 0x0004
  uint16_t accumulator_max;
  // 0x0006
  uint8_t envg_used_amplitude;
  // 0x0007
  uint8_t envg_used_biquad_0_coeffs[5];
  // 0x000C
  uint8_t envg_used_biquad_1_coeffs[5];

  // 0x0018
  uint16_t loop_start;
  // 0x001A
  uint16_t loop_end;
  // 0x001C
  uint16_t run_start;
  // 0x001E
  uint16_t run_end;

  // 0x0020
  struct chlorsnd_biquad_stage biquad_stages[2];

  // 0x0028
  // --------
  uint16_t pri_accumulator;
  uint16_t pri_wavetable_read_ptr;
  uint8_t pri_envelope_position;
};

struct chlorsnd_dsp {
  // 0x0000
  uint16_t control;
  // 0x0002
  uint8_t channel_select_mask;
  // 0x0003
  uint8_t envg_select_mask;
  // 0x0004
  uint16_t wavetable_write_ptr;
  // 0x0006
  uint16_t wavetable_write_data;

  // 0x0010: envg port
  struct chlorsnd_envg envgs[8];
  // 0x0040: channel port
  struct chlorsnd_channel channels[8];

  // 0x0060
  // --------
  uint8_t pri_sample_ram[32768];
};

static struct chlorsnd_dsp chlorsnd;

void chlorsnd_init() { memset(chlorsnd, 0, sizeof chlorsnd); }

void chlorsnd_poke_envg(uint16_t reg, uint8_t value) {
  bool hi = (reg % 2) == 1;
  uint16_t which = 0;
  uint16_t which_envg = 0;
  for (uint16_t m = 1; m != (1 << 8); m <<= 1) {
    for (uint16_t n = 1; n != (1 << 8); n <<= 1) {
      if (((m & chlorsnd.channel_select_mask) != 0) &&
          ((n & chlorsnd.envg_select_mask) != 0)) {
        switch (reg) {
        case 0x0000: {
          chlorsnd.envgs[n].multiplier = *(int8_t *)&value;
          break;
        }
        case 0x0001: {
          chlorsnd.envgs[n].hold_point = value;
          break;
          break;
        }
        case 0x0002: {
          chlorsnd.envgs[n].stride = value;
          break;
          break;
        }
        default: {
          if (reg > 0x0007 && reg < 0x0030) {
            chlorsnd.envgs.entries[reg - 8] = value;
          }
          break;
        }
        }
      }
    }
  }
}

void chlorsnd_poke_channel(uint16_t reg, uint8_t value) {
  bool hi = (reg % 2) == 1;
  uint16_t which = 0;
  for (uint16_t m = 1; m != (1 << 8); m <<= 1) {
    if ((m & chlorsnd.channel_select_mask) != 0) {
      switch (reg) {
      case 0x0000:
      case 0x0001: {
        chlorsnd.channels[i].control =
            (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
        break;
      }
      case 0x0002: {
        chlorsnd.channels[i].volume_L = *(int8_t *)&value;
        break;
      }
      case 0x0003: {
        chlorsnd.channels[i].volume_R = *(int8_t *)&value;
        break;
      }
      case 0x0004:
      case 0x0005: {
        chlorsnd.channels[i].accumulator_max =
            (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
        break;
      }
      case 0x0006: {
        chlorsnd.channels[i].envg_used_amplitude = value;
        break;
      }
      case 0x0007:
      case 0x0008:
      case 0x0009:
      case 0x000A:
      case 0x000B: {
        chlorsnd.channels[i].envg_used_biquad_0_coeffs[reg - 7] = value;
        break;
      }
      case 0x000C:
      case 0x000D:
      case 0x000E:
      case 0x000F:
      case 0x0010: {
        chlorsnd.channels[i].envg_used_biquad_1_coeffs[reg - 12] = value;
        break;
      }
      case 0x0018:
      case 0x0019: {
        chlorsnd.channels[i].loop_start =
            (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
        break;
      }
      case 0x001A:
      case 0x001B: {
        chlorsnd.channels[i].loop_end =
            (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
        break;
      }
      case 0x001C:
      case 0x001D: {
        chlorsnd.channels[i].run_start =
            (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
        break;
      }
      case 0x001E:
      case 0x001F: {
        chlorsnd.channels[i].run_end =
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
  case 0x0000:
  case 0x0001: {
    chlorsnd.control =
        (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
  }
  case 0x0003: {
    chlorsnd.channel_select_mask = value;
    break;
  }
  case 0x0004: {
    chlorsnd.biquad_select_mask = value;
    break;
  }
  case 0x0004:
  case 0x0005: {
    chlorsnd.wavetable_write_ptr =
        (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
    break;
  }
  case 0x0006:
  case 0x0007: {
    chlorsnd.wavetable_write_ptr =
        (hi ? (((uint16_t)value) << 8) : (((uint16_t)value) << 0));
    break;
  }
  default: {
    if (reg > 0x000F && reg < 0x0040) {
      chlorsnd_poke_envg(reg - 8, value);
    }
    if (reg > 0x003F && reg < 0x005F) {
      chlorsnd_poke_channel(reg - 64, value);
    }
  }
  }
}

void chlorsnd_destroy() {}

uint16_t chlorsnd_render_biquad(chlorsnd_biquad_stage *s, uint16_t sample,
                                bool is_right) {
  // Assign delays
  if (is_right) {
    s->pri_delays_R[2] = -s->pri_delays_R[1];
    s->pri_delays_R[1] = -s->pri_delays_R[0];
    s->pri_delays_R[0] = sample;
  } else {
    s->pri_delays_L[2] = -s->pri_delays_L[1];
    s->pri_delays_L[1] = -s->pri_delays_L[0];
    s->pri_delays_L[0] = sample;
  }

  // Calculate left-hand value
  int32_t w = 0;
  int32_t coefficient = 32768;
  for (uint32_t i = 0; i < 3; i++) {
    // Sum
    if (is_right) {
      w += (s->pri_delays_R[i] * coefficient) / 32768;
    } else {
      w += (s->pri_delays_L[i] * coefficient) / 32768;
    }

    // Then saturate
    if (w > 32767) {
      w = 32767;
    } else if (w < -32767) {
      w = -32767;
    }

    // Assign next coefficient
    coefficient = s->pri_coefficients[i - 1];
    coefficient *= 32768;
  }

  // Calculate right-hand value
  int32_t y = 0;
  for (uint32_t i = 0; i < 3; i++) {
    // Assign coefficient
    coefficient = s->pri_coefficients[i + 2];
    coefficient *= 32768;

    // Sum
    y += (w * coefficient) / 32768;

    // Then saturate
    if (y > 32767) {
      y = 32767;
    } else if (y < -32767) {
      y = -32767;
    }
  }

  return y;
}

int16_t chlorsnd_render_envg(uint32_t e) {
  if (chlorsnd.envgs[e].pri_current_entry < 40) {
    if (chlorsnd.envgs[e].pri_current_stride < chlorsnd.envgs[e].pri_current_stride) {
      chlorsnd.envgs[e].pri_current_stride++;
    } else if (chlorsnd.envgs[e].pri_current_entry != chlorsnd.envgs[e].hold_point) {
      chlorsnd.envgs[e].pri_current_entry++;
      chlorsnd.envgs[e].pri_current_stride = 0;
    }
  }

  uint8_t entry = chlorsnd.envgs[e].pri_current_entry;
  int16_t current = chlorsnd.envgs[e].multiplier;
  current *= ENVG_LOGARITHM[chlorsnd.envgs[e].entries[entry]];

  return current;
}

int16_t chlorsnd_render_channel(uint32_t c, bool is_right) {
  
}

void chlorsnd_render(int16_t *stereo_buffer, uint32_t samples) {
  for (uint32_t i = 0; i < samples; i++) {
    int16_t L = 0;
    int16_t R = 0;
    for (uint32_t c = 0; c < 8; c++) {

      L += channel_L;
      R += channel_R;
    }

    // LEFT CHANNEL
    stereo_buffer[(i * 2) + 0] = L;

    // RIGHT CHANNEL
    stereo_buffer[(i * 2) + 1] = R;
  }
}