#include "backend.h"
#include "backends.h"
#include "list.h"
#include "log.h"
#include "stdio.h"

struct backends {
  struct list list;
};

extern const struct imv_backend imv_backend_farbfeld;
extern const struct imv_backend imv_backend_libpng;
extern const struct imv_backend imv_backend_librsvg;
extern const struct imv_backend imv_backend_libtiff;
extern const struct imv_backend imv_backend_libjpeg;
extern const struct imv_backend imv_backend_libnsgif;
extern const struct imv_backend imv_backend_libnsbmp;
extern const struct imv_backend imv_backend_libheif;
extern const struct imv_backend imv_backend_libjxl;
extern const struct imv_backend imv_backend_libwebp;
extern const struct imv_backend imv_backend_qoi;

void imv_install_backend(struct list *backends, const struct imv_backend *backend)
{
  if (!backend->init || backend->init() == BACKEND_SUCCESS) {
    list_append(backends, (void *)backend);
  }
}

struct backends *backends_create(void)
{
  struct list *backends = list_create();
  if (!backends) {
    return NULL;
  }

#ifdef IMV_BACKEND_FARBFELD
  imv_install_backend(backends, &imv_backend_farbfeld);
#endif

#ifdef IMV_BACKEND_LIBTIFF
  imv_install_backend(backends, &imv_backend_libtiff);
#endif

#ifdef IMV_BACKEND_LIBPNG
  imv_install_backend(backends, &imv_backend_libpng);
#endif

#ifdef IMV_BACKEND_LIBJPEG
  imv_install_backend(backends, &imv_backend_libjpeg);
#endif

#ifdef IMV_BACKEND_LIBRSVG
  imv_install_backend(backends, &imv_backend_librsvg);
#endif

#ifdef IMV_BACKEND_LIBNSGIF
  imv_install_backend(backends, &imv_backend_libnsgif);
#endif

#ifdef IMV_BACKEND_LIBNSBMP
  imv_install_backend(backends, &imv_backend_libnsbmp);
#endif

#ifdef IMV_BACKEND_LIBHEIF
  imv_install_backend(backends, &imv_backend_libheif);
#endif

#ifdef IMV_BACKEND_LIBJXL
  imv_install_backend(backends, &imv_backend_libjxl);
#endif

#ifdef IMV_BACKEND_LIBWEBP
  imv_install_backend(backends, &imv_backend_libwebp);
#endif

#ifdef IMV_BACKEND_QOI
  imv_install_backend(backends, &imv_backend_qoi);
#endif

  return (struct backends *)backends;
}

void backends_free(struct backends *backends)
{
  for (size_t i = 0; i < backends->list.len; ++i) {
    struct imv_backend *backend = backends->list.items[i];
    if (backend->uninit) {
      backend->uninit();
    }
  }
  list_free(&backends->list);
}

void print_backend_infos(struct backends *backends)
{
  for (size_t i = 0; i < backends->list.len; ++i) {
    struct imv_backend *backend = backends->list.items[i];
    printf("Name: %s\n"
           "Description: %s\n"
           "Website: %s\n"
           "License: %s\n\n",
        backend->name, backend->description, backend->website, backend->license);
  }
}

enum backend_result backends_open_memory(
    struct backends *backends, void *data, size_t len, struct imv_source **src)
{
  for (size_t i = 0; i < backends->list.len; ++i) {
    const struct imv_backend *backend = backends->list.items[i];

    if (!backend->open_memory) {
      /* memory loading unsupported by backend */
      continue;
    }

    enum backend_result result = backend->open_memory(data, len, src);
    imv_log(IMV_DEBUG, "Trying backend %s, result %d.\n", backend->name, result);
    if (result != BACKEND_UNSUPPORTED) {
      return result;
    }
  }

  return BACKEND_UNSUPPORTED;
}

enum backend_result backends_open_path(
    struct backends *backends, const char *path, struct imv_source **src)
{
  for (size_t i = 0; i < backends->list.len; ++i) {
    const struct imv_backend *backend = backends->list.items[i];

    if (!backend->open_path) {
      /* path loading unsupported by backend */
      continue;
    }

    enum backend_result result = backend->open_path(path, src);
    imv_log(IMV_DEBUG, "Trying backend %s, result %d.\n", backend->name, result);
    if (result != BACKEND_UNSUPPORTED) {
      return result;
    }
  }

  return BACKEND_UNSUPPORTED;
}
