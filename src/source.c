#include "source.h"
#include "source_private.h"

#include <pthread.h>
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>

struct imv_source {
  /* pointers to implementation's functions */
  const struct imv_source_vtable *vtable;

  /* pointer to implementation data */
  void *private;

  /* Attempted to be locked by load_first_frame or load_next_frame.
   * If the mutex can't be locked, the call is aborted.
   * Used to prevent the source from having multiple worker threads at once.
   * Released by the source before calling the message callback with a result.
   */
  pthread_mutex_t busy;

  /* callback function */
  imv_source_callback callback;
  /* callback data */
  void *callback_data;
};

struct imv_source *imv_source_create(const struct imv_source_vtable *vtable, void *private)
{
  struct imv_source *source = calloc(1, sizeof *source);
  source->vtable = vtable;
  source->private = private;
  pthread_mutex_init(&source->busy, NULL);
  return source;
}

static void *free_thread(void *src)
{
  imv_source_free(src);
  return NULL;
}

void imv_source_async_free(struct imv_source *src)
{
  pthread_t thread;
  pthread_create(&thread, NULL, free_thread, src);
  pthread_detach(thread);
}

static void *first_frame_thread(void *src_raw)
{
  struct imv_source *src = src_raw;
  struct imv_source_message msg = {
    .source = src,
    .user_data = src->callback_data
  };
  if(imv_source_load_first_frame(src, &msg.image, &msg.frametime)) {
    src->callback(&msg);
  }
  return NULL;
}

void imv_source_async_load_first_frame(struct imv_source *src)
{
  pthread_t thread;
  pthread_create(&thread, NULL, first_frame_thread, src);
  pthread_detach(thread);
}

static void *next_frame_thread(void *src)
{
  imv_source_load_next_frame(src);
  return NULL;
}
void imv_source_async_load_next_frame(struct imv_source *src)
{
  pthread_t thread;
  pthread_create(&thread, NULL, next_frame_thread, src);
  pthread_detach(thread);
}

void imv_source_free(struct imv_source *src)
{
  pthread_mutex_lock(&src->busy);
  src->vtable->free(src->private);
  pthread_mutex_unlock(&src->busy);
  pthread_mutex_destroy(&src->busy);
  free(src);
}

bool imv_source_load_first_frame(struct imv_source *src, struct imv_image **image, int *frametime)
{
  if (!src->vtable->load_first_frame) {
    return false;
  }

  if (pthread_mutex_trylock(&src->busy)) {
    return false;
  }

  src->vtable->load_first_frame(src->private, image, frametime);

  if(pthread_mutex_unlock(&src->busy)) {
    // We locked the mutex so this can never fail
    assert(false);
  }
  return true;
}

void imv_source_load_next_frame(struct imv_source *src)
{
  if (!src->vtable->load_next_frame) {
    return;
  }

  if (pthread_mutex_trylock(&src->busy)) {
    return;
  }

  struct imv_source_message msg = {
    .source = src,
    .user_data = src->callback_data
  };

  src->vtable->load_next_frame(src->private, &msg.image, &msg.frametime);

  pthread_mutex_unlock(&src->busy);

  src->callback(&msg);
}

void imv_source_set_callback(struct imv_source *src, imv_source_callback callback,
    void *data)
{
  src->callback = callback;
  src->callback_data = data;
}
