/*
 * QTest: G233 PWM controller — basic functionality
 *
 * Copyright (c) 2025 Chao Liu <chao.liu@yeah.net>
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * PWM register map (base 0x10015000):
 *   0x00  PWM_GLB       — global control/status
 *   0x10  PWM_CH0_CTRL  — channel 0 control
 *   0x14  PWM_CH0_PERIOD — channel 0 period
 *   0x18  PWM_CH0_DUTY  — channel 0 duty cycle
 *   0x1C  PWM_CH0_CNT   — channel 0 counter (read-only)
 *   0x20  PWM_CH1_CTRL  — channel 1 control
 *   ...
 *
 * PWM_CHn_CTRL bits:
 *   bit 0: EN    — channel enable
 *   bit 1: POL   — output polarity (0=high active, 1=inverted)
 *   bit 2: INTIE — period complete interrupt enable
 *
 * PWM_GLB bits:
 *   bits 3:0 — CHx_EN  (read-only mirror of channel enable)
 *   bits 7:4 — CHx_DONE (write-1-to-clear)
 */

#include "qemu/osdep.h"
#include "libqtest.h"

#define PWM_BASE    0x10015000ULL

#define PWM_GLB     (PWM_BASE + 0x00)

#define PWM_CH0_CTRL    (PWM_BASE + 0x10)
#define PWM_CH0_PERIOD  (PWM_BASE + 0x14)
#define PWM_CH0_DUTY    (PWM_BASE + 0x18)
#define PWM_CH0_CNT     (PWM_BASE + 0x1C)

#define PWM_CH1_CTRL    (PWM_BASE + 0x20)
#define PWM_CH1_PERIOD  (PWM_BASE + 0x24)
#define PWM_CH1_DUTY    (PWM_BASE + 0x28)
#define PWM_CH1_CNT     (PWM_BASE + 0x2C)

#define PWM_CH2_CTRL    (PWM_BASE + 0x30)
#define PWM_CH2_PERIOD  (PWM_BASE + 0x34)
#define PWM_CH2_DUTY    (PWM_BASE + 0x38)
#define PWM_CH2_CNT     (PWM_BASE + 0x3C)

#define PWM_CH3_CTRL    (PWM_BASE + 0x40)
#define PWM_CH3_PERIOD  (PWM_BASE + 0x44)
#define PWM_CH3_DUTY    (PWM_BASE + 0x48)
#define PWM_CH3_CNT     (PWM_BASE + 0x4C)

/* PWM_CHn_CTRL bit definitions */
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

/*
 * test_pwm_config: Configure CH0 PERIOD=1000, DUTY=500,
 *                  verify registers read back correctly.
 */
static void test_pwm_config(void)
{
    QTestState *qts = qtest_init("-machine g233 -m 2G");

    /* Write period and duty */
    qtest_writel(qts, PWM_CH0_PERIOD, 1000);
    qtest_writel(qts, PWM_CH0_DUTY, 500);

    /* Verify readback */
    g_assert_cmpuint(qtest_readl(qts, PWM_CH0_PERIOD), ==, 1000);
    g_assert_cmpuint(qtest_readl(qts, PWM_CH0_DUTY), ==, 500);

    /* Verify other registers are still at reset value */
    g_assert_cmpuint(qtest_readl(qts, PWM_CH0_CTRL), ==, 0);
    g_assert_cmpuint(qtest_readl(qts, PWM_CH0_CNT), ==, 0);

    qtest_quit(qts);
}

/*
 * test_pwm_enable: Set PWM_CH0_CTRL.EN, verify PWM_GLB.CH0_EN
 *                  mirror bit is set.
 */
