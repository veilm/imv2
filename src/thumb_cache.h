#ifndef IMV_THUMB_CACHE_H
#define IMV_THUMB_CACHE_H

struct imv_image;
struct stat;

/* Cache only regular files. A miss or any cache error returns NULL. */
struct imv_image *imv_thumb_cache_read(const char *path, int size);
void imv_thumb_cache_write(const char *path, int size,
    const struct imv_image *image, const struct stat *source);

#endif
