#include "UDMABuf.hpp"
#include <drm_fourcc.h>
#include <fcntl.h>
#include <linux/memfd.h>
#include <linux/udmabuf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <cstring>

using namespace Aquamarine;
using namespace Hyprutils::Memory;
using namespace Hyprutils::Math;

#define SP CSharedPointer

// glibc only exposed memfd_create starting with 2.27; fall back to the raw syscall to avoid a
// hard dependency we don't otherwise need.
static int aq_memfd_create(const char* name, unsigned int flags) {
#ifdef __NR_memfd_create
    return syscall(__NR_memfd_create, name, flags);
#else
    errno = ENOSYS;
    return -1;
#endif
}

SP<CUDMABuf> CUDMABuf::create(const Vector2D& size, uint32_t format) {
    auto buf = SP<CUDMABuf>(new CUDMABuf());

    const uint32_t stride   = (uint32_t)size.x * 4; // 32bpp
    const size_t   rawLen   = (size_t)stride * (size_t)size.y;
    const size_t   pageSize = (size_t)sysconf(_SC_PAGESIZE);
    const size_t   len      = (rawLen + pageSize - 1) & ~(pageSize - 1);

    buf->memfd = aq_memfd_create("aquamarine-udmabuf", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (buf->memfd < 0)
        return nullptr;

    if (ftruncate(buf->memfd, len) < 0)
        return nullptr;

    if (fcntl(buf->memfd, F_ADD_SEALS, F_SEAL_SHRINK) < 0)
        return nullptr;

    buf->data = (uint8_t*)mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, buf->memfd, 0);
    if (buf->data == MAP_FAILED) {
        buf->data = nullptr;
        return nullptr;
    }
    buf->bufLen = len;

    int udmabuf = open("/dev/udmabuf", O_RDWR | O_CLOEXEC);
    if (udmabuf < 0)
        return nullptr;

    struct udmabuf_create createParams = {
        .memfd  = (uint32_t)buf->memfd,
        .flags  = UDMABUF_FLAGS_CLOEXEC,
        .offset = 0,
        .size   = len,
    };
    buf->dmabufFD = ioctl(udmabuf, UDMABUF_CREATE, &createParams);
    close(udmabuf);

    if (buf->dmabufFD < 0)
        return nullptr;

    buf->attrs.success    = true;
    buf->attrs.size       = size;
    buf->attrs.format     = format;
    // udmabuf hands back plain linear host pages with no driver-specific layout. Tag the modifier
    // as INVALID so EGL importers go through their implicit-modifier path, which avoids relying on
    // any particular driver advertising LINEAR support for the chosen format.
    buf->attrs.modifier   = DRM_FORMAT_MOD_INVALID;
    buf->attrs.planes     = 1;
    buf->attrs.offsets[0] = 0;
    buf->attrs.strides[0] = stride;
    buf->attrs.fds[0]     = buf->dmabufFD;

    buf->size = size;

    return buf;
}

CUDMABuf::~CUDMABuf() {
    events.destroy.emit();

    if (data && data != MAP_FAILED)
        munmap(data, bufLen);
    if (dmabufFD >= 0)
        close(dmabufFD);
    if (memfd >= 0)
        close(memfd);
}

eBufferCapability CUDMABuf::caps() {
    return BUFFER_CAPABILITY_DATAPTR;
}

eBufferType CUDMABuf::type() {
    return BUFFER_TYPE_DMABUF;
}

void CUDMABuf::update(const CRegion& damage) {
    ;
}

bool CUDMABuf::isSynchronous() {
    return false; // GPUs sample/render this dma-buf directly; CPU access is auxiliary.
}

bool CUDMABuf::good() {
    return attrs.success && data;
}

SDMABUFAttrs CUDMABuf::dmabuf() {
    return attrs;
}

std::tuple<uint8_t*, uint32_t, size_t> CUDMABuf::beginDataPtr(uint32_t flags) {
    return {data, attrs.format, bufLen};
}

void CUDMABuf::endDataPtr() {
    ;
}
