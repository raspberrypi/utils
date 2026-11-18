// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2023-25 Raspberry Pi Ltd.
 * All rights reserved.
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "piolib.h"
#include "piolib_priv.h"

#define PIO_MAX_INSTANCES 4
#define PIO_MAX_IRQS 16

/*
 * How often the IRQ thread checks whether it should exit, which is also how
 * long pio_close may have to wait for it.
 */
#define PIO_IRQ_POLL_MS 100

static __thread PIO __pio;

struct pio_irq_handler {
    PIO pio;
    irq_handler_t handler;
    void *context;
};

static PIO pio_instances[PIO_MAX_INSTANCES];
static uint num_instances;
static pthread_mutex_t pio_handle_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t pio_irq_threads[PIO_MAX_INSTANCES];
static bool pio_irq_thread_running[PIO_MAX_INSTANCES];
static atomic_bool pio_irq_thread_stop[PIO_MAX_INSTANCES];
static struct pio_irq_handler irq_handlers[PIO_MAX_IRQS];
static uint num_irqs;

void pio_select(PIO pio)
{
    __pio = pio;
}

PIO pio_get_current(void)
{
    PIO pio = __pio;
    check_pio_param(pio);
    return pio;
}

int pio_get_index(PIO pio)
{
    int i;
    for (i = 0; i < PIO_MAX_INSTANCES; i++)
    {
        if (pio == pio_instances[i])
            return i;
    }
    return -1;
}

static void *irq_wait_thread(void *arg) {
    PIO pio = (PIO)arg;
    int idx = pio_get_index(pio);

    /*
     * Wait with a timeout, so that pio_close can stop the thread. A thread
     * blocked indefinitely in the kernel would keep the device open, along
     * with everything that had been claimed through it.
     */
    while (!atomic_load(&pio_irq_thread_stop[idx])) {
        uint32_t active = pio->chip->pio_irq_wait(pio, PIO_IRQ_POLL_MS);
        uint i;
        if (active == ~0u)
            break;
        for (i = 0; active && i < pio->irq_count; i++) {
            uint32_t mask = (1 << i);
            if (active & mask) {
                struct pio_irq_handler *h = &irq_handlers[pio->irq_base + i];
                irq_handler_t handler = h->handler;
                active &= ~mask;
                if (handler)
                    handler(h->context);
            }
        }
    }

    return NULL;
}

/* The IRQ thread is only needed (and only started) once an IRQ is in use */
static void pio_irq_thread_start(PIO pio)
{
    int idx = pio_get_index(pio);

    if (idx < 0)
        return;

    pthread_mutex_lock(&pio_handle_lock);
    if (!pio_irq_thread_running[idx]) {
        atomic_store(&pio_irq_thread_stop[idx], false);
        if (pthread_create(&pio_irq_threads[idx], NULL, irq_wait_thread, pio))
            pio_panic("Failed to create irq wait thread!");
        pio_irq_thread_running[idx] = true;
    }
    pthread_mutex_unlock(&pio_handle_lock);
}

static void pio_irq_thread_stop_and_wait(PIO pio)
{
    int idx = pio_get_index(pio);
    pthread_t thread;
    bool running;

    if (idx < 0)
        return;

    pthread_mutex_lock(&pio_handle_lock);
    running = pio_irq_thread_running[idx];
    thread = pio_irq_threads[idx];
    pio_irq_thread_running[idx] = false;
    pthread_mutex_unlock(&pio_handle_lock);

    if (!running)
        return;

    atomic_store(&pio_irq_thread_stop[idx], true);
    /* Closing from an IRQ handler means the thread can't wait for itself */
    if (pthread_equal(thread, pthread_self()))
        pthread_detach(thread);
    else
        pthread_join(thread, NULL);
}

int pio_init(void)
{
#if LIBRARY_BUILD
    const PIO_CHIP_T *const *start = &library_piochips[0];
    const PIO_CHIP_T *const *end = &library_piochips[0] + library_piochips_count;
#else
    const PIO_CHIP_T *const *start = &__start_piochips;
    const PIO_CHIP_T *const *end = &__stop_piochips;
#endif
    static bool initialised;
    const PIO_CHIP_T * const *p;
    uint i = 0;

    pthread_mutex_lock(&pio_handle_lock);

    if (initialised) {
        pthread_mutex_unlock(&pio_handle_lock);
        return 0;
    }
    num_instances = 0;
    num_irqs = 0;

    p = start;
    while (p < end)
    {
        PIO_CHIP_T *chip = *p;
        PIO pio = chip->create_instance(chip, i);
        if (pio && !PIO_IS_ERR(pio)) {
            int irq_count = chip->irq_count;
            if (irq_count) {
                pio->irq_base = num_irqs;
                pio->irq_count = irq_count;
                while (irq_count--) {
                    struct pio_irq_handler *h = &irq_handlers[num_irqs++];
                    h->pio = pio;
                    h->handler = NULL;
                    pio->irqs[irq_count] = -1;
                }
            }
            pio_instances[num_instances++] = pio;
            i++;
        } else {
            p++;
            i = 0;
        }
    }

    initialised = true;
    pthread_mutex_unlock(&pio_handle_lock);
    return 0;
}

