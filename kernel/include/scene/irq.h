#ifndef SCENE_IRQ_H
#define SCENE_IRQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SCENE_IRQ_LINE_COUNT 16U

typedef void (*scene_irq_handler)(uint32_t irq, void *context);

bool scene_irq_attach(
    uint32_t irq,
    scene_irq_handler handler,
    void *context
);
bool scene_irq_detach(
    uint32_t irq,
    scene_irq_handler handler,
    void *context
);
bool scene_irq_dispatch(uint32_t irq);
size_t scene_irq_registered_count(void);

#endif
