#include <scene/framebuffer.h>

#include <stddef.h>

static uint32_t scale_component(uint8_t value, uint8_t mask_size) {
    if (mask_size == 0) {
        return 0;
    }
    if (mask_size >= 32) {
        return (uint32_t)value;
    }

    const uint32_t maximum = (UINT32_C(1) << mask_size) - 1U;
    return ((uint32_t)value * maximum) / 255U;
}

static uint32_t pack_rgb(
    const struct scene_framebuffer *framebuffer,
    uint8_t red,
    uint8_t green,
    uint8_t blue
) {
    return
        (scale_component(red, framebuffer->red_mask_size)
            << framebuffer->red_mask_shift) |
        (scale_component(green, framebuffer->green_mask_size)
            << framebuffer->green_mask_shift) |
        (scale_component(blue, framebuffer->blue_mask_size)
            << framebuffer->blue_mask_shift);
}

static void fill_rectangle(
    const struct scene_framebuffer *framebuffer,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint32_t colour
) {
    if (framebuffer == 0 || framebuffer->address == 0 ||
        framebuffer->bits_per_pixel != 32) {
        return;
    }

    const uint64_t x_end = (x + width < framebuffer->width)
        ? x + width
        : framebuffer->width;
    const uint64_t y_end = (y + height < framebuffer->height)
        ? y + height
        : framebuffer->height;

    for (uint64_t row = y; row < y_end; ++row) {
        volatile uint32_t *pixels =
            (volatile uint32_t *)(framebuffer->address + row * framebuffer->pitch);
        for (uint64_t column = x; column < x_end; ++column) {
            pixels[column] = colour;
        }
    }
}

void scene_framebuffer_draw_boot_scene(const struct scene_framebuffer *framebuffer) {
    if (framebuffer == 0 || framebuffer->bits_per_pixel != 32) {
        return;
    }

    const uint32_t desktop = pack_rgb(framebuffer, 20, 47, 74);
    const uint32_t taskbar = pack_rgb(framebuffer, 12, 24, 38);
    const uint32_t window = pack_rgb(framebuffer, 238, 244, 250);
    const uint32_t caption = pack_rgb(framebuffer, 26, 104, 166);
    const uint32_t accent = pack_rgb(framebuffer, 69, 181, 170);

    fill_rectangle(framebuffer, 0, 0, framebuffer->width, framebuffer->height, desktop);

    const uint64_t taskbar_height = framebuffer->height >= 80 ? 48 : 24;
    fill_rectangle(
        framebuffer,
        0,
        framebuffer->height - taskbar_height,
        framebuffer->width,
        taskbar_height,
        taskbar
    );

    const uint64_t window_width = framebuffer->width * 3 / 5;
    const uint64_t window_height = framebuffer->height / 2;
    const uint64_t window_x = (framebuffer->width - window_width) / 2;
    const uint64_t window_y = (framebuffer->height - window_height) / 3;

    fill_rectangle(
        framebuffer,
        window_x,
        window_y,
        window_width,
        window_height,
        window
    );
    fill_rectangle(
        framebuffer,
        window_x,
        window_y,
        window_width,
        36,
        caption
    );
    fill_rectangle(
        framebuffer,
        window_x + 32,
        window_y + 80,
        window_width - 64,
        12,
        accent
    );
}
