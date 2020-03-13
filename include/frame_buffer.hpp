#pragma once

#include <buffer.hpp>
#include <texture.hpp>

namespace gld{
    

    struct RenderBuffer;

    struct FrameBuffer : public buf::Buffer<BufferType::FRAME>
    {
        constexpr static BufferType BT = BufferType::FRAME;

        void create() override
        {
            glGenFramebuffers(1,&id);
        }

        void destroy() override
        {
            glDeleteFramebuffers(1,&id);
        }

        void bind() override
        {
            glBindFramebuffer(static_cast<size_t>(BT),id);
        }

        void unbind() override
        {
            glBindFramebuffer(static_cast<size_t>(BT),0);
        }

        GLenum check_status()
        {
            return glCheckFramebufferStatus(static_cast<size_t>(BT));
        }

        template <TexType TT>
        void attach_texture(Texture<TT>& texture, GLenum attachment, GLint level, GLint zoffset = 0)
        {
            if constexpr(TT == TexType::D3)
            {
                glFramebufferTexture3D(static_cast<size_t>(BT), attachment , static_cast<size_t>(TT), texture.get_id(), level,zoffset);
            }else 
            if constexpr(TT == TexType::D2)
            {
                glFramebufferTexture2D(static_cast<size_t>(BT), attachment , static_cast<size_t>(TT), texture.get_id(), level);
            }else
            if constexpr(TT == TexType::D1)
            {
                glFramebufferTexture1D(static_cast<size_t>(BT), attachment , static_cast<size_t>(TT), texture.get_id(), level);
            }
        }

        template <BufferType Bt>
        void attach_buffer(GLenum attachment,buf::Buffer<Bt>& renderbuffer)
        {
            if constexpr(Bt == BufferType::RENDER)
            {
                glFramebufferRenderbuffer(static_cast<size_t>(BT), attachment, static_cast<size_t>(Bt), renderbuffer.get_id());
            }
        }
        
    };

    struct RenderBuffer : public buf::Buffer<BufferType::RENDER>
    {
        constexpr static BufferType BT = BufferType::RENDER;

        void create() override
        {
            glGenRenderbuffers(1,&id);
        }

        void destroy() override
        {
            glDeleteRenderbuffers(1,&id);
        }

        void bind() override
        {
            glBindRenderbuffer(static_cast<size_t>(BT),id);
        }

        void unbind() override
        {
            glBindRenderbuffer(static_cast<size_t>(BT),0);
        }

        void storage(GLenum internalformat, GLsizei width, GLsizei height)
        {
            glRenderbufferStorage(static_cast<size_t>(BT), internalformat, width, height);
        }
    };
}