/* SPDX-License-Identifier: GPL-3.0-only */
/* tests/lxr_ref: the velocity / LFO routing is not part of the voices' sound here (modNodeValue stays 1) */
#ifndef MODULATIONNODE_H_
#define MODULATIONNODE_H_
#include "stm32f7xx_hal.h"
typedef struct { float amount; } ModulationNode;
extern ModulationNode velocityModulators[6];
void modNode_updateValue(ModulationNode *m, float v);
#endif
