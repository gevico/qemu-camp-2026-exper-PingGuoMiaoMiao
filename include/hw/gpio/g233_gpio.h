/*
 * QEMU G233 GPIO Controller
 *
 * Copyright (c) 2025 Chao Liu <chao.liu@yeah.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_G233_GPIO_H
#define HW_G233_GPIO_H

#include "hw/core/sysbus.h"
#include "hw/core/irq.h"

#define TYPE_G233_GPIO "g233.gpio"
OBJECT_DECLARE_SIMPLE_TYPE(G233GPIOState, G233_GPIO)

#define G233_GPIO_NUM_PINS 32

typedef struct G233GPIOState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    /* Registers */
    uint32_t dir;    /* GPIO_DIR  - direction */
    uint32_t out;    /* GPIO_OUT  - output data */
    uint32_t in;     /* GPIO_IN   - input data (read-only) */
    uint32_t ie;     /* GPIO_IE   - interrupt enable */
    uint32_t is;     /* GPIO_IS   - interrupt status (w1c) */
    uint32_t trig;   /* GPIO_TRIG - trigger type (0=edge, 1=level) */
    uint32_t pol;    /* GPIO_POL  - polarity (0=low/falling, 1=high/rising) */
} G233GPIOState;

#endif /* HW_G233_GPIO_H */
