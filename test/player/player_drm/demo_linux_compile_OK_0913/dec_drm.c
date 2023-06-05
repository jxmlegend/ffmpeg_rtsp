#define _GNU_SOURCE

#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sched.h>

#include <drm_fourcc.h>
#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <sys/time.h>
#include <unistd.h>

#include "main.h"

int main(int argc, char *argv[])
{
    struct drm_bo *bo;

    drm_init(32, 0);
    bo = malloc_drm_bo(480, 480, DRM_FORMAT_ARGB8888);
    // fill buffer bo->ptr
    setdrmdisp(bo);
}

