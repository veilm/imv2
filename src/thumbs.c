#include "thumbs.h"

#include "backends.h"
#include "canvas.h"
#include "image.h"
#include "navigator.h"
#include "source.h"

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum thumb_state {
  THUMB_EMPTY,
  THUMB_LOADING,
  THUMB_READY,
  THUMB_FAILED,
};

struct thumb_item {
  struct imv_image *image;
  enum thumb_state state;
  int x;
  int y;
  int width;
  int height;
};

struct imv_thumbs {
  struct backends *backends;
  imv_thumbs_ready_cb ready_cb;
  void *ready_cb_data;

  struct thumb_item *items;
  size_t count;

  int window_width;
  int window_height;
  double scale;
  int x;
  int y;
  int cols;
  int rows;
  int size_index;
  int thumb_size;
  int border_width;
  int cell_size;
  size_t first;
  size_t end;
  bool dirty;

  unsigned generation;

  pthread_t worker;
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  bool stop_worker;
  bool worker_started;
  bool job_pending;
  size_t job_index;
  unsigned job_generation;
  int job_thumb_size;
  char *job_path;
};

static const int thumb_sizes[] = {
  64, 96, 128, 160, 192, 224, 256
};

static void draw_mark_overlay(struct imv_canvas *canvas, const struct thumb_item *item,
    double scale, int border_width)
{
  const double outer_width = scale * (border_width + 2);
  const double inner_width = scale * (border_width > 1 ? border_width : 2);
  const int inset = border_width;
  const int x = (int)(item->x * scale + 0.5);
  const int y = (int)(item->y * scale + 0.5);
  const int width = (int)(item->width * scale + 0.5);
  const int height = (int)(item->height * scale + 0.5);
  const int x1 = (int)((item->x + inset) * scale + 0.5);
  const int y1 = (int)((item->y + inset) * scale + 0.5);
  const int x2 = (int)((item->x + item->width - inset - 1) * scale + 0.5);
  const int y2 = (int)((item->y + item->height - inset - 1) * scale + 0.5);

  imv_canvas_color(canvas, 0.0f, 0.0f, 0.0f, 0.95f);
  imv_canvas_stroke_rectangle(canvas, x, y, width, height, outer_width);
  if (x2 >= x1 && y2 >= y1) {
    imv_canvas_stroke_line(canvas, x1, y1, x2, y2, outer_width);
    imv_canvas_stroke_line(canvas, x1, y2, x2, y1, outer_width);
  }

  imv_canvas_color(canvas, 0.95f, 0.95f, 0.95f, 0.95f);
  imv_canvas_stroke_rectangle(canvas, x, y, width, height, inner_width);
  if (x2 >= x1 && y2 >= y1) {
    imv_canvas_stroke_line(canvas, x1, y1, x2, y2, inner_width);
    imv_canvas_stroke_line(canvas, x1, y2, x2, y1, inner_width);
  }
}

static void free_thumb_item(struct thumb_item *item)
{
  if (item->image) {
    imv_image_free(item->image);
    item->image = NULL;
  }
  item->state = THUMB_EMPTY;
}

static void clear_thumbs(struct imv_thumbs *thumbs)
{
  for (size_t i = 0; i < thumbs->count; ++i) {
    free_thumb_item(&thumbs->items[i]);
  }
  free(thumbs->items);
  thumbs->items = NULL;
  thumbs->count = 0;
  thumbs->first = 0;
  thumbs->end = 0;
}

static void update_metrics(struct imv_thumbs *thumbs)
{
  thumbs->thumb_size = thumb_sizes[thumbs->size_index];
  thumbs->border_width = ((thumbs->thumb_size - 1) >> 5) + 1;
  if (thumbs->border_width > 4) {
    thumbs->border_width = 4;
  }
  thumbs->cell_size = thumbs->thumb_size + 2 * thumbs->border_width + 6;
  if (thumbs->window_width > 0) {
    thumbs->cols = thumbs->window_width / thumbs->cell_size;
  }
  if (thumbs->cols < 1) {
    thumbs->cols = 1;
  }
  if (thumbs->window_height > 0) {
    thumbs->rows = thumbs->window_height / thumbs->cell_size;
  }
  if (thumbs->rows < 1) {
    thumbs->rows = 1;
  }
}

static struct imv_image *load_thumbnail(struct backends *backends,
    const char *path, int thumb_size)
{
  struct imv_source *src = NULL;
  enum backend_result result = backends_open_path(backends, path, &src);

  if (result != BACKEND_SUCCESS || !src) {
    return NULL;
  }

  struct imv_image *image = NULL;
  int frametime = 0;
  const bool ok = imv_source_load_first_frame(src, &image, &frametime);
  (void)frametime;
  imv_source_free(src);
  if (!ok || !image) {
    return NULL;
  }

  struct imv_image *thumb = imv_image_thumbnail(image, thumb_size, thumb_size);
  imv_image_free(image);
  return thumb;
}

