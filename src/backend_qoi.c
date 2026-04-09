#include "backend.h"
#include "bitmap.h"
#include "image.h"
#include "source_private.h"

#include <stdio.h>
#include <stdlib.h>
#define QOI_IMPLEMENTATION
#include <qoi.h>

struct private {
  qoi_desc desc;
  void *bitmap;
};

static void free_private(void *raw_private)
{
  if (!raw_private) {
    return;
  }

  struct private *private = raw_private;
  free(private->bitmap);
  free(private);
}

static void load_image(void *raw_private, struct imv_image **image, int *frametime)
{
  *image = NULL;
  *frametime = 0;

  struct private *private = raw_private;

  struct imv_bitmap bmp =
      imv_bitmap_alloc(private->desc.width, private->desc.height);
  if (!bmp.data) {
    return;
  }
  memcpy(bmp.data, private->bitmap, imv_bitmap_size(bmp));

  *image = imv_image_create_from_bitmap(bmp);
}

static const struct imv_source_vtable vtable = {
  .load_first_frame = load_image,
  .free = free_private
};

static enum backend_result open_memory(void *data, size_t len, struct imv_source **src)
{
  struct private *priv = calloc(1, sizeof *priv);
  if (!priv) {
    return BACKEND_UNSUPPORTED;
  }
  priv->bitmap = qoi_decode(data, len, &priv->desc, 4);
  if (!priv->bitmap) {
    free(priv);
    return BACKEND_UNSUPPORTED;
  }
  *src = imv_source_create(&vtable, priv);
  return BACKEND_SUCCESS;
}

static enum backend_result open_path(const char *path, struct imv_source **src)
{
  struct private *priv = calloc(1, sizeof *priv);
  if (!priv) {
    return BACKEND_UNSUPPORTED;
  }
  priv->bitmap = qoi_read(path, &priv->desc, 4);
  if (!priv->bitmap) {
    free(priv);
    return BACKEND_UNSUPPORTED;
  }
  *src = imv_source_create(&vtable, priv);
  return BACKEND_SUCCESS;
}

const struct imv_backend imv_backend_qoi = {
  .name = "qoi",
  .description = "The 'Quite OK Image Format' for fast, lossless image compression",
  .website = "https://qoiformat.org/",
  .license = "MIT",
  .open_path = &open_path,
  .open_memory = &open_memory,
};
