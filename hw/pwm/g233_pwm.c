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
 *
 * Register map (base 0x10015000):
 *   0x00  PWM_GLB       — global control/status
 *   0x10  PWM_CH0_CTRL  — channel 0 control
 *   0x14  PWM_CH0_PERIOD — channel 0 period
 *   0x18  PWM_CH0_DUTY  — channel 0 duty cycle
 *   0x1C  PWM_CH0_CNT   — channel 0 counter (read-only)
 *   0x20  PWM_CH1_CTRL  — channel 1 control
 *   ...
 */

#include "qemu/osdep.h"
#include "hw/pwm/g233_pwm.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"

static void g233_pwm_update_irq(G233PWMState *s)
{
    /* IRQ is asserted when any channel has DONE set and INTIE enabled */
    uint32_t irq_pending = 0;
    int i;

    for (i = 0; i < G233_PWM_NUM_CHANNELS; i++) {
        uint32_t done_bit = PWM_GLB_CH0_DONE << i;
        if ((s->glb & done_bit) && (s->ch[i].ctrl & PWM_CTRL_INTIE)) {
            irq_pending = 1;
            break;
        }
    }

    qemu_set_irq(s->irq, irq_pending);
}

static void g233_pwm_timer_cb(void *opaque)
{
    G233PWMState *s = G233_PWM(opaque);
    int i;

    for (i = 0; i < G233_PWM_NUM_CHANNELS; i++) {
        if (!(s->ch[i].ctrl & PWM_CTRL_EN)) {
            continue;
        }

        /* Advance counter */
        s->ch[i].cnt++;

        /* Check if period completed */
        if (s->ch[i].period > 0 && s->ch[i].cnt >= s->ch[i].period) {
            s->ch[i].cnt = 0;
            /* Set DONE flag */
            s->glb |= (PWM_GLB_CH0_DONE << i);
        }
    }

    /* Update IRQ */
    g233_pwm_update_irq(s);

    /* Re-arm timer for next tick */
    timer_mod(s->timer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
}

static uint64_t g233_pwm_read(void *opaque, hwaddr addr, unsigned int size)
{
    G233PWMState *s = G233_PWM(opaque);

    if (addr == PWM_GLB) {
        /* Update CHx_EN mirror bits from channel control registers */
        uint32_t glb = s->glb & ~0x0F; /* Clear mirror bits */
        int i;
        for (i = 0; i < G233_PWM_NUM_CHANNELS; i++) {
            if (s->ch[i].ctrl & PWM_CTRL_EN) {
                glb |= (PWM_GLB_CH0_EN << i);
            }
        }
        glb |= (s->glb & 0xF0); /* Preserve DONE bits */
        return glb;
    }

    /* Channel registers */
    if (addr >= 0x10 && addr < 0x10 + G233_PWM_NUM_CHANNELS * 0x10) {
        int ch = (addr - 0x10) / 0x10;
        int offset = (addr - 0x10) % 0x10;

        switch (offset) {
        case PWM_CH_CTRL:
            return s->ch[ch].ctrl;
        case PWM_CH_PERIOD:
            return s->ch[ch].period;
        case PWM_CH_DUTY:
            return s->ch[ch].duty;
        case PWM_CH_CNT:
            return s->ch[ch].cnt;
        default:
            break;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: read from invalid addr 0x%" HWADDR_PRIx "\n",
                  __func__, addr);
    return 0;
}

static void g233_pwm_write(void *opaque, hwaddr addr,
                           uint64_t val64, unsigned int size)
{
    G233PWMState *s = G233_PWM(opaque);
    uint32_t val = (uint32_t)val64;

    if (addr == PWM_GLB) {
        /* Write-1-to-clear for DONE bits */
        s->glb &= ~(val & 0xF0);
        g233_pwm_update_irq(s);
        return;
    }

    /* Channel registers */
    if (addr >= 0x10 && addr < 0x10 + G233_PWM_NUM_CHANNELS * 0x10) {
        int ch = (addr - 0x10) / 0x10;
        int offset = (addr - 0x10) % 0x10;

        switch (offset) {
        case PWM_CH_CTRL:
            s->ch[ch].ctrl = val & 0x7; /* Only bits 0-2 are valid */
            /* Start timer when any channel is enabled */
            if (s->ch[ch].ctrl & PWM_CTRL_EN) {
                timer_mod(s->timer,
                          qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + 1000);
            }
            break;
        case PWM_CH_PERIOD:
            s->ch[ch].period = val;
            break;
        case PWM_CH_DUTY:
            s->ch[ch].duty = val;
            break;
        case PWM_CH_CNT:
            /* Read-only */
            qemu_log_mask(LOG_GUEST_ERROR,
                          "%s: write to read-only PWM_CH%u_CNT\n",
                          __func__, ch);
            break;
        default:
            goto invalid;
        }
        return;
    }

invalid:
    qemu_log_mask(LOG_GUEST_ERROR,
                  "%s: write to invalid addr 0x%" HWADDR_PRIx "\n",
                  __func__, addr);
}

static const MemoryRegionOps g233_pwm_ops = {
    .read = g233_pwm_read,
    .write = g233_pwm_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void g233_pwm_init(Object *obj)
{
    G233PWMState *s = G233_PWM(obj);

    memory_region_init_io(&s->mmio, obj, &g233_pwm_ops, s,
                          TYPE_G233_PWM, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    /* Create timer for counter advancement */
    s->timer = timer_new_ns(QEMU_CLOCK_VIRTUAL, g233_pwm_timer_cb, s);
}

static void g233_pwm_reset_hold(Object *obj, ResetType type)
{
    G233PWMState *s = G233_PWM(obj);
    int i;

    s->glb = 0;
    for (i = 0; i < G233_PWM_NUM_CHANNELS; i++) {
        s->ch[i].ctrl = 0;
        s->ch[i].period = 0;
        s->ch[i].duty = 0;
        s->ch[i].cnt = 0;
    }

    /* Stop timer */
    timer_del(s->timer);
}

static void g233_pwm_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = g233_pwm_reset_hold;
}

static const TypeInfo g233_pwm_info = {
    .name = TYPE_G233_PWM,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233PWMState),
    .instance_init = g233_pwm_init,
    .class_init = g233_pwm_class_init,
};

static void g233_pwm_register_types(void)
{
    type_register_static(&g233_pwm_info);
}

type_init(g233_pwm_register_types)
