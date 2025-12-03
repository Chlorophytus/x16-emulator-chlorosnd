// ChlorSND DSP
// Copyright (c) 2025 Roland Metivier
// All rights reserved. License: 3-clause BSD

#ifndef _CHLORSND_H_
#define _CHLORSND_H_

#include <stdint.h>

void chlorsnd_init();
void chlorsnd_poke(uint16_t reg, uint8_t value);
void chlorsnd_render(int16_t *stereo_buffer, uint32_t samples);
void chlorsnd_destroy();

#endif