static void test_pwm_enable(void)
{
    QTestState *qts = qtest_init("-machine g233 -m 2G");

    /* Enable channel 0 */
    qtest_writel(qts, PWM_CH0_CTRL, PWM_CTRL_EN);

    /* Verify CH0_EN mirror bit in GLB */
    uint32_t glb = qtest_readl(qts, PWM_GLB);
    g_assert_cmpuint(glb & PWM_GLB_CH0_EN, ==, PWM_GLB_CH0_EN);

    /* Verify other channels are not enabled */
    g_assert_cmpuint(glb & PWM_GLB_CH1_EN, ==, 0);
    g_assert_cmpuint(glb & PWM_GLB_CH2_EN, ==, 0);
    g_assert_cmpuint(glb & PWM_GLB_CH3_EN, ==, 0);

    qtest_quit(qts);
}

/*
 * test_pwm_counter: Start CH0 and read PWM_CH0_CNT,
 *                   verify the counter is incrementing.
 */
static void test_pwm_counter(void)
{
    QTestState *qts = qtest_init("-machine g233 -m 2G");

    /* Set a large period so counter doesn't wrap quickly */
    qtest_writel(qts, PWM_CH0_PERIOD, 0xFFFFFFFF);
    qtest_writel(qts, PWM_CH0_DUTY, 0);

    /* Enable channel */
    qtest_writel(qts, PWM_CH0_CTRL, PWM_CTRL_EN);

    /* Read counter - should be 0 initially */
    uint32_t cnt1 = qtest_readl(qts, PWM_CH0_CNT);
    g_assert_cmpuint(cnt1, ==, 0);

    /* Advance virtual clock by 1ms (1000ns per tick = 1000 ticks) */
    qtest_clock_step(qts, 1000000);

    uint32_t cnt2 = qtest_readl(qts, PWM_CH0_CNT);
    g_assert_cmpuint(cnt2, >, cnt1);

    qtest_quit(qts);
}

/*
 * test_pwm_done_flag: Wait for counter to complete one period,
 *                     verify PWM_GLB.CH0_DONE is set.
 */
static void test_pwm_done_flag(void)
{
    QTestState *qts = qtest_init("-machine g233 -m 2G");

    /* Set a small period so it completes quickly */
    qtest_writel(qts, PWM_CH0_PERIOD, 10);
    qtest_writel(qts, PWM_CH0_DUTY, 5);

    /* Enable channel */
    qtest_writel(qts, PWM_CH0_CTRL, PWM_CTRL_EN);

    /* Advance virtual clock enough for 11 timer ticks (period=10, 1000ns/tick) */
    qtest_clock_step(qts, 11000);

    /* Verify DONE flag is set */
    uint32_t glb = qtest_readl(qts, PWM_GLB);
    g_assert_cmpuint(glb & PWM_GLB_CH0_DONE, ==, PWM_GLB_CH0_DONE);

    qtest_quit(qts);
}

/*
 * test_pwm_done_clear: Write 1 to PWM_GLB.CH0_DONE to clear,
 *                      verify flag is reset.
 */
static void test_pwm_done_clear(void)
{
    QTestState *qts = qtest_init("-machine g233 -m 2G");

    /* Set a small period */
    qtest_writel(qts, PWM_CH0_PERIOD, 10);
    qtest_writel(qts, PWM_CH0_DUTY, 5);

    /* Enable channel */
    qtest_writel(qts, PWM_CH0_CTRL, PWM_CTRL_EN);

    /* Advance virtual clock enough for period completion */
    qtest_clock_step(qts, 11000);

    /* Verify DONE is set */
    uint32_t glb = qtest_readl(qts, PWM_GLB);
    g_assert_cmpuint(glb & PWM_GLB_CH0_DONE, ==, PWM_GLB_CH0_DONE);

    /* Write 1 to clear DONE */
    qtest_writel(qts, PWM_GLB, PWM_GLB_CH0_DONE);

    /* Verify DONE is cleared */
    glb = qtest_readl(qts, PWM_GLB);
    g_assert_cmpuint(glb & PWM_GLB_CH0_DONE, ==, 0);

    qtest_quit(qts);
}

/*
 * test_pwm_multi_channel: Configure CH0-CH3 with different
 *                         period/duty values, verify channel
 *                         independence.
 */
