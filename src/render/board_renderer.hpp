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
  /// Rotation about the mesh's own Z, in radians. Lets one mesh - the rounded corner
  /// of a timeline connector - be placed in any of the four quadrants without four
  /// copies of the geometry.
  float roll{0};
  /// Full orientation as a quaternion (x,y,z,w), applied after `roll`. Identity on the
  /// flat board; on the geometry view it turns a cell's local +Z onto the surface normal,
  /// so a tile or a piece stands on the warped board with the ordinary camera and depth
  /// buffer (M17).
  float quat[4]{0.0f, 0.0f, 0.0f, 1.0f};
  /// 1 for a mirror face, which is shaded as polished metal; 0 for everything else.
  float metal{0};
};
static_assert(sizeof(Instance) == 84);

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
  /// Draw the board as the surface its geometry describes: cells become tiles on the
  /// warped surface and pieces ride its normals, all through the ordinary camera (M17).
  /// Only meaningful for a glued 2-D variant; `buildInstances` ignores it otherwise.
  bool surface{false};
  /// Where the board sits on that surface, in cells: slide it one along the files and a1
  /// is where b1 was. A pose, not a shape - the cells, the moves and the position are
  /// untouched at any value (M17).
  float surfaceSlideU{0.0f};
  float surfaceSlideV{0.0f};
  /// How far the surface has been turned through itself, 0 to 1 - a torus pulled inside
  /// out through its own hole, a cylinder rolled back over itself.
  float surfaceEvert{0.0f};
  /// The board mesh's alpha, 1 opaque down to a translucent ghost so the far side of the
  /// shape and the pieces on it show through (M17.10). The pieces stay opaque.
  float surfaceGhost{1.0f};
};

/// Instances grouped by the shape that draws them - one draw call per shape.
struct InstanceSet {
  std::vector<Instance> instances;
  struct Batch {
    std::uint32_t first{0};
    std::uint32_t count{0};
  };
  std::array<Batch, static_cast<std::size_t>(Archetype::Count)> batches{};

  /// The geometry view's board, as one mesh (M17). A square on a curved surface is a
  /// patch of that surface, not a slab turned to face it, so it cannot be an instance of
  /// anything: it is built fresh each frame with its colour in its vertices. Empty
  /// everywhere else, which is every screen but one.
  std::vector<MeshVertex> surfaceVertices;
  std::vector<std::uint32_t> surfaceIndices;
  /// Which instance carries the transform for that mesh - an identity one, because the
  /// vertices are already in world space. The pipeline needs *an* instance bound.
  std::uint32_t surfaceInstance{0};

  [[nodiscard]] std::size_t size() const noexcept { return instances.size(); }
};

/// Draws a position. Consumes an immutable PositionView, never a live Position, and
/// never generates a move of its own - a highlight it draws came from the engine.
class BoardRenderer {
 public:
  static Result<BoardRenderer> create(
      const VulkanContext& ctx, VkFormat colorFormat = OffscreenTarget::kColorFormat);

  /// How many frames may be in flight at once. Two: the interactive path renders into a
  /// freshly acquired swapchain image while the previous frame is still being presented,
  /// which is the whole of M4.8's pipelining (ADR-0018).
  static constexpr std::uint32_t kFramesInFlight = 2;

  BoardRenderer() = default;
  ~BoardRenderer();
  BoardRenderer(const BoardRenderer&) = delete;
  BoardRenderer& operator=(const BoardRenderer&) = delete;
  BoardRenderer(BoardRenderer&& o) noexcept { *this = std::move(o); }
  BoardRenderer& operator=(BoardRenderer&& o) noexcept;