static void *thumb_worker(void *raw_thumbs)
{
  struct imv_thumbs *thumbs = raw_thumbs;

  while (true) {
    pthread_mutex_lock(&thumbs->mutex);
    while (!thumbs->stop_worker && !thumbs->job_pending) {
      pthread_cond_wait(&thumbs->cond, &thumbs->mutex);
    }
    if (thumbs->stop_worker) {
      pthread_mutex_unlock(&thumbs->mutex);
      break;
    }

    const size_t index = thumbs->job_index;
    const unsigned generation = thumbs->job_generation;
    const int thumb_size = thumbs->job_thumb_size;
    char *path = thumbs->job_path;
    thumbs->job_path = NULL;
    thumbs->job_pending = false;
    pthread_mutex_unlock(&thumbs->mutex);

    struct imv_image *image = load_thumbnail(thumbs->backends, path, thumb_size);
    free(path);

    thumbs->ready_cb(index, generation, image, thumbs->ready_cb_data);
  }

  return NULL;
}

struct imv_thumbs *imv_thumbs_create(struct backends *backends,
    imv_thumbs_ready_cb ready_cb, void *ready_cb_data)
{
  struct imv_thumbs *thumbs = calloc(1, sizeof *thumbs);
  thumbs->backends = backends;
  thumbs->ready_cb = ready_cb;
  thumbs->ready_cb_data = ready_cb_data;
  thumbs->size_index = 4;
  thumbs->dirty = true;
  pthread_mutex_init(&thumbs->mutex, NULL);
  pthread_cond_init(&thumbs->cond, NULL);
  update_metrics(thumbs);
  thumbs->worker_started = pthread_create(&thumbs->worker, NULL, thumb_worker, thumbs) == 0;
  return thumbs;
}

void imv_thumbs_free(struct imv_thumbs *thumbs)
{
  if (!thumbs) {
    return;
  }

  pthread_mutex_lock(&thumbs->mutex);
  thumbs->stop_worker = true;
  pthread_cond_signal(&thumbs->cond);
  pthread_mutex_unlock(&thumbs->mutex);

  if (thumbs->worker_started) {
    pthread_join(thumbs->worker, NULL);
  }

  free(thumbs->job_path);
  clear_thumbs(thumbs);
  pthread_cond_destroy(&thumbs->cond);
  pthread_mutex_destroy(&thumbs->mutex);
  free(thumbs);
}

void imv_thumbs_resize(struct imv_thumbs *thumbs, int width, int height, double scale)
{
  thumbs->window_width = width;
  thumbs->window_height = height;
  thumbs->scale = scale > 0.0 ? scale : 1.0;
  update_metrics(thumbs);
  thumbs->dirty = true;
}

static void invalidate_all(struct imv_thumbs *thumbs)
{
  ++thumbs->generation;
  for (size_t i = 0; i < thumbs->count; ++i) {
    free_thumb_item(&thumbs->items[i]);
  }
  thumbs->dirty = true;
}

void imv_thumbs_resync(struct imv_thumbs *thumbs, struct imv_navigator *nav)
{
  const size_t count = imv_navigator_length(nav);
  if (count == thumbs->count) {
    return;
  }

  clear_thumbs(thumbs);
  if (count > 0) {
    thumbs->items = calloc(count, sizeof *thumbs->items);
  }
  thumbs->count = count;
  ++thumbs->generation;
  thumbs->first = 0;
  thumbs->end = 0;
  thumbs->dirty = true;
}

void imv_thumbs_set_index(struct imv_thumbs *thumbs, size_t index, size_t count)
{
  if (count == 0) {
    thumbs->first = 0;
    thumbs->dirty = true;
    return;
  }

  if (thumbs->cols < 1) {
    thumbs->cols = 1;
  }

  thumbs->first -= thumbs->first % (size_t)thumbs->cols;
  const size_t column = index % (size_t)thumbs->cols;
  const size_t page_size = (size_t)thumbs->cols * (size_t)thumbs->rows;
  if (page_size == 0) {
    thumbs->first = 0;
    thumbs->dirty = true;
    return;
  }

  if (index >= thumbs->first + page_size) {
    thumbs->first = index - column - (size_t)thumbs->cols * (size_t)(thumbs->rows - 1);
    thumbs->dirty = true;
  } else if (index < thumbs->first) {
    thumbs->first = index - column;
    thumbs->dirty = true;
  }

  if (thumbs->first >= count) {
    thumbs->first = 0;
  }
}

