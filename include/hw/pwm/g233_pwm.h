/*
 * QEMU G233 PWM Controller
 *
 * Copyright (c) 2025 Chao Liu <chao.liu@yeah.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PWM controller with 4 independent channels (CH0~CH3).
 * Each channel has a 32-bit period counter, duty cycle counter,
 * configurable polarity, and period-complete interrupt.
 */

#ifndef HW_G233_PWM_H
#define HW_G233_PWM_H

#include "hw/core/sysbus.h"
#include "hw/core/irq.h"

#define TYPE_G233_PWM "g233.pwm"
OBJECT_DECLARE_SIMPLE_TYPE(G233PWMState, G233_PWM)

#define G233_PWM_NUM_CHANNELS 4

/* Global register */
#define PWM_GLB         0x00

/* Channel N register offsets (base = 0x10 + N * 0x10) */
#define PWM_CH_CTRL     0x00
#define PWM_CH_PERIOD   0x04
#define PWM_CH_DUTY     0x08
#define PWM_CH_CNT      0x0C

/* PWM_CH_CTRL bit definitions */
#define PWM_CTRL_EN     (1u << 0)
#define PWM_CTRL_POL    (1u << 1)
#define PWM_CTRL_INTIE  (1u << 2)

/* PWM_GLB bit definitions */
#define PWM_GLB_CH0_EN  (1u << 0)
#define PWM_GLB_CH1_EN  (1u << 1)
#define PWM_GLB_CH2_EN  (1u << 2)
#define PWM_GLB_CH3_EN  (1u << 3)
#define PWM_GLB_CH0_DONE (1u << 4)
#define PWM_GLB_CH1_DONE (1u << 5)
#define PWM_GLB_CH2_DONE (1u << 6)
#define PWM_GLB_CH3_DONE (1u << 7)

typedef struct G233PWMChannel {
    uint32_t ctrl;      /* PWM_CHn_CTRL */
    uint32_t period;    /* PWM_CHn_PERIOD */
    uint32_t duty;      /* PWM_CHn_DUTY */
    uint32_t cnt;       /* PWM_CHn_CNT (read-only) */
} G233PWMChannel;

typedef struct G233PWMState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    /* Global register */
    uint32_t glb;

    /* 4 channels */
    G233PWMChannel ch[G233_PWM_NUM_CHANNELS];

    /* QEMU timer for counter advancement */
    QEMUTimer *timer;
} G233PWMState;

#endif /* HW_G233_PWM_H */
