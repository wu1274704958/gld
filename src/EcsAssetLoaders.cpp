#include <glad/glad.h>
#include <stb_image.h>
#include <sundry.hpp>
#include <program.hpp>
#include <texture.hpp>

#include <ecs/assets/Loader.hpp>

#include <vector>
#include <string>
#include <cstdio>

namespace gld::ecs {

    // ---------------- TextureLoader ----------------
    namespace {
        struct CpuImage {
            unsigned char* data = nullptr; // owned (stbi), freed in dtor
            std::vector<unsigned char> packed;
            int w = 0, h = 0, comp = 0;
            ~CpuImage() { if (data) stbi_image_free(data); }
            const unsigned char* pixels() const {
                return packed.empty() ? data : packed.data();
            }
            void release_decode() {
                if (data) stbi_image_free(data);
                data = nullptr;
            }
        };

        GLenum gl_format_for(int comp) {
            switch (comp) {
            case 1:  return GL_RED;
            case 2:  return GL_RG;
            case 3:  return GL_RGB;
            case 4:  return GL_RGBA;
            default: return GL_RGB;
            }
        }

        GLenum gl_internal_format_for(int comp) {
            switch (comp) {
            case 1:  return GL_R8;
            case 2:  return GL_RG8;
            case 3:  return GL_RGB;
            case 4:  return GL_RGBA;
            default: return GL_RGB;
            }
        }
    }

    std::shared_ptr<void> TextureLoader::load_cpu(const TextureDesc& desc, const IFileSystem& fs) {
        if (!valid_channel_mapping(desc.channels(), desc.channel_mapping())) {
            std::fprintf(stderr, "[TextureLoader] invalid channels/mapping for %s\n",
                         desc.path().c_str());
            return nullptr;
        }
        auto bytes = fs.read_bytes(desc.path());
        if (!bytes) return nullptr;

        stbi_set_flip_vertically_on_load(desc.flip() ? 1 : 0);
        const bool mapped = desc.channel_mapping() != TextureChannelMapping::Default;
        int req = mapped ? 4 : static_cast<int>(desc.channels()); // Auto=0 lets stb pick
        auto img = std::make_shared<CpuImage>();
        img->data = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(bytes->data()),
            static_cast<int>(bytes->size()),
            &img->w, &img->h, &img->comp, req);
        if (!img->data) return nullptr;
        if (req != 0) img->comp = req; // channels forced
        if (mapped) {
            const auto pixel_count = static_cast<std::size_t>(img->w)
                * static_cast<std::size_t>(img->h);
            img->packed = detail::pack_texture_channels(
                std::span<const unsigned char>(img->data, pixel_count * 4u),
                pixel_count, desc.channel_mapping());
            if (img->packed.empty()) return nullptr;
            img->release_decode();
            img->comp = static_cast<int>(texture_channel_count(desc.channels()));
        }
        return img;
    }

    std::shared_ptr<Texture<TexType::D2>> TextureLoader::finalize(std::shared_ptr<void> cpu, const TextureDesc& desc) {
        auto img = std::static_pointer_cast<CpuImage>(cpu);
        if (!img || !img->pixels()) return nullptr;

        GLenum fmt = gl_format_for(img->comp);
        GLenum internal_fmt = gl_internal_format_for(img->comp);
        auto tex = std::make_shared<Texture<TexType::D2>>();
        tex->create();
        tex->bind();
        glPixelStorei(GL_UNPACK_ALIGNMENT, (img->comp == 4) ? 4 : 1);
        tex->tex_image(0, internal_fmt, 0, fmt, img->pixels(), img->w, img->h);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
            desc.wrap_s() == TextureWrap::ClampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
            desc.wrap_t() == TextureWrap::ClampToEdge ? GL_CLAMP_TO_EDGE : GL_REPEAT);
        const GLint min_filter = desc.min_filter() == TextureFilter::Nearest
            ? (desc.mipmap() ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST)
            : (desc.mipmap() ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        const GLint mag_filter = desc.mag_filter() == TextureFilter::Nearest ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);
        if (desc.channel_mapping() == TextureChannelMapping::RedAlpha)
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_A, GL_GREEN);
        if (desc.mipmap()) tex->generate_mipmap();
        tex->measure.width = img->w;
        tex->measure.height = img->h;
        tex->unbind();
        return tex;
    }

    // ---------------- ProgramLoader ----------------
    namespace {
        struct CpuProgram {
            std::string vs, fs, gs;
            bool ok = false;
        };
    }

    std::shared_ptr<void> ProgramLoader::load_cpu(const ProgramDesc& desc, const IFileSystem& fs) {
        auto p = std::make_shared<CpuProgram>();
        auto vs = fs.read_text(desc.vs());
        auto fg = fs.read_text(desc.fs());
        if (!vs || !fg) return p; // ok stays false
        p->vs = std::move(*vs);
        p->fs = std::move(*fg);
        if (!desc.gs().empty()) {
            if (auto gs = fs.read_text(desc.gs())) p->gs = std::move(*gs);
            else return p;
        }
        p->ok = true;
        return p;
    }

    std::shared_ptr<Program> ProgramLoader::finalize(std::shared_ptr<void> cpu, const ProgramDesc& desc) {
        auto src = std::static_pointer_cast<CpuProgram>(cpu);
        if (!src || !src->ok) return nullptr;

        Shader<ShaderType::VERTEX> vertex;
        Shader<ShaderType::FRAGMENT> frag;
        const char* vs_p = src->vs.c_str();
        const char* fg_p = src->fs.c_str();
        try {
            sundry::compile_shaders<100>(
                GL_VERTEX_SHADER, &vs_p, 1, (GLuint*)vertex,
                GL_FRAGMENT_SHADER, &fg_p, 1, (GLuint*)frag);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[ProgramLoader] compile failed vs=%s fs=%s: %s\n",
                         desc.vs().c_str(), desc.fs().c_str(), e.what());
            return nullptr;
        }

        auto prog = std::make_shared<Program>();
        prog->cretate();
        prog->attach_shader(std::move(vertex));
        prog->attach_shader(std::move(frag));

        if (!src->gs.empty()) {
            Shader<ShaderType::GEOMETRY> geom;
            const char* gs_p = src->gs.c_str();
            try {
                sundry::compile_shaders<100>(GL_GEOMETRY_SHADER, &gs_p, 1, (GLuint*)geom);
            } catch (const std::exception& e) {
                std::fprintf(stderr, "[ProgramLoader] compile failed gs=%s: %s\n",
                             desc.gs().c_str(), e.what());
                return nullptr;
            }
            prog->attach_shader(std::move(geom));
        }

        prog->link();
        auto [succ, err] = prog->check_link_state<200>();
        if (!succ) {
            std::fprintf(stderr, "[ProgramLoader] link failed vs=%s fs=%s: %s\n",
                         desc.vs().c_str(), desc.fs().c_str(), err ? err->c_str() : "");
            return nullptr;
        }
        return prog;
    }
}
