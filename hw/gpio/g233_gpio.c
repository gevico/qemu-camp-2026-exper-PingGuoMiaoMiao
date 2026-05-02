/*
 * QEMU G233 GPIO Controller
 *
 * Copyright (c) 2025 Chao Liu <chao.liu@yeah.net>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * GPIO register map (base 0x10012000):
 *   0x00  GPIO_DIR   — direction (0=input, 1=output)
 *   0x04  GPIO_OUT   — output data
 *   0x08  GPIO_IN    — input data (read-only; reflects OUT when DIR=output)
 *   0x0C  GPIO_IE    — interrupt enable
 *   0x10  GPIO_IS    — interrupt status (write-1-to-clear)
 *   0x14  GPIO_TRIG  — trigger type (0=edge, 1=level)
 *   0x18  GPIO_POL   — polarity (0=low/falling, 1=high/rising)
 */

#include "qemu/osdep.h"
#include "hw/gpio/g233_gpio.h"
#include "qemu/log.h"
#include "qemu/module.h"

static uint64_t g233_gpio_read(void *opaque, hwaddr addr, unsigned int size)
{
    G233GPIOState *s = G233_GPIO(opaque);

    switch (addr) {
    case 0x00: /* GPIO_DIR */
        return s->dir;
    case 0x04: /* GPIO_OUT */
        return s->out;
    case 0x08: /* GPIO_IN */
        /*
         * For output pins (dir=1): GPIO_IN reflects the output latch (GPIO_OUT).
         * For input pins (dir=0): GPIO_IN reflects the external pin level.
         * In qtest, external drive is not modeled, so input pins read 0.
         */
        return (s->out & s->dir);
    case 0x0C: /* GPIO_IE */
        return s->ie;
    case 0x10: /* GPIO_IS */
        return s->is;
    case 0x14: /* GPIO_TRIG */
        return s->trig;
    case 0x18: /* GPIO_POL */
        return s->pol;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: read from invalid addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }
}

static void g233_gpio_update_irq(G233GPIOState *s)
{
    /* Interrupt is asserted when any enabled pin has its status bit set */
    uint32_t active = s->is & s->ie;
    qemu_set_irq(s->irq, !!active);
}

static void g233_gpio_update_is(G233GPIOState *s)
{
    uint32_t old_is = s->is;

    /*
     * For level-triggered pins: IS reflects the current pin level
     * (active-high or active-low based on POL).
     */
    uint32_t level_pins = s->trig;
    if (level_pins) {
        uint32_t pin_level = (s->out & s->dir);
        uint32_t active_level;
        if (s->pol) {
            /* Active high */
            active_level = pin_level;
        } else {
            /* Active low */
            active_level = ~pin_level;
        }
        /* Set IS for level-triggered pins at active level */
        s->is |= active_level & level_pins;
        /* Clear IS for level-triggered pins NOT at active level */
        s->is &= ~(~active_level & level_pins);
    }

    if (old_is != s->is) {
        g233_gpio_update_irq(s);
    }
}

static void g233_gpio_write(void *opaque, hwaddr addr,
                            uint64_t val64, unsigned int size)
{
    G233GPIOState *s = G233_GPIO(opaque);
    uint32_t val = (uint32_t)val64;
    uint32_t old_out = s->out;
    uint32_t old_is = s->is;

    switch (addr) {
    case 0x00: /* GPIO_DIR */
        s->dir = val;
        g233_gpio_update_is(s);
        break;
    case 0x04: /* GPIO_OUT */
        s->out = val;

        /*
         * Edge-triggered interrupt detection:
         * For edge-triggered pins (trig=0), detect transitions on output pins.
         * Rising edge (pol=1): 0→1 transition sets IS
         * Falling edge (pol=0): 1→0 transition sets IS
         * Only set IS when the corresponding IE bit is enabled.
         */
        {
            uint32_t edge_pins = ~s->trig;
            uint32_t output_pins = s->dir;
            uint32_t edge_detect = edge_pins & output_pins;

            if (edge_detect) {
                uint32_t rising = ~old_out & s->out;
                uint32_t falling = old_out & ~s->out;

                if (s->pol) {
                    /* Rising edge active */
                    s->is |= rising & edge_detect & s->ie;
                } else {
                    /* Falling edge active */
                    s->is |= falling & edge_detect & s->ie;
                }
            }
        }

        /* Update level-triggered IS and notify IRQ if IS changed */
        g233_gpio_update_is(s);

        /* Notify IRQ for edge-triggered changes (update_is may not detect them) */
        g233_gpio_update_irq(s);
        break;
    case 0x08: /* GPIO_IN - read-only */
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to read-only GPIO_IN\n", __func__);
        break;
    case 0x0C: /* GPIO_IE */
        s->ie = val;
        g233_gpio_update_irq(s);
        break;
    case 0x10: /* GPIO_IS - write-1-to-clear */
        s->is &= ~val;
        if (old_is != s->is) {
            g233_gpio_update_irq(s);
        }
        break;
    case 0x14: /* GPIO_TRIG */
        s->trig = val;
        break;
    case 0x18: /* GPIO_POL */
        s->pol = val;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write to invalid addr 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps g233_gpio_ops = {
    .read = g233_gpio_read,
    .write = g233_gpio_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void g233_gpio_init(Object *obj)
{
    G233GPIOState *s = G233_GPIO(obj);

    memory_region_init_io(&s->mmio, obj, &g233_gpio_ops, s,
                          TYPE_G233_GPIO, 0x100);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void g233_gpio_reset_hold(Object *obj, ResetType type)
{
    G233GPIOState *s = G233_GPIO(obj);

    s->dir = 0;
    s->out = 0;
    s->in = 0;
    s->ie = 0;
    s->is = 0;
    s->trig = 0;
    s->pol = 0;
}

static void g233_gpio_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = g233_gpio_reset_hold;
}

static const TypeInfo g233_gpio_info = {
    .name = TYPE_G233_GPIO,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(G233GPIOState),
    .instance_init = g233_gpio_init,
    .class_init = g233_gpio_class_init,
};

static void g233_gpio_register_types(void)
{
    type_register_static(&g233_gpio_info);
}

type_init(g233_gpio_register_types)
