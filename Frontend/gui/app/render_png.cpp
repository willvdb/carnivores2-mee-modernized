#include "render_png.hpp"
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/Log.h>
#include <cstring>
#include <memory>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_ASSERT(x) ((void)0)
#include <stb_image.h>

namespace c2::frontend::gui::app {
Rml::TextureHandle PngRenderInterface::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) {
    const bool png = source.size() >= 4 &&
        (source.compare(source.size() - 4, 4, ".png") == 0 || source.compare(source.size() - 4, 4, ".PNG") == 0);
    if (!png) return RenderInterface_GL3::LoadTexture(texture_dimensions, source);

    Rml::FileInterface* files = Rml::GetFileInterface();
    Rml::FileHandle handle = files->Open(source);
    if (!handle) {
        Rml::Log::Message(Rml::Log::LT_WARNING, "Optional artwork not found: %s", source.c_str());
        return 0;
    }
    const size_t length = files->Length(handle);
    std::vector<unsigned char> bytes(length);
    const size_t read = length ? files->Read(bytes.data(), length, handle) : 0;
    files->Close(handle);
    if (read != length || length == 0) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Cannot read artwork bytes: %s", source.c_str());
        return 0;
    }
    int width = 0, height = 0, channels = 0;
    const int max_dimension = 8192;
    if (length > static_cast<size_t>(64) * 1024 * 1024) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Artwork exceeds 64 MiB: %s", source.c_str());
        return 0;
    }
    unsigned char* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(length), &width, &height, &channels, 4);
    if (!pixels) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "PNG decode failed for %s: %s", source.c_str(), stbi_failure_reason());
        return 0;
    }
    std::unique_ptr<unsigned char, void (*)(void*)> owned(pixels, stbi_image_free);
    if (width <= 0 || height <= 0 || width > max_dimension || height > max_dimension) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "PNG dimensions unsupported (%dx%d): %s", width, height, source.c_str());
        return 0;
    }
    // RmlUi expects non-premultiplied RGBA8 through GenerateTexture, which the
    // GL3 renderer premultiplies itself.
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    Rml::Span<const Rml::byte> span(reinterpret_cast<const Rml::byte*>(pixels), count);
    texture_dimensions = Rml::Vector2i(width, height);
    return GenerateTexture(span, texture_dimensions);
}
} // namespace c2::frontend::gui::app
