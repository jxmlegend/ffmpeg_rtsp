#include "main.h"

#define NUM_DUMB_BO 3
#define DEBUG
#ifdef DEBUG
#define DRM_DEBUG(fmt, ...) \
    if (getenv("LVGL_DRM_DEBUG")) \
        printf("DRM_DEBUG: %s(%d) " fmt, __func__, __LINE__, __VA_ARGS__)
#else
#define DRM_DEBUG(fmt, ...)
#endif

struct device {
    int fd;

    struct {
        int width;
        int height;

        int hdisplay;
        int vdisplay;

        int current;
        int fb_num;
        int bpp;
    } mode;

    drmModeResPtr res;

    int connector_id;
    int encoder_id;
    int crtc_id;
    int plane_id;
    int last_fb_id;

    struct drm_bo *dumb_bo[NUM_DUMB_BO];
    struct drm_bo *video_bo[NUM_DUMB_BO];
    struct drm_bo *black_bo;
    int current_dumb;
    int current_video;

    int waiting_for_flip;
    struct pollfd drm_pollfd;
    drmEventContext drm_evctx;
    int rot;

    int drm_invalide;
    int screen_state;
};

struct device *pdev;

static int bo_map(struct device *dev, struct drm_bo *bo)
{
    struct drm_mode_map_dumb arg = {
        .handle = bo->handle,
    };
    struct drm_prime_handle fd_args = {
        .fd = -1,
        .handle = bo->handle,
        .flags = 0,
    };
    int ret;

    ret = drmIoctl(dev->fd, DRM_IOCTL_MODE_MAP_DUMB, &arg);
    if (ret) {
        printf("map dump failed\n");
        return ret;
    }

    ret = drmIoctl(dev->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &fd_args);
    if (ret)
    {
        printf("handle_to_fd failed ret=%d, handle=%x \n", ret ,fd_args.handle);
        return -1;
    }
    bo->buf_fd = fd_args.fd;

    bo->ptr = mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   dev->fd, arg.offset);
    if (bo->ptr == MAP_FAILED) {
        printf("mmap failed\n");
        bo->ptr = NULL;
        return -1;
    }

    return 0;
}

static void bo_unmap(struct device *dev, struct drm_bo *bo)
{
    if (dev == NULL)
        return;
    if (!bo->ptr)
        return;

    drmUnmap(bo->ptr, bo->size);
    if (bo->buf_fd > 0)
        close(bo->buf_fd);
    bo->ptr = NULL;
}

void bo_destroy(struct device *dev, struct drm_bo *bo)
{
    struct drm_mode_destroy_dumb arg = {
        .handle = bo->handle,
    };

    if (bo->fb_id)
        drmModeRmFB(dev->fd, bo->fb_id);

    bo_unmap(dev, bo);

    if (bo->handle)
        drmIoctl(dev->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &arg);

    free(bo);
}

