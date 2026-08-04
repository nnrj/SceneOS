#include <scene/arch.h>
#include <scene/irq.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct irq_registration {
    scene_irq_handler handler;
    void *context;
};

static struct irq_registration registrations[SCENE_IRQ_LINE_COUNT];
static size_t registration_count;

bool scene_irq_attach(
    uint32_t irq,
    scene_irq_handler handler,
    void *context
) {
    if (irq >= SCENE_IRQ_LINE_COUNT || handler == 0 ||
        registrations[irq].handler != 0) {
        return false;
    }

    registrations[irq] = (struct irq_registration){
        .handler = handler,
        .context = context
    };
    if (!scene_arch_irq_enable(irq)) {
        registrations[irq] = (struct irq_registration){0};
        return false;
    }
    ++registration_count;
    return true;
}

bool scene_irq_detach(
    uint32_t irq,
    scene_irq_handler handler,
    void *context
) {
    if (irq >= SCENE_IRQ_LINE_COUNT || handler == 0 ||
        registrations[irq].handler != handler ||
        registrations[irq].context != context ||
        !scene_arch_irq_disable(irq)) {
        return false;
    }

    registrations[irq] = (struct irq_registration){0};
    --registration_count;
    return true;
}

bool scene_irq_dispatch(uint32_t irq) {
    if (irq >= SCENE_IRQ_LINE_COUNT ||
        registrations[irq].handler == 0) {
        return false;
    }
    registrations[irq].handler(irq, registrations[irq].context);
    return true;
}

size_t scene_irq_registered_count(void) {
    return registration_count;
}
