#include "image.h"

#include "bitmap.h"

#include <stdlib.h>
#include <assert.h>
#include <stdbool.h>

struct imv_image {
  enum image_type type;
  union {
    struct imv_bitmap bitmap;
    #ifdef IMV_BACKEND_LIBRSVG
    RsvgHandle *svg;
    #endif
  } val;
};

enum image_type imv_image_get_type(struct imv_image *image) {
  assert(image);
  return image->type;
}

struct imv_image *imv_image_create_from_bitmap(struct imv_bitmap bmp)
{
  struct imv_image *image = calloc(1, sizeof *image);
  image->type = IMV_IMAGE_BITMAP;
  image->val.bitmap = bmp;
  return image;
}

#ifdef IMV_BACKEND_LIBRSVG
struct imv_image *imv_image_create_from_svg(RsvgHandle *handle)
{
  struct imv_image *image = calloc(1, sizeof *image);
  image->type = IMV_IMAGE_SVG;
  image->val.svg = handle;
  return image;
}
#endif

void imv_image_free(struct imv_image *image)
{
  if (!image) {
    return;
  }

  switch (image->type) {
    case IMV_IMAGE_BITMAP:
      imv_bitmap_free(image->val.bitmap);
      break;
#ifdef IMV_BACKEND_LIBRSVG
    case IMV_IMAGE_SVG: {
      g_object_unref(image->val.svg);
      break;
    }
#endif
  }

  free(image);
}

int imv_image_width(const struct imv_image *image)
{
  if (!image) {
    return 0;
  }
  switch (image->type) {
    case IMV_IMAGE_BITMAP:
      return image->val.bitmap.width;
#ifdef IMV_BACKEND_LIBRSVG
    case IMV_IMAGE_SVG: {
      RsvgDimensionData dims;
      rsvg_handle_get_dimensions(image->val.svg, &dims);
      return dims.width;
    }
#endif
  }
  assert(false);
}

int imv_image_height(const struct imv_image *image)
{
  if (!image) {
    return 0;
  }
  switch (image->type) {
    case IMV_IMAGE_BITMAP:
      return image->val.bitmap.height;
#ifdef IMV_BACKEND_LIBRSVG
    case IMV_IMAGE_SVG: {
      RsvgDimensionData dims;
      rsvg_handle_get_dimensions(image->val.svg, &dims);
      return dims.height;
    }
#endif
  }
  assert(false);
}

/* Non-public functions, only used by imv_canvas */
const struct imv_bitmap *imv_image_get_bitmap(const struct imv_image *image)
{
  assert(image);
  return image->type == IMV_IMAGE_BITMAP ? &image->val.bitmap : NULL;
}

#ifdef IMV_BACKEND_LIBRSVG
RsvgHandle *imv_image_get_svg(const struct imv_image *image)
{
  return image->type == IMV_IMAGE_SVG ? image->val.svg : NULL;
}
#endif

/* vim:set ts=2 sts=2 sw=2 et: */