static void test_pwm_multi_channel(void)
{
    QTestState *qts = qtest_init("-machine g233 -m 2G");

    /* Configure each channel with different values */
    qtest_writel(qts, PWM_CH0_PERIOD, 100);
    qtest_writel(qts, PWM_CH0_DUTY, 50);

    qtest_writel(qts, PWM_CH1_PERIOD, 200);
    qtest_writel(qts, PWM_CH1_DUTY, 100);

    qtest_writel(qts, PWM_CH2_PERIOD, 300);
    qtest_writel(qts, PWM_CH2_DUTY, 150);

    qtest_writel(qts, PWM_CH3_PERIOD, 400);
    qtest_writel(qts, PWM_CH3_DUTY, 200);

    /* Verify each channel's registers are independent */
    g_assert_cmpuint(qtest_readl(qts, PWM_CH0_PERIOD), ==, 100);
    g_assert_cmpuint(qtest_readl(qts, PWM_CH0_DUTY), ==, 50);

    g_assert_cmpuint(qtest_readl(qts, PWM_CH1_PERIOD), ==, 200);
    g_assert_cmpuint(qtest_readl(qts, PWM_CH1_DUTY), ==, 100);

    g_assert_cmpuint(qtest_readl(qts, PWM_CH2_PERIOD), ==, 300);
    g_assert_cmpuint(qtest_readl(qts, PWM_CH2_DUTY), ==, 150);

    g_assert_cmpuint(qtest_readl(qts, PWM_CH3_PERIOD), ==, 400);
    g_assert_cmpuint(qtest_readl(qts, PWM_CH3_DUTY), ==, 200);

    /* Enable all channels */
    qtest_writel(qts, PWM_CH0_CTRL, PWM_CTRL_EN);
    qtest_writel(qts, PWM_CH1_CTRL, PWM_CTRL_EN);
    qtest_writel(qts, PWM_CH2_CTRL, PWM_CTRL_EN);
    qtest_writel(qts, PWM_CH3_CTRL, PWM_CTRL_EN);

    /* Verify all CHx_EN mirror bits are set */
    uint32_t glb = qtest_readl(qts, PWM_GLB);
    g_assert_cmpuint(glb & 0x0F, ==, 0x0F);

    qtest_quit(qts);
}

/*
 * test_pwm_polarity: Set POL=1 (inverted), verify the
 *                    polarity configuration is stored.
 */
static void test_pwm_polarity(void)
{
    QTestState *qts = qtest_init("-machine g233 -m 2G");

    /* Set polarity to inverted */
    qtest_writel(qts, PWM_CH0_CTRL, PWM_CTRL_POL);

    /* Verify POL bit is set */
    uint32_t ctrl = qtest_readl(qts, PWM_CH0_CTRL);
    g_assert_cmpuint(ctrl & PWM_CTRL_POL, ==, PWM_CTRL_POL);

    /* Verify EN is not set (only POL was written) */
    g_assert_cmpuint(ctrl & PWM_CTRL_EN, ==, 0);

    /* Set both EN and POL */
    qtest_writel(qts, PWM_CH0_CTRL, PWM_CTRL_EN | PWM_CTRL_POL);
    ctrl = qtest_readl(qts, PWM_CH0_CTRL);
    g_assert_cmpuint(ctrl, ==, PWM_CTRL_EN | PWM_CTRL_POL);

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("g233/pwm/config", test_pwm_config);
    qtest_add_func("g233/pwm/enable", test_pwm_enable);
    qtest_add_func("g233/pwm/counter", test_pwm_counter);
    qtest_add_func("g233/pwm/done_flag", test_pwm_done_flag);
    qtest_add_func("g233/pwm/done_clear", test_pwm_done_clear);
    qtest_add_func("g233/pwm/multi_channel", test_pwm_multi_channel);
    qtest_add_func("g233/pwm/polarity", test_pwm_polarity);

    return g_test_run();
}