int imv_thumbs_move(struct imv_thumbs *thumbs, size_t *index,
    size_t count, int dx, int dy, int amount)
{
  if (count == 0) {
    return 0;
  }

  size_t current = *index;
  if (amount < 1) {
    amount = 1;
  }

  if (dy < 0) {
    const size_t col = current % (size_t)thumbs->cols;
    const size_t step = (size_t)amount * (size_t)thumbs->cols;
    current = step > current ? col : current - step;
  } else if (dy > 0) {
    const size_t step = (size_t)amount * (size_t)thumbs->cols;
    size_t last = (size_t)thumbs->cols * ((count - 1) / (size_t)thumbs->cols)
        + ((count - 1) % (size_t)thumbs->cols);
    const size_t col = current % (size_t)thumbs->cols;
    if (last % (size_t)thumbs->cols > col) {
      last = last - (last % (size_t)thumbs->cols) + col;
    }
    if (current + step < last) {
      current += step;
    } else {
      current = last;
    }
  } else if (dx < 0) {
    current = current > (size_t)amount ? current - (size_t)amount : 0;
  } else if (dx > 0) {
    current += (size_t)amount;
    if (current >= count) {
      current = count - 1;
    }
  }

  if (current == *index) {
    return 0;
  }
  *index = current;
  imv_thumbs_set_index(thumbs, *index, count);
  thumbs->dirty = true;
  return 1;
}

int imv_thumbs_scroll(struct imv_thumbs *thumbs, size_t *index,
    size_t count, int dir, int screenful)
{
  if (count == 0) {
    return 0;
  }

  const size_t page_rows = screenful ? (size_t)thumbs->rows : 1;
  const size_t delta = (size_t)thumbs->cols * page_rows;
  const size_t old_first = thumbs->first;
  if (dir > 0) {
    thumbs->first += delta;
    if (thumbs->first >= count) {
      thumbs->first = old_first;
    }
  } else if (dir < 0) {
    thumbs->first = thumbs->first > delta ? thumbs->first - delta : 0;
  }

  if (thumbs->first == old_first) {
    return 0;
  }

  thumbs->first -= thumbs->first % (size_t)thumbs->cols;
  const size_t column = *index % (size_t)thumbs->cols;
  const size_t page_size = (size_t)thumbs->cols * (size_t)thumbs->rows;
  if (*index >= thumbs->first + page_size) {
    *index = thumbs->first + column + (size_t)thumbs->cols * (size_t)(thumbs->rows - 1);
  } else if (*index < thumbs->first) {
    *index = thumbs->first + column;
  }
  if (*index >= count) {
    *index = count - 1;
  }
  thumbs->dirty = true;
  return 1;
}

int imv_thumbs_zoom(struct imv_thumbs *thumbs, int delta)
{
  const int old = thumbs->size_index;
  thumbs->size_index += (delta > 0) - (delta < 0);
  if (thumbs->size_index < 0) {
    thumbs->size_index = 0;
  }
  if (thumbs->size_index >= (int)(sizeof thumb_sizes / sizeof thumb_sizes[0])) {
    thumbs->size_index = (int)(sizeof thumb_sizes / sizeof thumb_sizes[0]) - 1;
  }
  if (thumbs->size_index == old) {
    return 0;
  }

  update_metrics(thumbs);
  invalidate_all(thumbs);
  return 1;
}

int imv_thumbs_translate(const struct imv_thumbs *thumbs, int x, int y)
{
  if (thumbs->scale > 0.0) {
    x = (int)(x / thumbs->scale);
    y = (int)(y / thumbs->scale);
  }

  const int x_max = thumbs->x + thumbs->cell_size * thumbs->cols;
  const int y_max = thumbs->y + thumbs->cell_size * thumbs->rows;
  if (x < thumbs->x || y < thumbs->y || x > x_max || y > y_max) {
    return -1;
  }

  const size_t index = thumbs->first
      + (size_t)((y - thumbs->y) / thumbs->cell_size) * (size_t)thumbs->cols
      + (size_t)((x - thumbs->x) / thumbs->cell_size);
  if (index >= thumbs->count) {
    return -1;
  }
  return (int)index;
}

bool imv_thumbs_handle_loaded(struct imv_thumbs *thumbs, size_t index,
    unsigned generation, struct imv_image *image)
{
  if (generation != thumbs->generation || index >= thumbs->count) {
    if (image) {
      imv_image_free(image);
    }
    return false;
  }

  struct thumb_item *item = &thumbs->items[index];
  if (item->image) {
    imv_image_free(item->image);
  }
  item->image = image;
  item->state = image ? THUMB_READY : THUMB_FAILED;
  thumbs->dirty = true;
  return image != NULL;
}