static struct drm_bo *
bo_create(struct device *dev, int width, int height, int format)
{
    struct drm_mode_create_dumb arg = {
        .bpp = 32,
        .width = ALIGN(width, 16),
        .height = ALIGN(height, 16),
    };
    struct drm_bo *bo;
    uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};
    int ret;

    bo = malloc(sizeof(struct drm_bo));
    if (bo == NULL) {
        fprintf(stderr, "allocate bo failed\n");
        return NULL;
    }
    memset(bo, 0, sizeof(*bo));
    if (format == DRM_FORMAT_NV12) {
        arg.bpp = 8;
        arg.height = height * 3 / 2;
    }

    ret = drmIoctl(dev->fd, DRM_IOCTL_MODE_CREATE_DUMB, &arg);
    if (ret) {
        fprintf(stderr, "create dumb failed\n");
        goto err;
    }

    bo->fd = dev->fd;
    bo->handle = arg.handle;
    bo->size = arg.size;
    bo->pitch = arg.pitch;
    bo->w = width;
    bo->h = height;

    ret = bo_map(dev, bo);
    if (ret) {
        fprintf(stderr, "map bo failed\n");
        goto err;
    }

    switch (format) {
    case DRM_FORMAT_NV12:
    case DRM_FORMAT_NV16:
      handles[0] = bo->handle;
      pitches[0] = bo->pitch ;
      offsets[0] = 0;
      handles[1] = bo->handle;
      pitches[1] = pitches[0];
      offsets[1] = pitches[0] * height;
      break;
    case DRM_FORMAT_RGB332:
      handles[0] = bo->handle;
      pitches[0] = bo->pitch;
      offsets[0] = 0;
      break;
    case DRM_FORMAT_RGB565:
    case DRM_FORMAT_BGR565:
      handles[0] = bo->handle;
      pitches[0] = bo->pitch ;
      offsets[0] = 0;
      break;
    case DRM_FORMAT_RGB888:
    case DRM_FORMAT_BGR888:
      handles[0] = bo->handle;
      pitches[0] = bo->pitch ;
      offsets[0] = 0;
      break;
    case DRM_FORMAT_ARGB8888:
    case DRM_FORMAT_ABGR8888:
    case DRM_FORMAT_RGBA8888:
    case DRM_FORMAT_BGRA8888:
      handles[0] = bo->handle;
      pitches[0] = bo->pitch ;
      offsets[0] = 0;
      break;
    }

    ret = drmModeAddFB2(dev->fd, width, height, format, handles,
                        pitches, offsets, (uint32_t *)&bo->fb_id, 0);
    if (ret) {
        fprintf(stderr, "add fb failed\n");
        goto err;
    }
    DRM_DEBUG("Created bo: %d, %dx%d\n", bo->fb_id, width, height);

    return bo;
err:
    bo_destroy(dev, bo);
    return NULL;
}

struct drm_bo *malloc_drm_bo(int width, int height, int format)
{
    struct device *dev = pdev;
    return bo_create(dev, width, height, format);
}

void free_drm_bo(struct drm_bo *bo)
{
    struct device *dev = pdev;
    bo_destroy(dev, bo);
}

static void free_fb(struct device *dev)
{
    DRM_DEBUG("Free fb, num: %d, bpp: %d\n", dev->mode.fb_num, dev->mode.bpp);

    dev->mode.fb_num = 0;
    dev->mode.bpp = 0;
    dev->mode.current = 0;
}

static int alloc_fb(struct device *dev, int num, int bpp)
{
    DRM_DEBUG("Alloc fb num: %d, bpp: %d\n", num, bpp);

    dev->mode.fb_num = num;
    dev->mode.bpp = bpp;
    dev->mode.current = 0;

    return 0;
}

static int drm_get_preferred_connector(void)
{
    const char *path;
    char buf[256] = "\0";
    int fd;

#define DRM_CONNECTOR_CFG_PATH_ENV	"DRM_CONNECTOR_CFG_PATH"
#define DRM_CONNECTOR_CFG_PATH_DEFAULT	"/tmp/drm_connector.cfg"
    path = getenv(DRM_CONNECTOR_CFG_PATH_ENV);
    if (!path)
        path = DRM_CONNECTOR_CFG_PATH_DEFAULT;

    fd = open(path, O_RDONLY);
    read(fd, buf, sizeof(buf));
    close(fd);

    if (!buf[0])
        return -1;

    return atoi(buf);
}

static int drm_get_preferred_mode(int *width, int *height)
{
    const char *path;
    char buf[256] = "\0";
    int fd, w, h;

#define DRM_MODE_CFG_PATH_ENV	"DRM_CONNECTOR_CFG_PATH"
#define DRM_MODE_CFG_PATH_DEFAULT	"/tmp/drm_mode.cfg"
    path = getenv(DRM_MODE_CFG_PATH_ENV);
    if (!path)
        path = DRM_MODE_CFG_PATH_DEFAULT;

    fd = open(path, O_RDONLY);
    read(fd, buf, sizeof(buf));
    close(fd);

    if (!buf[0])
        return -1;

    if (2 != sscanf(buf, "%dx%d", &w, &h))
        return -1;

    *width = w;
    *height = h;

    return 0;
}

