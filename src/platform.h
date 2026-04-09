#ifndef IMV_PLATFORM_H
#define IMV_PLATFORM_H

#include <time.h>

// Returns current time in seconds since epoch
time_t imv_time(void);

// Returns last file modification time in seconds since epoch
time_t imv_file_last_modified(const char *path);

#endif
