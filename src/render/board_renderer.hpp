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
#include "view/move_anim.hpp"
#include "view/seams.hpp"
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

/// What the board shows. Driven by the player's settings; the renderer holds no
/// opinions of its own about what is worth marking.
struct BoardOptions {
  bool showLegalMoves{true};
  bool showLastMove{true};
  bool showCheck{true};
  bool showSeams{true};
  /// 1 keeps the height a piece's value implies; 0 makes every piece the same height.
  float pieceHeightScale{1.0f};
  /// Draw the board flat, looking straight down, with pieces as tokens rather than as
  /// models. Fewer triangles, no orbiting, and - the point of it - readable on a
  /// machine that cannot comfortably draw the three-dimensional scene.
  bool flat{false};
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
  [[nodiscard]] const BoardOptions& options() const noexcept { return options_; }
  void setOptions(const BoardOptions& o) noexcept { options_ = o; }

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
  /// `seams` colours the edges that are not really edges, and `anim` moves one piece
  /// off its cell while it is travelling. Both are optional: a still frame of a plain
  /// box needs neither, and the tests that cover instance building pass nullptr.
  [[nodiscard]] InstanceSet buildInstances(
      const view::PositionView& p, const view::ViewConfig& cfg,
      const view::SeamMap* seams = nullptr,
      const view::MoveAnimation* anim = nullptr) const;

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
  Result<void> buildBackdropPipeline();
  Result<void> uploadGeometry();
  Result<void> ensureInstanceCapacity(std::size_t count);

  const VulkanContext* ctx_{nullptr};
  view::Theme theme_{};
  BoardOptions options_{};
  MeshLibrary meshes_;
  CellId lastFrom_{kInvalidCell};
  CellId lastTo_{kInvalidCell};
  CellId checkCell_{kInvalidCell};

  VkShaderModule vert_{VK_NULL_HANDLE};
  VkShaderModule frag_{VK_NULL_HANDLE};
  VkPipelineLayout layout_{VK_NULL_HANDLE};
  VkPipeline pipeline_{VK_NULL_HANDLE};

  // The lit ground the board stands on, drawn first across the whole frame.
  VkShaderModule backdropVert_{VK_NULL_HANDLE};
  VkShaderModule backdropFrag_{VK_NULL_HANDLE};
  VkPipelineLayout backdropLayout_{VK_NULL_HANDLE};
  VkPipeline backdropPipeline_{VK_NULL_HANDLE};

  VkBuffer vertexBuffer_{VK_NULL_HANDLE};
  VkDeviceMemory vertexMem_{VK_NULL_HANDLE};
  VkBuffer indexBuffer_{VK_NULL_HANDLE};
  VkDeviceMemory indexMem_{VK_NULL_HANDLE};

  VkBuffer instanceBuffer_{VK_NULL_HANDLE};
  VkDeviceMemory instanceMem_{VK_NULL_HANDLE};
  std::size_t instanceCapacity_{0};
};

}  // namespace cb::render
