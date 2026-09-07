#include "arch.h"

#include <efi.h>
#include <efilib.h>

extern EFI_BOOT_SERVICES *BS;

static UINT64 time_hz = 0;

static inline UINT64 read_time(void)
{
    UINT64 v;
    __asm__ volatile("rdtime %0" : "=r"(v));
    return v;
}

void arch_clock_init(void)
{
    if (time_hz)
        return;

    /*
     * RISC-V has no CNTFRQ_EL0 equivalent.
     * Calibrate the time CSR against UEFI Stall().
     */
    const UINTN sample_us = 10000; /* 10 ms */

    UINT64 start = read_time();

    BS->Stall(sample_us);

    UINT64 delta = read_time() - start;

    if (delta) {
        /*
         * sample_us = 10000, therefore this is effectively
         *
         *     time_hz = delta * 100
         *
         * Keep the general form for readability.
         */
        time_hz = delta * 1000000ULL / sample_us;
    }

    /*
     * This should never happen on a usable RISC-V UEFI platform,
     * but avoid division by zero.
     */
    if (!time_hz)
        time_hz = 1;
}

UINT64 arch_now_us(void)
{
    if (!time_hz)
        arch_clock_init();

    UINT64 ticks = read_time();

    /*
     * Avoid ticks * 1000000 overflowing.
     */
    UINT64 sec = ticks / time_hz;
    UINT64 rem = ticks % time_hz;

    return sec * 1000000ULL +
           rem * 1000000ULL / time_hz;
}

int arch_clock_since_power_on(void)
{
    /*
     * The RISC-V time counter is a constant-frequency real-time
     * counter, but the ISA does not guarantee that zero corresponds
     * to this system boot/power-on.
     */
    return 0;
}

typedef struct visor_cpu_arch visor_cpu_arch_t;

struct visor_cpu_arch {
    void *FlushDataCache;
    void *EnableInterrupt;
    void *DisableInterrupt;
    void *GetInterruptState;
    void *Init;
    void *RegisterInterruptHandler;
    void *GetTimerValue;

    EFI_STATUS (EFIAPI *SetMemoryAttributes)(
        visor_cpu_arch_t *This,
        EFI_PHYSICAL_ADDRESS BaseAddress,
        UINT64 Length,
        UINT64 Attributes
    );
};

const CHAR16 *arch_fb_make_wc(UINT64 base, UINT64 size)
{
    static EFI_GUID cpu_guid = {
        0x26baccb1, 0x6f42, 0x11d4,
        { 0xbc, 0xe7, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 }
    };

    if (!base || !size)
        return L"none";

    visor_cpu_arch_t *cpu = NULL;

    if (!EFI_ERROR(
            BS->LocateProtocol(
                &cpu_guid,
                NULL,
                (void **)&cpu
            )
        ) &&
        cpu &&
        cpu->SetMemoryAttributes &&
        !EFI_ERROR(
            cpu->SetMemoryAttributes(
                cpu,
                base,
                size,
                EFI_MEMORY_WC
            )
        )) {
        return L"cpu-arch";
    }

    return L"none";
}
