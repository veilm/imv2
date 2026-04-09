#ifndef IMV_THUMBS_H
#define IMV_THUMBS_H

#include "canvas.h"

#include <stdbool.h>
#include <stddef.h>

struct backends;
struct imv_image;
struct imv_navigator;

struct imv_thumbs;

typedef void (*imv_thumbs_ready_cb)(size_t index, unsigned generation,
    struct imv_image *image, void *data);

struct imv_thumbs *imv_thumbs_create(struct backends *backends,
    imv_thumbs_ready_cb ready_cb, void *ready_cb_data);
void imv_thumbs_free(struct imv_thumbs *thumbs);

void imv_thumbs_resize(struct imv_thumbs *thumbs, int width, int height, double scale);
void imv_thumbs_resync(struct imv_thumbs *thumbs, struct imv_navigator *nav);
void imv_thumbs_set_index(struct imv_thumbs *thumbs, size_t index, size_t count);

int imv_thumbs_move(struct imv_thumbs *thumbs, size_t *index,
    size_t count, int dx, int dy, int amount);
int imv_thumbs_scroll(struct imv_thumbs *thumbs, size_t *index,
    size_t count, int dir, int screenful);
int imv_thumbs_zoom(struct imv_thumbs *thumbs, int delta);

int imv_thumbs_translate(const struct imv_thumbs *thumbs, int x, int y);
bool imv_thumbs_handle_loaded(struct imv_thumbs *thumbs, size_t index,
    unsigned generation, struct imv_image *image);
void imv_thumbs_schedule(struct imv_thumbs *thumbs, struct imv_navigator *nav);

void imv_thumbs_render(struct imv_thumbs *thumbs, struct imv_canvas *canvas,
    struct imv_navigator *nav, size_t selected,
    enum upscaling_method upscaling_method);

#endif