static drmModeConnectorPtr
drm_get_connector(struct device *dev, int connector_id)
{
    drmModeConnectorPtr conn;

    conn = drmModeGetConnector(dev->fd, connector_id);
    if (!conn)
        return NULL;

    DRM_DEBUG("Connector id: %d, %sconnected, modes: %d\n", connector_id,
              (conn->connection == DRM_MODE_CONNECTED) ? "" : "dis",
              conn->count_modes);
    if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes)
        return conn;

    drmModeFreeConnector(conn);
    return NULL;
}

static drmModeConnectorPtr
drm_find_best_connector(struct device *dev)
{
    drmModeResPtr res = dev->res;
    drmModeConnectorPtr conn;
    int i, preferred_connector_id = drm_get_preferred_connector();

    DRM_DEBUG("Preferred connector id: %d\n", preferred_connector_id);
    conn = drm_get_connector(dev, preferred_connector_id);
    if (conn)
        return conn;

    for (i = 0; i < res->count_connectors; i++) {
        conn = drm_get_connector(dev, res->connectors[i]);
        if (conn)
            return conn;
    }
    return NULL;
}

static drmModeCrtcPtr
drm_find_best_crtc(struct device *dev, drmModeConnectorPtr conn)
{
    drmModeResPtr res = dev->res;
    drmModeEncoderPtr encoder;
    drmModeCrtcPtr crtc;
    int i, preferred_crtc_id = 0;
    int crtcs_for_connector = 0;

    encoder = drmModeGetEncoder(dev->fd, conn->encoder_id);
    if (encoder) {
        preferred_crtc_id = encoder->crtc_id;
        drmModeFreeEncoder(encoder);
    }
    DRM_DEBUG("Preferred crtc: %d\n", preferred_crtc_id);

    crtc = drmModeGetCrtc(dev->fd, preferred_crtc_id);
    if (crtc)
        return crtc;

    for (i = 0; i < res->count_encoders; i++) {
        encoder = drmModeGetEncoder(dev->fd, res->encoders[i]);
        if (encoder)
            crtcs_for_connector |= encoder->possible_crtcs;
        drmModeFreeEncoder(encoder);
    }
    DRM_DEBUG("Possible crtcs: %x\n", crtcs_for_connector);
    if (!crtcs_for_connector)
        return NULL;

    return drmModeGetCrtc(dev->fd, res->crtcs[ffs(crtcs_for_connector) - 1]);
}

int
drm_plane_is_primary(struct device *dev, int plane_id)
{
    drmModeObjectPropertiesPtr props;
    drmModePropertyPtr prop;
    unsigned int i;
    int type = 0;

    props = drmModeObjectGetProperties(dev->fd, plane_id,
                                       DRM_MODE_OBJECT_PLANE);
    if (!props)
        return 0;

    for (i = 0; i < props->count_props; i++) {
        prop = drmModeGetProperty(dev->fd, props->props[i]);
        if (prop && !strcmp(prop->name, "type"))
            type = props->prop_values[i];
        drmModeFreeProperty(prop);
    }
    DRM_DEBUG("Plane: %d, type: %d\n", plane_id, type);

    drmModeFreeObjectProperties(props);
    return type == DRM_PLANE_TYPE_PRIMARY;
}

int
drm_plane_is_overlay(struct device *dev, int plane_id)
{
    drmModeObjectPropertiesPtr props;
    drmModePropertyPtr prop;
    unsigned int i;
    int type = 0;

    props = drmModeObjectGetProperties(dev->fd, plane_id,
                                       DRM_MODE_OBJECT_PLANE);
    if (!props)
        return 0;

    for (i = 0; i < props->count_props; i++) {
        prop = drmModeGetProperty(dev->fd, props->props[i]);
        if (prop && !strcmp(prop->name, "type"))
            type = props->prop_values[i];
        drmModeFreeProperty(prop);
    }
    DRM_DEBUG("Plane: %d, type: %d\n", plane_id, type);

    drmModeFreeObjectProperties(props);
    return type == DRM_PLANE_TYPE_OVERLAY;
}

