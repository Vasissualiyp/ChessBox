// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <vector>

#include "render/image_io.hpp"
#include "render/offscreen_target.hpp"
#include "view/camera.hpp"
#include "view/layout.hpp"
#include "view/snapshot.hpp"

namespace cb::render {

/// Colours, kept in one place so a theme is data and the renderer holds no opinions.
struct Theme {
  Image::Rgba background{24, 26, 32, 255};
  Image::Rgba lightCell{198, 190, 172, 255};
  Image::Rgba darkCell{112, 104, 92, 255};
  Image::Rgba selected{232, 196, 72, 255};
  Image::Rgba legalTarget{96, 176, 112, 255};
  Image::Rgba whitePiece{244, 244, 238, 255};
  Image::Rgba blackPiece{40, 40, 46, 255};
};

/// One instanced box. Cells and pieces are the same primitive: the draw-call count is
/// independent of board size, so a million-cell lattice is a buffer upload rather than a
/// scene graph (ARCH section 10).
struct Instance {
  float center[3]{};
  float scale[3]{};
  float color[4]{};
};
static_assert(sizeof(Instance) == 40);

/// Draws a position. Consumes an immutable PositionView, never a live Position, and never
/// generates a move of its own - a highlight it draws came from the engine.
class BoardRenderer {
 public:
  static Result<BoardRenderer> create(const VulkanContext& ctx);

  BoardRenderer() = default;
  ~BoardRenderer();
  BoardRenderer(const BoardRenderer&) = delete;
  BoardRenderer& operator=(const BoardRenderer&) = delete;
  BoardRenderer(BoardRenderer&& o) noexcept { *this = std::move(o); }
  BoardRenderer& operator=(BoardRenderer&& o) noexcept;

  [[nodiscard]] const Theme& theme() const noexcept { return theme_; }
  void setTheme(const Theme& t) noexcept { theme_ = t; }

  /// Turn a snapshot into instances. Pure and side-effect free, so it is unit-testable
  /// with no Vulkan at all - which is where most rendering bugs are actually found.
  [[nodiscard]] std::vector<Instance> buildInstances(const view::PositionView& p,
                                                     const view::ViewConfig& cfg) const;

  /// Render one frame into `target` and leave the colour image in
  /// TRANSFER_SRC_OPTIMAL, ready for readPixels().
  Result<void> render(const OffscreenTarget& target,
                      const std::vector<Instance>& instances,
                      const view::OrbitCamera& camera);

  /// Convenience: build, render, read back.
  Result<Image> renderToImage(const OffscreenTarget& target, const view::PositionView& p,
                              const view::ViewConfig& cfg,
                              const view::OrbitCamera& camera);

  /// Half-extent of a cell box, for CPU picking against the same layout the renderer
  /// drew.
  [[nodiscard]] static view::Vec3 cellHalfExtent();

 private:
  Result<void> buildPipeline();
  Result<void> uploadGeometry();
  Result<void> ensureInstanceCapacity(std::size_t count);

  const VulkanContext* ctx_{nullptr};
  Theme theme_{};

  VkShaderModule vert_{VK_NULL_HANDLE};
  VkShaderModule frag_{VK_NULL_HANDLE};
  VkPipelineLayout layout_{VK_NULL_HANDLE};
  VkPipeline pipeline_{VK_NULL_HANDLE};

  VkBuffer vertexBuffer_{VK_NULL_HANDLE};
  VkDeviceMemory vertexMem_{VK_NULL_HANDLE};
  VkBuffer indexBuffer_{VK_NULL_HANDLE};
  VkDeviceMemory indexMem_{VK_NULL_HANDLE};
  std::uint32_t indexCount_{0};

  VkBuffer instanceBuffer_{VK_NULL_HANDLE};
  VkDeviceMemory instanceMem_{VK_NULL_HANDLE};
  std::size_t instanceCapacity_{0};
};

}  // namespace cb::render
