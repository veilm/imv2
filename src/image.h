#ifndef IMV_IMAGE_H
#define IMV_IMAGE_H

#include "bitmap.h"

#ifdef IMV_BACKEND_LIBRSVG
#include <librsvg/rsvg.h>
#endif

enum image_type {
  IMV_IMAGE_BITMAP,
#ifdef IMV_BACKEND_LIBRSVG
  IMV_IMAGE_SVG,
#endif
};

struct imv_image;

enum image_type imv_image_get_type(struct imv_image *image);

struct imv_image *imv_image_create_from_bitmap(struct imv_bitmap bmp);

/* Returns the underlying image bitmap or NULL if the image is of different
 * type. */
const struct imv_bitmap *imv_image_get_bitmap(const struct imv_image *image);

#ifdef IMV_BACKEND_LIBRSVG
struct imv_image *imv_image_create_from_svg(RsvgHandle *handle);

RsvgHandle *imv_image_get_svg(const struct imv_image *image);
#endif

/* Cleans up an imv_image instance */
void imv_image_free(struct imv_image *image);

/* Get the image width */
int imv_image_width(const struct imv_image *image);

/* Get the image height */
int imv_image_height(const struct imv_image *image);

#endif


/* vim:set ts=2 sts=2 sw=2 et: */
