#include "backend.h"

/* Stores all available image decoding backends */
struct backends;
struct backends *backends_create(void);
void backends_free(struct backends *backends);

/* Prints all backend infos for the help message */
void print_backend_infos(struct backends *backends);

/* Tries to open the given path. If successful, BACKEND_SUCCESS is returned
 * and src will point to an imv_source instance for the given path.
 */
enum backend_result backends_open_path(
    struct backends *backends, const char *path, struct imv_source **src);

/* Tries to read the passed data. If successful, BACKEND_SUCCESS is returned
 * and src will point to an imv_source instance for the given data.
 */
enum backend_result backends_open_memory(
    struct backends *backends, void *data, size_t len, struct imv_source **src);
