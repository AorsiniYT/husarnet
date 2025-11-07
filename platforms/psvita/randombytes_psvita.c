#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/rng.h>
#include <sodium/randombytes.h>
#include <sodium/randombytes_sysrandom.h>

static const char* randombytes_psvita_implementation_name(void) {
    return "psvita";
}

static uint32_t randombytes_psvita_random(void) {
    uint32_t r;
    sceKernelGetRandomNumber(&r, sizeof(r));
    return r;
}

static void randombytes_psvita_stir(void) {
    // No-op
}

static uint32_t randombytes_psvita_uniform(const uint32_t upper_bound) {
    if (upper_bound < 2) {
        return 0;
    }

    const uint32_t min = (uint32_t)(-upper_bound) % upper_bound;
    uint32_t r = 0;
    do {
        sceKernelGetRandomNumber(&r, sizeof(r));
    } while (r < min);

    return r % upper_bound;
}

static void randombytes_psvita_buf(void * const buf, const size_t size) {
    sceKernelGetRandomNumber(buf, size);
}

static int randombytes_psvita_close(void) {
    return 0;
}

struct randombytes_implementation randombytes_sysrandom_implementation = {
    .implementation_name = randombytes_psvita_implementation_name,
    .random = randombytes_psvita_random,
    .stir = randombytes_psvita_stir,
    .uniform = randombytes_psvita_uniform,
    .buf = randombytes_psvita_buf,
    .close = randombytes_psvita_close,
};