static void trim_cache(struct imv_thumbs *thumbs)
{
  const size_t keep_margin = (size_t)thumbs->cols * (size_t)thumbs->rows;
  const size_t keep_start = thumbs->first > keep_margin ? thumbs->first - keep_margin : 0;
  size_t keep_end = thumbs->end + keep_margin;
  if (keep_end > thumbs->count) {
    keep_end = thumbs->count;
  }

  for (size_t i = 0; i < thumbs->count; ++i) {
    if (i >= keep_start && i < keep_end) {
      continue;
    }
    if (thumbs->items[i].state == THUMB_READY) {
      free_thumb_item(&thumbs->items[i]);
    }
  }
}

void imv_thumbs_schedule(struct imv_thumbs *thumbs, struct imv_navigator *nav)
{
  if (!thumbs->worker_started || thumbs->count == 0) {
    return;
  }

  pthread_mutex_lock(&thumbs->mutex);
  if (thumbs->job_pending) {
    pthread_mutex_unlock(&thumbs->mutex);
    return;
  }
  pthread_mutex_unlock(&thumbs->mutex);

  for (size_t i = thumbs->first; i < thumbs->end; ++i) {
    struct thumb_item *item = &thumbs->items[i];
    if (item->state != THUMB_EMPTY) {
      continue;
    }

    char *path = strdup(imv_navigator_at(nav, i));
    if (!path) {
      return;
    }

    item->state = THUMB_LOADING;
    pthread_mutex_lock(&thumbs->mutex);
    free(thumbs->job_path);
    thumbs->job_path = path;
    thumbs->job_index = i;
    thumbs->job_generation = thumbs->generation;
    thumbs->job_thumb_size = thumbs->thumb_size;
    thumbs->job_pending = true;
    pthread_cond_signal(&thumbs->cond);
    pthread_mutex_unlock(&thumbs->mutex);
    return;
  }
}

void imv_thumbs_render(struct imv_thumbs *thumbs, struct imv_canvas *canvas,
    struct imv_navigator *nav, size_t selected,
    enum upscaling_method upscaling_method)
{
  if (thumbs->dirty) {
    update_metrics(thumbs);
    imv_thumbs_set_index(thumbs, selected, thumbs->count);

    const size_t page_size = (size_t)thumbs->cols * (size_t)thumbs->rows;
    thumbs->end = thumbs->first + page_size;
    if (thumbs->end > thumbs->count) {
      thumbs->end = thumbs->count;
    }

    trim_cache(thumbs);

    int count = (int)(thumbs->end - thumbs->first);
    if (count < 0) {
      count = 0;
    }
    const int partial_row = count % thumbs->cols ? 1 : 0;
    const int rows_used = count == 0 ? 0 : count / thumbs->cols + partial_row;
    thumbs->x = (thumbs->window_width - (count < thumbs->cols ? count : thumbs->cols) * thumbs->cell_size) / 2
        + thumbs->border_width + 3;
    thumbs->y = (thumbs->window_height - rows_used * thumbs->cell_size) / 2
        + thumbs->border_width + 3;
    thumbs->dirty = false;
  }

  int x = thumbs->x;
  int y = thumbs->y;
  for (size_t i = thumbs->first; i < thumbs->end; ++i) {
    struct thumb_item *item = &thumbs->items[i];
    item->x = x;
    item->y = y;
    item->width = thumbs->thumb_size;
    item->height = thumbs->thumb_size;

    if (item->image) {
      item->width = imv_image_width(item->image);
      item->height = imv_image_height(item->image);
      const int draw_x = x + (thumbs->thumb_size - item->width) / 2;
      const int draw_y = y + (thumbs->thumb_size - item->height) / 2;
      item->x = draw_x;
      item->y = draw_y;
      imv_canvas_draw_image(canvas, item->image,
          (int)(draw_x * thumbs->scale + 0.5),
          (int)(draw_y * thumbs->scale + 0.5),
          thumbs->scale, 0.0, false,
          upscaling_method);
    }

    if (i == selected && item->image) {
      imv_canvas_color(canvas, 0.90f, 0.90f, 0.90f, 1.0f);
      imv_canvas_stroke_rectangle(canvas,
          (int)((item->x - thumbs->border_width) * thumbs->scale + 0.5),
          (int)((item->y - thumbs->border_width) * thumbs->scale + 0.5),
          (int)((item->width + 2 * thumbs->border_width) * thumbs->scale + 0.5),
          (int)((item->height + 2 * thumbs->border_width) * thumbs->scale + 0.5),
          thumbs->border_width * thumbs->scale);
    }

    if (imv_navigator_is_marked(nav, i)) {
      draw_mark_overlay(canvas, item, thumbs->scale, thumbs->border_width);
    }

    if ((i + 1 - thumbs->first) % (size_t)thumbs->cols == 0) {
      x = thumbs->x;
      y += thumbs->cell_size;
    } else {
      x += thumbs->cell_size;
    }
  }

  imv_thumbs_schedule(thumbs, nav);
}
