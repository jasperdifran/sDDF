/*
 * Copyright 2022, UNSW
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#include <sel4cp.h>
#include <sel4/sel4.h>
#include <sel4/benchmark_track_types.h>
#include <syscall_implementation.h>
#include "sel4bench.h"
#include "fence.h"
#include "bench.h"
#include "util.h"

#define MAGIC_CYCLES 150
#define ULONG_MAX 0xfffffffffffffffful
#define UINT_MAX 0xfffffffful

#define LOG_BUFFER_CAP 7

#define START 1
#define STOP 2

#define INIT 3

pid_t my_pid = BENCH_PID;

// uintptr_t uart_base;
uintptr_t cyclecounters_vaddr;
uintptr_t instructionCount_vaddr;

struct bench *b = (void *)(uintptr_t)0x5010000;

struct instr *inst = (void *)(uintptr_t)0x3000000;

ccnt_t counter_values[8];
counter_bitfield_t benchmark_bf;

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
benchmark_track_kernel_entry_t *log_buffer;
#endif

char *counter_names[] = {
    "L1 i-cache misses",
    "L1 d-cache misses",
    "L1 i-tlb misses",
    "L1 d-tlb misses",
    "Instructions",
    "Branch mispredictions",
};

event_id_t benchmarking_events[] = {
    SEL4BENCH_EVENT_CACHE_L1I_MISS,
    SEL4BENCH_EVENT_CACHE_L1D_MISS,
    SEL4BENCH_EVENT_TLB_L1I_MISS,
    SEL4BENCH_EVENT_TLB_L1D_MISS,
    SEL4BENCH_EVENT_EXECUTE_INSTRUCTION,
    SEL4BENCH_EVENT_BRANCH_MISPREDICT,
};

#ifdef CONFIG_BENCHMARK_TRACK_UTILISATION
static void
sel4cp_benchmark_start(void)
{
    seL4_BenchmarkResetThreadUtilisation(TCB_CAP);
    seL4_BenchmarkResetLog();
}

static void
sel4cp_benchmark_stop(uint64_t *total, uint64_t *idle, uint64_t *kernel, uint64_t *entries)
{
    seL4_BenchmarkFinalizeLog();
    seL4_BenchmarkGetThreadUtilisation(TCB_CAP);
    uint64_t *buffer = (uint64_t *)&seL4_GetIPCBuffer()->msg[0];

    *total = buffer[BENCHMARK_TOTAL_UTILISATION];
    *idle = buffer[BENCHMARK_IDLE_LOCALCPU_UTILISATION];
    *kernel = buffer[BENCHMARK_TOTAL_KERNEL_UTILISATION];
    *entries = buffer[BENCHMARK_TOTAL_NUMBER_KERNEL_ENTRIES];
}
#endif

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
static inline void seL4_BenchmarkTrackDumpSummary(benchmark_track_kernel_entry_t *logBuffer, uint64_t logSize)
{
    seL4_Word index = 0;
    seL4_Word syscall_entries = 0;
    seL4_Word fastpaths = 0;
    seL4_Word interrupt_entries = 0;
    seL4_Word userlevelfault_entries = 0;
    seL4_Word vmfault_entries = 0;
    seL4_Word debug_fault = 0;
    seL4_Word other = 0;

    while (logBuffer[index].start_time != 0 && index < logSize)
    {
        if (logBuffer[index].entry.path == Entry_Syscall)
        {
            if (logBuffer[index].entry.is_fastpath)
            {
                fastpaths++;
            }
            syscall_entries++;
        }
        else if (logBuffer[index].entry.path == Entry_Interrupt)
        {
            interrupt_entries++;
        }
        else if (logBuffer[index].entry.path == Entry_UserLevelFault)
        {
            userlevelfault_entries++;
        }
        else if (logBuffer[index].entry.path == Entry_VMFault)
        {
            vmfault_entries++;
        }
        else if (logBuffer[index].entry.path == Entry_DebugFault)
        {
            debug_fault++;
        }
        else
        {
            other++;
        }
        index++;
    }

    print("Number of system call invocations ");
    puthex64(syscall_entries);
    print(" and fastpaths ")
        puthex64(fastpaths);
    print("\n");
    print("Number of interrupt invocations ");
    puthex64(interrupt_entries);
    print("\n");
    print("Number of user-level faults ");
    puthex64(userlevelfault_entries);
    print("\n");
    print("Number of VM faults ");
    puthex64(vmfault_entries);
    print("\n");
    print("Number of debug faults ");
    puthex64(debug_fault);
    print("\n");
    print("Number of others ");
    puthex64(other);
    print("\n");
}
#endif

void notified(sel4cp_channel ch)
{
    switch (ch)
    {
    case START:
        sel4bench_reset_counters();
        THREAD_MEMORY_RELEASE();
        inst->instr_idle_count = 0;
        inst->instr_overflows = 0;

        sel4bench_start_counters(benchmark_bf);

#ifdef CONFIG_BENCHMARK_TRACK_UTILISATION
        sel4cp_benchmark_start();
#endif

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
        seL4_BenchmarkResetLog();
#endif

        break;
    case STOP:
        sel4bench_get_counters(benchmark_bf, &counter_values[0]);
        sel4bench_stop_counters(benchmark_bf);

#ifdef CONFIG_BENCHMARK_TRACK_UTILISATION
        uint64_t total;
        uint64_t kernel;
        uint64_t entries;
        uint64_t idle;
        sel4cp_benchmark_stop(&total, &idle, &kernel, &entries);
        /* Dump the counters */
        print("{\n");
        for (int i = 0; i < ARRAY_SIZE(benchmarking_events); i++)
        {
            print(counter_names[i]);
            print(": ");
            if (i == 4)
            {
                uint64_t instr = (counter_values[i] + (UINT_MAX * inst->instr_overflows)) - inst->instr_idle_count;
                puthex64(instr);
            }
            else
            {
                puthex64(counter_values[i]);
            }
            print("\n");
        }

        print("KernelUtilisation");
        print(": ");
        puthex64(kernel);
        print("\n");
        print("KernelEntries");
        print(": ");
        puthex64(entries);
        print("\n");
        print("}\n");
#endif

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
        entries = seL4_BenchmarkFinalizeLog();
        print("KernelEntries");
        print(": ");
        puthex64(entries);
        seL4_BenchmarkTrackDumpSummary(log_buffer, entries);
#endif

        break;
    default:
        print("Bench thread notified on unexpected channel\n");
    }
}

void init(void)
{
    sel4bench_init();
    seL4_Word n_counters = sel4bench_get_num_counters();
    int n_chunks = DIV_ROUND_UP(ARRAY_SIZE(benchmarking_events), n_counters);

    counter_bitfield_t mask = 0;

    for (seL4_Word i = 0; i < n_counters; i++)
    {
        seL4_Word counter = i;
        if (counter >= ARRAY_SIZE(benchmarking_events))
        {
            break;
        }
        sel4bench_set_count_event(i, benchmarking_events[counter]);
        mask |= BIT(i);
    }

    sel4bench_reset_counters();
    sel4bench_start_counters(mask);

    benchmark_bf = mask;

    /* Notify the idle thread that the sel4bench library is initialised. */
    sel4cp_notify(INIT);

#ifdef CONFIG_BENCHMARK_TRACK_KERNEL_ENTRIES
    int res_buf = seL4_BenchmarkSetLogBuffer(LOG_BUFFER_CAP);
    if (res_buf)
    {
        print("Could not set log buffer");
        puthex64(res_buf);
    }
    else
    {
        print("We set the log buffer\n");
    }
#endif
}