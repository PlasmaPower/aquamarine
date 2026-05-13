#pragma once

#include <aquamarine/buffer/Buffer.hpp>
#include <hyprutils/memory/SharedPtr.hpp>

namespace Aquamarine {
    // Host-memory dma-buf backed by a sealed memfd, exported as a generic dma-buf via udmabuf.
    // Unlike a DRM dumb buffer, this is not tied to any DRM device, so it can be imported as an
    // EGLImage on GPU vendors that refuse cross-DRM-device dma-buf imports (e.g. NVIDIA).
    //
    // Requires /dev/udmabuf (CONFIG_UDMABUF=y plus a device node).
    class CUDMABuf : public IBuffer {
      public:
        // Allocates a linear 32bpp buffer of the given size and fourcc. Returns nullptr on failure
        // (most commonly: /dev/udmabuf is not available).
        static Hyprutils::Memory::CSharedPointer<CUDMABuf> create(const Hyprutils::Math::Vector2D& size, uint32_t format);

        virtual ~CUDMABuf();

        virtual eBufferCapability                      caps();
        virtual eBufferType                            type();
        virtual void                                   update(const Hyprutils::Math::CRegion& damage);
        virtual bool                                   isSynchronous();
        virtual bool                                   good();
        virtual SDMABUFAttrs                           dmabuf();
        virtual std::tuple<uint8_t*, uint32_t, size_t> beginDataPtr(uint32_t flags);
        virtual void                                   endDataPtr();

      private:
        CUDMABuf() = default;

        int          memfd    = -1;
        int          dmabufFD = -1;
        uint8_t*     data     = nullptr;
        size_t       bufLen   = 0;
        SDMABUFAttrs attrs{.success = false};
    };
};