PIO pio_open(uint idx)
{
    PIO pio = NULL;
    int err;
    uint i;

    err = pio_init();
    if (err)
        return PIO_ERR(err);

    if (idx >= num_instances)
        return PIO_ERR(-EINVAL);

    pthread_mutex_lock(&pio_handle_lock);

    pio = pio_instances[idx];
    if (pio) {
        if (pio->in_use)
            err = -EBUSY;
        else
            pio->in_use = 1;
    }

    pthread_mutex_unlock(&pio_handle_lock);

    if (err)
        return PIO_ERR(err);

    err = pio->chip->open_instance(pio);
    if (err) {
        pthread_mutex_lock(&pio_handle_lock);
        pio->in_use = 0;
        pthread_mutex_unlock(&pio_handle_lock);
        return PIO_ERR(err);
    }

    /* The IRQ thread is started when the first IRQ is mapped */
    for (i = 0; i < pio->irq_count; i++)
        pio->irqs[i] = -1;

    pio_select(pio);

    return pio;
}

PIO pio_open_by_name(const char *name)
{
    int err = -ENOENT;
    uint i;

    err = pio_init();
    if (err)
        return PIO_ERR(err);

    for (i = 0; i < num_instances; i++) {
        PIO p = pio_instances[i];
        if (!strcmp(name, p->chip->name))
            break;
    }

    if (i == num_instances)
        return PIO_ERR(-ENOENT);

    return pio_open(i);
}

PIO pio_open_helper(uint idx)
{
    PIO pio = pio_instances[idx];
    if (!pio || !pio->in_use) {
        pio = pio_open(idx);
        if (PIO_IS_ERR(pio)) {
            printf("* Failed to open PIO device %d (error %d)\n",
                   idx, PIO_ERR_VAL(pio));
            exit(1);
        }
    }
    return pio;
}

void pio_close(PIO pio)
{
    /* Stop the IRQ thread first, otherwise it keeps the device open */
    pio_irq_thread_stop_and_wait(pio);
    pio->chip->close_instance(pio);
    pthread_mutex_lock(&pio_handle_lock);
    pio->in_use = 0;
    pthread_mutex_unlock(&pio_handle_lock);
}

void pio_panic(const char *msg)
{
    fprintf(stderr, "PANIC: %s\n", msg);
    exit(1);
}

void sleep_us(uint64_t us)
{
    const struct timespec tv = {
        .tv_sec = (us / 1000000),
        .tv_nsec = 1000ull * (us % 1000000)
    };
    nanosleep(&tv, NULL);
}

uint pio_irq_map(PIO pio, uint irq_index) {
    int pirq;
    check_pio_param(pio);
    invalid_params_if(PIO, irq_index > pio->irq_count);
    pirq = pio->irqs[irq_index];
    if (pirq < 0) {
        pirq = pio->chip->pio_irq_claim(pio);
        if (pirq >= 0) {
            pio->irqs[irq_index] = pirq;
        }
    }
    if (pirq < 0)
        pio_panic("Unable to map irq");
    pio_irq_thread_start(pio);
    return (uint)pirq;
}

static inline void check_irq_param(uint num)
{
    if (num >= num_irqs)
        pio_panic("irq out of range");
}

void irq_set_handler(uint num, irq_handler_t handler, void *context)
{
    struct pio_irq_handler *h = &irq_handlers[num];
    check_irq_param(num);
    h->handler = handler;
    h->context = context;
}

void irq_remove_handler(uint num, irq_handler_t handler)
{
    struct pio_irq_handler *h = &irq_handlers[num];
    check_irq_param(num);
    if (h->handler == handler)
        h->handler = NULL;
}

irq_handler_t irq_get_handler(uint num)
{
    struct pio_irq_handler *h = &irq_handlers[num];
    check_irq_param(num);
    return h->handler;
}

void irq_set_enabled(uint num, bool enabled)
{
    struct pio_irq_handler *h = &irq_handlers[num];
    PIO pio;
    check_irq_param(num);
    pio = h->pio;
    pio->chip->irq_set_enabled(pio, num - pio->irq_base, enabled);
}

bool irq_is_enabled(uint num)
{
    struct pio_irq_handler *h = &irq_handlers[num];
    PIO pio;
    check_irq_param(num);
    pio = h->pio;
    return pio->chip->irq_is_enabled(pio, num - pio->irq_base);
}
