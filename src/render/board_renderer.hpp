// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <vector>

#include "render/image_io.hpp"
#include "render/offscreen_target.hpp"
#include "render/piece_mesh.hpp"
#include "view/camera.hpp"
#include "view/layout.hpp"
#include "view/snapshot.hpp"
#include "view/theme.hpp"

namespace cb::render {

/// One instanced box or piece.
struct Instance {
  float center[3]{};
  float scale[3]{};
  float color[4]{};
  /// Seam colour, alpha 0 when this cell is not on a glued face.
  float edge[4]{};
  /// Which sides are glued: bit 0 = -x, 1 = +x, 2 = -y, 3 = +y.
  float edgeMask{0};
  float pad{0};
};
static_assert(sizeof(Instance) == 64);

/// The sub-rectangle of the target the board is drawn into, in pixels. Zero width means
/// the whole target. At namespace scope because a nested class's default member
/// initializers cannot be used in a default argument inside the enclosing class.
struct BoardRect {
  float x{0}, y{0}, width{0}, height{0};
  [[nodiscard]] bool valid() const noexcept { return width > 0 && height > 0; }
};

/// Instances grouped by the shape that draws them - one draw call per shape.
struct InstanceSet {
  std::vector<Instance> instances;
  struct Batch {
    std::uint32_t first{0};
    std::uint32_t count{0};
  };
  std::array<Batch, static_cast<std::size_t>(Archetype::Count)> batches{};

  [[nodiscard]] std::size_t size() const noexcept { return instances.size(); }
};

/// Draws a position. Consumes an immutable PositionView, never a live Position, and
/// never generates a move of its own - a highlight it draws came from the engine.
class BoardRenderer {
 public:
  static Result<BoardRenderer> create(const VulkanContext& ctx);

  BoardRenderer() = default;
  ~BoardRenderer();
  BoardRenderer(const BoardRenderer&) = delete;
  BoardRenderer& operator=(const BoardRenderer&) = delete;
  BoardRenderer(BoardRenderer&& o) noexcept { *this = std::move(o); }
  BoardRenderer& operator=(BoardRenderer&& o) noexcept;

  [[nodiscard]] const view::Theme& theme() const noexcept { return theme_; }
  void setTheme(const view::Theme& t) noexcept { theme_ = t; }

  /// Cells the engine says were part of the last move, drawn with a lingering mark so a
  /// player can see what just happened without reading the ledger.
  void setLastMove(CellId from, CellId to) noexcept {
    lastFrom_ = from;
    lastTo_ = to;
  }
  /// Royal cell to mark as under attack, or kInvalidCell.
  void setCheckCell(CellId c) noexcept { checkCell_ = c; }

  /// Turn a snapshot into instances. Pure and side-effect free, so it is unit-testable
  /// with no Vulkan at all - which is where most rendering bugs are actually found.
  [[nodiscard]] InstanceSet buildInstances(const view::PositionView& p,
                                           const view::ViewConfig& cfg) const;

  /// Render one frame into `target`, leaving the colour image in TRANSFER_SRC_OPTIMAL.
  /// `overlay` records extra commands inside the same render pass, which is how the UI
  /// is drawn over the board without a second path.
  Result<void> render(const OffscreenTarget& target, const InstanceSet& instances,
                      const view::OrbitCamera& camera,
                      const std::function<void(VkCommandBuffer)>& overlay = {},
                      BoardRect boardRect = {});

  Result<Image> renderToImage(const OffscreenTarget& target, const view::PositionView& p,
                              const view::ViewConfig& cfg,
                              const view::OrbitCamera& camera);

  [[nodiscard]] static view::Vec3 cellHalfExtent();

 private:
  Result<void> buildPipeline();
  Result<void> uploadGeometry();
  Result<void> ensureInstanceCapacity(std::size_t count);

  const VulkanContext* ctx_{nullptr};
  view::Theme theme_{};
  MeshLibrary meshes_;
  CellId lastFrom_{kInvalidCell};
  CellId lastTo_{kInvalidCell};
  CellId checkCell_{kInvalidCell};

  VkShaderModule vert_{VK_NULL_HANDLE};
  VkShaderModule frag_{VK_NULL_HANDLE};
  VkPipelineLayout layout_{VK_NULL_HANDLE};
  VkPipeline pipeline_{VK_NULL_HANDLE};

  VkBuffer vertexBuffer_{VK_NULL_HANDLE};
  VkDeviceMemory vertexMem_{VK_NULL_HANDLE};
  VkBuffer indexBuffer_{VK_NULL_HANDLE};
  VkDeviceMemory indexMem_{VK_NULL_HANDLE};

  VkBuffer instanceBuffer_{VK_NULL_HANDLE};
  VkDeviceMemory instanceMem_{VK_NULL_HANDLE};
  std::size_t instanceCapacity_{0};
};

}  // namespace cb::render
