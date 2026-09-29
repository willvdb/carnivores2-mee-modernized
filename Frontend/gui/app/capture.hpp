#pragma once
// Framebuffer capture for evidence: reads the current GL back buffer and
// writes an uncompressed PNG (stored deflate blocks, no zlib dependency).
#include <filesystem>
#include <string>

namespace c2::frontend::gui::app {
// Reads `width` x `height` pixels from the current framebuffer (after a
// render, before the swap) and writes them to `path`. Returns false with a
// reason on failure.
bool capture_framebuffer_png(int width, int height, const std::filesystem::path& path, std::string& error);
} // namespace c2::frontend::gui::app
