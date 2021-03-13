
/******************************************************************************************
 *
 * MPFS HSS Embedded Software
 *
 * Copyright 2019 Microchip Corporation.
 *
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Originally based on code from OpenSBI, which is:
 *
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 *
 */

#include <sbi/sbi_types.h>
#define false FALSE
#define true TRUE

#include <sbi/riscv_encoding.h>
#include <sbi/sbi_const.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_platform.h>
#include <sbi/riscv_io.h>
#include <sbi_utils/fdt/fdt_fixup.h>
#include <sbi_utils/irqchip/plic.h>
#include <sbi_utils/serial/uart8250.h>
#include <sbi_utils/sys/clint.h>
#include "mss_sw_config.h"

#include <sm.h>

#define MPFS_HART_COUNT            5
#define MPFS_HART_STACK_SIZE       8192

#define MPFS_CLINT_ADDR            0x2000000

#define MPFS_PLIC_ADDR             0xc000000
#define MPFS_PLIC_NUM_SOURCES      0x35
#define MPFS_PLIC_NUM_PRIORITIES   7

/**
 * The MPFS SoC has 5 HARTs but HART ID 0 doesn't have S mode. enable only
 * HARTs 1 to 4.
 */
static u32 hart_idx2id[5] = { -1U, 1, 2, 3, 4 };

extern unsigned long STACK_SIZE_PER_HART;

static int mpfs_final_init(bool cold_boot)
{
    sm_init(cold_boot);

    if (!cold_boot) return 0;

	void *fdt = sbi_scratch_thishart_arg1_ptr();
    fdt_cpu_fixup(fdt);
	fdt_fixups(fdt);

    return 0;
}

static bool console_initialized = false;

static void mpfs_console_putc(char ch)
{
    if (console_initialized) {
        u32 hartid = current_hartid();
        int uart_putc(int hartid, const char ch); //TBD
        uart_putc(hartid, ch);
    }
}

#define NO_BLOCK 0
#define GETC_EOF -1
static int mpfs_console_getc(void)
{
    int result = GETC_EOF;
    bool uart_getchar(uint8_t *pbuf, int32_t timeout_sec, bool do_sec_tick);

    uint8_t rcvBuf;
    if (uart_getchar(&rcvBuf, NO_BLOCK, FALSE)) {
        result = rcvBuf;
    }

    return result;
}

static int mpfs_console_init(void)
{
    console_initialized = true;
    return 0;
}

static struct plic_data plic = { MPFS_PLIC_ADDR, MPFS_PLIC_NUM_SOURCES };

static int mpfs_irqchip_init(bool cold_boot)
{
    int rc;
    u32 hartid = current_hartid();

    if (cold_boot) {
        rc = plic_cold_irqchip_init(&plic);

        if (rc) {
            return rc;
        }
    }

    rc = plic_warm_irqchip_init(&plic,
        (hartid) ? (2 * hartid - 1) : 0, (hartid) ? (2 * hartid) : -1);

    return rc;
}

static struct clint_data clint = {
    .addr = MPFS_CLINT_ADDR,
    .first_hartid = MPFS_HAL_FIRST_HART,
    .hart_count = MPFS_HART_COUNT,
    .has_64bit_mmio = true,
};

static int mpfs_ipi_init(bool cold_boot)
{
    int rc;

    if (cold_boot) {
        rc = clint_cold_ipi_init(&clint);

        if (rc) {
            return rc;
        }

    }

    return clint_warm_ipi_init();
}

static int mpfs_timer_init(bool cold_boot)
{
    int rc;

    if (cold_boot) {
        rc = clint_cold_timer_init(&clint, NULL);

        if (rc) {
            return rc;
        }
    }

    return clint_warm_timer_init();
}

static int mpfs_reset_check(u32 type, u32 reason)
{
	return 0;
}

#define MPFS_TLB_RANGE_FLUSH_LIMIT 0u
static u64 mpfs_get_tlbr_flush_limit(void)
{
    return MPFS_TLB_RANGE_FLUSH_LIMIT;
}

const struct sbi_platform_operations platform_ops = {
    .early_init = NULL,
    .final_init = mpfs_final_init,
    .early_exit = NULL,
    .final_exit = NULL,

    .misa_check_extension = NULL,
    .misa_get_xlen = NULL,

    // .pmp_region_count = mpfs_pmp_region_count,
    // .pmp_region_info = mpfs_pmp_region_info,

    .console_putc = mpfs_console_putc,
    .console_getc = mpfs_console_getc,
    .console_init = mpfs_console_init,

    .irqchip_init = mpfs_irqchip_init,
    .irqchip_exit = NULL,

    .ipi_send = clint_ipi_send,
    .ipi_clear = clint_ipi_clear,
    .ipi_init = mpfs_ipi_init,
    .ipi_exit = NULL,

    .get_tlbr_flush_limit = mpfs_get_tlbr_flush_limit,

    .timer_value = clint_timer_value,
    .timer_event_start = clint_timer_event_start,
    .timer_event_stop = clint_timer_event_stop,
    .timer_init = mpfs_timer_init,
    .timer_exit = NULL,

    //.hart_start = NULL,
    //.hart_stop = NULL,

    .system_reset_check = mpfs_reset_check,

    .vendor_ext_check = NULL,
    .vendor_ext_provider = NULL
};

const struct sbi_platform platform = {
    .opensbi_version = OPENSBI_VERSION,
    .platform_version = SBI_PLATFORM_VERSION(0x0, 0x01),
    .name = "Microchip PolarFire SoC",
    //.features = SBI_PLATFORM_DEFAULT_FEATURES & (~SBI_PLATFORM_HAS_PMP), // already have PMPs setup
    .features = SBI_PLATFORM_DEFAULT_FEATURES, // already have PMPs setup
    .hart_count = MPFS_HART_COUNT,
    .hart_stack_size = MPFS_HART_STACK_SIZE, //TODO: revisit
    .hart_index2id = hart_idx2id,
    .platform_ops_addr = (unsigned long)&platform_ops,
    .firmware_context = 0
};