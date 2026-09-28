#pragma once
// RmlUi's GL3 sample renderer decodes only TGA. This subclass adds PNG via
// the vendored stb_image decoder and otherwise defers to the sample renderer.
#include <RmlUi_Renderer_GL3.h>

namespace c2::frontend::gui::app {
class PngRenderInterface final : public RenderInterface_GL3 {
public:
    Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
};
} // namespace c2::frontend::gui::app