static drmModePlanePtr
drm_get_plane(struct device *dev, int plane_id, int pipe)
{
    drmModePlanePtr plane;

    plane = drmModeGetPlane(dev->fd, plane_id);
    if (!plane)
        return NULL;

    DRM_DEBUG("Check plane: %d, possible_crtcs: %x\n", plane_id,
              plane->possible_crtcs);
    if (drm_plane_is_primary(dev, plane_id)) {
        if (plane->possible_crtcs & (1 << pipe))
            return plane;
    }

    drmModeFreePlane(plane);
    return NULL;
}

static drmModePlanePtr
drm_get_video_plane(struct device *dev, int plane_id, int pipe)
{
    drmModePlanePtr plane;

    plane = drmModeGetPlane(dev->fd, plane_id);
    if (!plane)
        return NULL;

    DRM_DEBUG("Check plane: %d, possible_crtcs: %x\n", plane_id,
              plane->possible_crtcs);
    if (drm_plane_is_overlay(dev, plane_id)) {
        if (plane->possible_crtcs & (1 << pipe))
            return plane;
    }

    drmModeFreePlane(plane);
    return NULL;
}

static drmModePlanePtr
drm_find_best_plane(struct device *dev, drmModeCrtcPtr crtc)
{
    drmModeResPtr res = dev->res;
    drmModePlaneResPtr pres;
    drmModePlanePtr plane;
    unsigned int i;
    int pipe;

    for (pipe = 0; pipe < res->count_crtcs; pipe++) {
        if (crtc->crtc_id == res->crtcs[pipe])
            break;
    }
    if (pipe == res->count_crtcs)
        return NULL;

    pres = drmModeGetPlaneResources(dev->fd);
    if (!pres)
        return NULL;

    for (i = 0; i < pres->count_planes; i++) {
        plane = drm_get_plane(dev, pres->planes[i], pipe);
        if (plane) {
            drmModeFreePlaneResources(pres);
            return plane;
        }
        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(pres);
    return NULL;
}

static drmModePlanePtr
drm_find_video_plane(struct device *dev, drmModeCrtcPtr crtc)
{
    drmModeResPtr res = dev->res;
    drmModePlaneResPtr pres;
    drmModePlanePtr plane;
    unsigned int i;
    int pipe;

    for (pipe = 0; pipe < res->count_crtcs; pipe++) {
        if (crtc->crtc_id == res->crtcs[pipe])
            break;
    }
    if (pipe == res->count_crtcs)
        return NULL;

    pres = drmModeGetPlaneResources(dev->fd);
    if (!pres)
        return NULL;

    for (i = 0; i < pres->count_planes; i++) {
        plane = drm_get_video_plane(dev, pres->planes[i], pipe);
        if (plane) {
            drmModeFreePlaneResources(pres);
            return plane;
        }
        drmModeFreePlane(plane);
    }

    drmModeFreePlaneResources(pres);
    return NULL;
}

static drmModeModeInfoPtr
drm_find_best_mode(struct device *dev, drmModeConnectorPtr conn)
{
    drmModeModeInfoPtr mode;
    int i, preferred_width = 1920, preferred_height = 1080;

    if (dev == NULL)
    	  return 0;
    drm_get_preferred_mode(&preferred_width, &preferred_height);
    DRM_DEBUG("Preferred mode: %dx%d\n", preferred_width, preferred_height);

    mode = &conn->modes[0];
    for (i = 0; i < conn->count_modes; i++) {
        DRM_DEBUG("Check mode: %dx%d\n",
                conn->modes[i].hdisplay, conn->modes[i].vdisplay);
        if (conn->modes[i].hdisplay == preferred_width &&
                conn->modes[i].vdisplay == preferred_height) {
            mode = &conn->modes[i];
            break;
        }
    }

    return mode;
}

static int drm_get_preferred_fb_mode(int *width, int *height)
{
    char *buf;
    int w, h;

    buf = getenv("MINIGUI_DRM_FB_MODE");
    if (!buf)
        return -1;

    if (2 != sscanf(buf, "%dx%d", &w, &h))
        return -1;

    DRM_DEBUG("Preferred fb mode: %dx%d\n", w, h);
    *width = w;
    *height = h;

    return 0;
}

static void drm_setup_fb_mode(struct device *dev)
{
    drmModeResPtr res = dev->res;
    drmModeConnectorPtr conn;
    drmModeModeInfoPtr mode;
    int i;

    if (dev->mode.width && dev->mode.height)
        return;

    if (!drm_get_preferred_fb_mode(&dev->mode.width, &dev->mode.height))
        return;

    dev->mode.width = dev->mode.hdisplay;
    dev->mode.height = dev->mode.vdisplay;

    for (i = 0; i < res->count_connectors; i++) {
        conn = drm_get_connector(dev, res->connectors[i]);
        if (!conn)
            continue;

        mode = drm_find_best_mode(dev, conn);
        if (mode) {
            DRM_DEBUG("Best mode for connector(%d): %dx%d\n",
                      conn->connector_id, mode->hdisplay, mode->vdisplay);
            if (dev->mode.width > mode->hdisplay ||
                    dev->mode.height > mode->vdisplay) {
                dev->mode.width = mode->hdisplay;
                dev->mode.height = mode->vdisplay;
            }
        }
        drmModeFreeConnector(conn);
    }
}

static void drm_free(struct device *dev)
{
    int i;

    for (i = 0; i < NUM_DUMB_BO; i++) {
        if (dev->dumb_bo[i]) {
            bo_destroy(dev, dev->dumb_bo[i]);
            dev->dumb_bo[i] = NULL;
        }
        if (dev->video_bo[i]) {
            bo_destroy(dev, dev->video_bo[i]);
            dev->video_bo[i] = NULL;
        }
    }

    if (dev->res) {
        drmModeFreeResources(dev->res);
        dev->res = NULL;
    }

    dev->connector_id = 0;
    dev->crtc_id = 0;
    dev->plane_id = 0;
    dev->mode.hdisplay = 0;
    dev->mode.vdisplay = 0;
}

static void configure_plane_zpos (struct device *self, int plane_id, uint64_t zpos)
{
    drmModeObjectPropertiesPtr props = NULL;
    drmModePropertyPtr prop = NULL;
    char *buf;
    unsigned int i;

    if (plane_id <= 0)
        return;

    if (drmSetClientCap (self->fd, DRM_CLIENT_CAP_ATOMIC, 1))
        return;

    props = drmModeObjectGetProperties (self->fd, plane_id,
          DRM_MODE_OBJECT_PLANE);
    if (!props)
        goto out;

    for (i = 0; i < props->count_props; i++) {
        prop = drmModeGetProperty (self->fd, props->props[i]);
        if (prop && !strcmp (prop->name, "ZPOS"))
          break;
        drmModeFreeProperty (prop);
        prop = NULL;
    }

    if (!prop)
        goto out;

    drmModeObjectSetProperty (self->fd, plane_id,
          DRM_MODE_OBJECT_PLANE, props->props[i], zpos);
out:
    drmModeFreeProperty (prop);
    drmModeFreeObjectProperties (props);
}

static int drm_setup(struct device *dev)
{
    drmModeConnectorPtr conn = NULL;
    drmModeModeInfoPtr mode;
    drmModePlanePtr plane = NULL;
    drmModeCrtcPtr crtc = NULL;
    //int ret;
    int i, success = 0;

    dev->res = drmModeGetResources(dev->fd);
    if (!dev->res) {
        fprintf(stderr, "drm get resource failed\n");
        goto err;
    }

    conn = drm_find_best_connector(dev);
    if (!conn) {
        fprintf(stderr, "drm find connector failed\n");
        goto err;
    }
    DRM_DEBUG("Best connector id: %d\n", conn->connector_id);

    mode = drm_find_best_mode(dev, conn);
    if (!mode) {
        fprintf(stderr, "drm find mode failed\n");
        goto err;
    }
    DRM_DEBUG("Best mode: %dx%d\n", mode->hdisplay, mode->vdisplay);

    crtc = drm_find_best_crtc(dev, conn);
    if (!crtc) {
        fprintf(stderr, "drm find crtc failed\n");
        goto err;
    }
    DRM_DEBUG("Best crtc: %d\n", crtc->crtc_id);

    plane = drm_find_best_plane(dev, crtc);
    if (!plane) {
        fprintf(stderr, "drm find plane failed\n");
        goto err;
    }
    configure_plane_zpos(dev, plane->plane_id, 2);
    for (i = 0; i < NUM_DUMB_BO; i++) {
        dev->dumb_bo[i] = bo_create(dev, mode->hdisplay, mode->vdisplay, DRM_FORMAT_ARGB8888);
        if (!dev->dumb_bo[i]) {
            fprintf(stderr, "create dumb bo %d failed\n", i);
            goto err;
        }
        DRM_DEBUG("Created dumb bo fb: %d\n", dev->dumb_bo[i]->fb_id);
    }
    dev->current_dumb = 0;
    dev->current_video = 0;
    dev->connector_id = conn->connector_id;
    dev->crtc_id = crtc->crtc_id;
    dev->plane_id = plane->plane_id;
    dev->last_fb_id = 0;
    dev->mode.hdisplay = mode->hdisplay;
    dev->mode.vdisplay = mode->vdisplay;

    drm_setup_fb_mode(dev);
    DRM_DEBUG("Drm fb mode: %dx%d\n", dev->mode.width, dev->mode.height);

    success = 1;
err:
    drmModeFreeConnector(conn);
    drmModeFreePlane(plane);
    drmModeFreeCrtc(crtc);
    if (!success) {
        drm_free(dev);
        return -1;
    }
    return 0;
}

static void drm_flip_handler(int fd, unsigned frame, unsigned sec,
                             unsigned usec, void *data)
{
    // data is &dev->waiting_for_flip
    DRM_DEBUG("Page flip received(%d)!, %d, %d, %d, %d\n", *(int*)data, fd, frame, sec, usec);
    *(int*)data = 0;
}

int drm_init(int bpp, int rot)
{
    int ret;

    pdev = malloc(sizeof(struct device));
    if (pdev == NULL) {
        fprintf(stderr, "allocate device failed\n");
        return -1;
    }
    memset(pdev, 0, sizeof(*pdev));
    pdev->screen_state = 1;

    //drm_install_sighandler(pdev);

    pdev->fd = drmOpen(NULL, NULL);
    if (pdev->fd < 0)
        pdev->fd = open("/dev/dri/card0", O_RDWR);
    if (pdev->fd < 0) {
        fprintf(stderr, "drm open failed\n");
        goto err_drm_open;
    }
    fcntl(pdev->fd, F_SETFD, FD_CLOEXEC);

    drmSetClientCap(pdev->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

    ret = alloc_fb(pdev, NUM_DUMB_BO, bpp);
    if (ret) {
        fprintf(stderr, "alloc fb failed\n");
        goto err_alloc_fb;
    }

    ret = drm_setup(pdev);
    if (ret) {
        fprintf(stderr, "drm setup failed\n");
        goto err_drm_setup;
    }

    pdev->drm_pollfd.fd = pdev->fd;
    pdev->drm_pollfd.events = POLLIN;

    pdev->drm_evctx.version = DRM_EVENT_CONTEXT_VERSION;
    pdev->drm_evctx.page_flip_handler = drm_flip_handler;
    pdev->rot = rot;

    return 0;
err_alloc_fb:
    drm_free(pdev);
err_drm_setup:
    drmClose(pdev->fd);
err_drm_open:
    free(pdev);
    pdev = NULL;
    return -1;
}

int drm_deinit(void)
{
    struct device* dev = pdev;
    if (!dev)
        return 0;

    free_fb(dev);
    drm_free(dev);

    if (pdev->fd > 0)
        drmClose(dev->fd);

    free(pdev);
    pdev = NULL;

    return 0;
}

int getdrmfd(void)
{
    return pdev->fd;
}

int getdrmrot(void)
{
    return pdev->rot;
}

void getdrmdispbuff(char **buff)
{
    *buff = pdev->dumb_bo[pdev->current_dumb]->ptr;
}

void getdrmdispfd(int *fd)
{
    *fd = pdev->dumb_bo[pdev->current_dumb]->buf_fd;
}

int getdrmdispinfo(struct drm_bo *bo, int *w, int *h)
{
    if (pdev->dumb_bo[pdev->current_dumb]) {
        memcpy(bo, pdev->dumb_bo[pdev->current_dumb], sizeof(struct drm_bo));
        *w = pdev->mode.width;
        *h = pdev->mode.height;
    }

    return 0;
}

struct drm_bo *getdrmdisp(void)
{
    if (pdev->screen_state)
        pdev->current_dumb ++;
    if (pdev->current_dumb >= NUM_DUMB_BO)
        pdev->current_dumb = 0;
    return pdev->dumb_bo[pdev->current_dumb];
}

static void drm_wait_flip(struct device* dev, int timeout)
{
    int ret;

    while (dev->waiting_for_flip) {
        dev->drm_pollfd.revents = 0;
        ret = poll(&dev->drm_pollfd, 1, timeout);
        if (ret <= 0)
            return;

        drmHandleEvent(dev->fd, &dev->drm_evctx);
    }
}

void setdrmdisp(struct drm_bo *bo)
{
    struct device* dev = pdev;
    int crtc_x, crtc_y, crtc_w, crtc_h;
    int ret;
    int fb = bo->fb_id, sw = dev->mode.width, sh = dev->mode.height;

    if (dev == NULL)
        return;
    if (!dev->screen_state)
        return;

    crtc_w = dev->mode.width;
    crtc_h = dev->mode.height;
    crtc_x = 0;
    crtc_y = 0;

    sw = bo->w;
    sh = bo->h;
    crtc_w = sw;
    crtc_h = sh;
    crtc_x = (dev->mode.width - crtc_w) / 2;
    crtc_y = (dev->mode.height - crtc_h) / 2;

    DRM_DEBUG("Display bo %d(%dx%d) at (%d,%d) %dx%d, %d %d %d\n", fb, sw, sh,
             crtc_x, crtc_y, crtc_w, crtc_h, dev->fd, dev->plane_id, dev->crtc_id);
    ret = drmModeSetPlane(dev->fd, dev->plane_id, dev->crtc_id, fb, 0,
                          crtc_x, crtc_y, crtc_w, crtc_h,
                          0, 0, sw << 16, sh << 16);
    if (ret) {
        fprintf(stderr, "drm set plane failed %d\n", ret);
        return;
    }
    if (0) {
        // Queue page flip
        dev->waiting_for_flip = 1;
        ret = drmModePageFlip(dev->fd, dev->crtc_id, fb,
                              DRM_MODE_PAGE_FLIP_EVENT, &dev->waiting_for_flip);
        if (ret) {
            fprintf(stderr, "drm page flip failed\n");
            return;
        }
        // Wait for last page flip
        drm_wait_flip(dev, -1);
    }
}

int createDrmVideoDisp(int format, int w, int h)
{
    int rot = getdrmrot();

    if (rot == 90 || rot == 270)
    {
        int tmp;
        tmp = w;
        w = h;
        h = tmp;
    }
    for (int i = 0; i < NUM_DUMB_BO; i++) {
        pdev->video_bo[i] = bo_create(pdev, w, h, format);
        if (!pdev->video_bo[i]) {
            fprintf(stderr, "create video bo %d failed\n", i);
            return -1;
        }
        DRM_DEBUG("Created video bo fb: %d\n", pdev->video_bo[i]->fb_id);
    }
}

int destroyDrmVideoDisp(void)
{
    for (int i = 0; i < NUM_DUMB_BO; i++) {
        if (pdev->video_bo[i]) {
            bo_destroy(pdev, pdev->video_bo[i]);
            pdev->video_bo[i] = NULL;
        }
    }
}

void getdrmvideodispbuff(char **buff)
{
    if (pdev->video_bo[pdev->current_video])
        *buff = pdev->video_bo[pdev->current_video]->ptr;
}

void getdrmvideodispfd(int *fd)
{
    if (pdev->video_bo[pdev->current_video])
        *fd = pdev->video_bo[pdev->current_video]->buf_fd;
}

int getdrmvideodispinfo(struct drm_bo *bo, int *w, int *h)
{
    if (pdev->video_bo[pdev->current_video]) {
        memcpy(bo, pdev->video_bo[pdev->current_video], sizeof(struct drm_bo));
        *w = pdev->mode.width;
        *h = pdev->mode.height;
    }

    return 0;
}

struct drm_bo *getblackvideodisp(void)
{
    return pdev->black_bo;
}

struct drm_bo *getdrmvideodisp(void)
{
    if (pdev->screen_state)
        pdev->current_video ++;
    if (pdev->current_video >= NUM_DUMB_BO)
        pdev->current_video = 0;
    if (pdev->video_bo[pdev->current_video])
        return pdev->video_bo[pdev->current_video];

    return NULL;
}

int drm_screen_off(void)
{
    struct device *dev = pdev;
    struct drm_bo *bo;
    int current_dumb;
    int *buff;
    int sw;
    int w, h;
    int ret;
    int i;

    if (dev == NULL)
        return -1;
    if (!dev->screen_state)
        return -1;
    dev->screen_state = 0;
    current_dumb = pdev->current_dumb + 1;

    if (current_dumb >= NUM_DUMB_BO)
        current_dumb = 0;
    bo = pdev->dumb_bo[current_dumb];
    buff = bo->ptr;
    getdrmresolve(&w, &h);
    sw = getdrmvideowstride();
    for (i = 0; i < sw * h; i++) {
        buff[i] = 0xff000000;
    }
    ret = drmModeSetPlane(dev->fd, dev->plane_id, dev->crtc_id, bo->fb_id, 0,
                          0, 0, dev->mode.width, dev->mode.height,
                          0, 0, dev->mode.width << 16, dev->mode.height << 16);
    if (ret) {
        fprintf(stderr, "drm set plane failed\n");
    }
    return 0;
}

int drm_screen_on(void)
{
    struct device *dev = pdev;
    struct drm_bo *bo;
    int current_dumb;
    int ret;
    if (dev->screen_state)
        return -1;
    current_dumb = pdev->current_dumb;

    bo = pdev->dumb_bo[current_dumb];

    ret = drmModeSetPlane(dev->fd, dev->plane_id, dev->crtc_id, bo->fb_id, 0,
                          0, 0, dev->mode.width, dev->mode.height,
                          0, 0, dev->mode.width << 16, dev->mode.height << 16);
    if (ret) {
        fprintf(stderr, "drm set plane failed\n");
    }
    dev->screen_state = 1;

    return 0;
}

int drm_invalide(void)
{
    return pdev && pdev->drm_invalide;
}

void getdrmdispbpp(int *bpp)
{
    *bpp = pdev->mode.bpp;
}

int getdrmwstride(void)
{
    return pdev->dumb_bo[pdev->current_dumb]->pitch / 4;
}

int getdrmvideowstride(void)
{
    if (pdev->video_bo[pdev->current_video])
        return pdev->video_bo[pdev->current_video]->pitch;
    return 0;
}

void getdrmresolve(int *w, int *h)
{
    *w = pdev->mode.width;
    *h = pdev->mode.height;
}