  [[nodiscard]] const view::Theme& theme() const noexcept { return theme_; }
  void setTheme(const view::Theme& t) noexcept { theme_ = t; }
  /// How far out of focus the board is, 0 to 1.
  ///
  /// Pause steps the camera back and throws the board out of focus so the menu in front
  /// of it is what the eye lands on. Zero costs nothing at all - the passes are skipped,
  /// not run with a zero radius - so a game in play pays for none of it.
  void setBlur(float strength) noexcept { blur_ = strength; }
  [[nodiscard]] float blur() const noexcept { return blur_; }

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
      const view::SeamMap* seams = nullptr, const view::MoveAnimation* anim = nullptr,
      const std::function<bool(CellId)>& visible = {},
      const std::function<bool(CellId)>& present = {},
      const std::vector<view::TimelineLink>& links = {}) const;
  /// Render one frame into `target`, leaving the colour image in TRANSFER_SRC_OPTIMAL.
  /// `overlay` records extra commands inside the same render pass, which is how the UI
  /// is drawn over the board without a second path.
  Result<void> render(const OffscreenTarget& target, const InstanceSet& instances,
                      const view::OrbitCamera& camera,
                      const std::function<void(VkCommandBuffer)>& overlay = {},
                      BoardRect boardRect = {});

  /// Record one frame into a command buffer the caller owns and submits. Nothing is
  /// waited on, so the caller can keep `kFramesInFlight` frames overlapping; `frame`
  /// selects the per-frame instance buffer and blur descriptors, and must be
  /// `0 .. kFramesInFlight-1`. The colour image is left in COLOR_ATTACHMENT_OPTIMAL for
  /// the caller's own final transition. `render` above is the synchronous wrapper used by
  /// captures and tests, which leaves the image in TRANSFER_SRC_OPTIMAL.
  Result<void> record(VkCommandBuffer cmd, const OffscreenTarget& target,
                      const InstanceSet& instances, const view::OrbitCamera& camera,
                      const std::function<void(VkCommandBuffer)>& overlay,
                      BoardRect boardRect, std::uint32_t frame);

  Result<Image> renderToImage(const OffscreenTarget& target, const view::PositionView& p,
                              const view::ViewConfig& cfg,
                              const view::OrbitCamera& camera);

  [[nodiscard]] static view::Vec3 cellHalfExtent();

 private:
  Result<void> buildPipeline();
  Result<void> buildBackdropPipeline();
  Result<void> buildBlurPipeline();
  /// Point the frame's two descriptor sets at this target's images. Rewritten only when
  /// the target's colour view changes; the frame slot's fence is what makes that safe
  /// while the previous frame of the same slot may still have been reading them.
  Result<void> bindBlurTarget(const OffscreenTarget& target, std::uint32_t frame);
  Result<void> uploadGeometry();
  Result<void> ensureInstanceCapacity(std::uint32_t frame, std::size_t count);
  Result<void> ensureSurfaceCapacity(std::uint32_t frame, std::size_t vertices,
                                     std::size_t indices);

  const VulkanContext* ctx_{nullptr};
  VkFormat colorFormat_{OffscreenTarget::kColorFormat};
  view::Theme theme_{view::Theme::manifold()};
  BoardOptions options_{};
  MeshLibrary meshes_;
  CellId lastFrom_{kInvalidCell};
  CellId lastTo_{kInvalidCell};
  CellId checkCell_{kInvalidCell};

  VkShaderModule vert_{VK_NULL_HANDLE};
  VkShaderModule frag_{VK_NULL_HANDLE};
  VkPipelineLayout layout_{VK_NULL_HANDLE};
  VkPipeline pipeline_{VK_NULL_HANDLE};
  /// The same shaders and vertex input as `pipeline_`, with alpha blending and no depth
  /// writes - the geometry view's ghost board (M17.10). The opaque path is unchanged.
  VkPipeline surfaceBlendPipeline_{VK_NULL_HANDLE};

  // The lit ground the board stands on, drawn first across the whole frame.
  VkShaderModule backdropVert_{VK_NULL_HANDLE};
  VkShaderModule backdropFrag_{VK_NULL_HANDLE};
  VkPipelineLayout backdropLayout_{VK_NULL_HANDLE};
  VkPipeline backdropPipeline_{VK_NULL_HANDLE};

  // The defocus pass. One pipeline, one sampler, and two descriptor sets that ping-pong
  // between the target's two colour images.
  VkShaderModule blurVert_{VK_NULL_HANDLE};
  VkShaderModule blurFrag_{VK_NULL_HANDLE};
  VkPipelineLayout blurLayout_{VK_NULL_HANDLE};
  VkPipeline blurPipeline_{VK_NULL_HANDLE};
  VkDescriptorSetLayout blurSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool blurPool_{VK_NULL_HANDLE};
  VkSampler blurSampler_{VK_NULL_HANDLE};
  /// Two sets per frame in flight - one sampling the colour image, one the scratch -
  /// because a frame in flight must not have its descriptors rewritten underneath it.
  std::array<VkDescriptorSet, kFramesInFlight> blurFromColor_{};
  std::array<VkDescriptorSet, kFramesInFlight> blurFromScratch_{};
  std::array<VkImageView, kFramesInFlight> blurBoundColor_{};
  float blur_{0.0f};

  VkBuffer vertexBuffer_{VK_NULL_HANDLE};
  VkDeviceMemory vertexMem_{VK_NULL_HANDLE};
  VkBuffer indexBuffer_{VK_NULL_HANDLE};
  VkDeviceMemory indexMem_{VK_NULL_HANDLE};

  /// The instance buffer is per frame in flight: frame N+1 is uploaded while frame N's
  /// draws may still be reading it.
  std::array<VkBuffer, kFramesInFlight> instanceBuffer_{};
  std::array<VkDeviceMemory, kFramesInFlight> instanceMem_{};
  std::array<std::size_t, kFramesInFlight> instanceCapacity_{};

  /// And so is the geometry view's board mesh, for the same reason: it is rebuilt every
  /// frame, because the shape can be slid or turned every frame.
  std::array<VkBuffer, kFramesInFlight> surfaceVertexBuffer_{};
  std::array<VkDeviceMemory, kFramesInFlight> surfaceVertexMem_{};
  std::array<std::size_t, kFramesInFlight> surfaceVertexCapacity_{};
  std::array<VkBuffer, kFramesInFlight> surfaceIndexBuffer_{};
  std::array<VkDeviceMemory, kFramesInFlight> surfaceIndexMem_{};
  std::array<std::size_t, kFramesInFlight> surfaceIndexCapacity_{};
};

}  // namespace cb::render
