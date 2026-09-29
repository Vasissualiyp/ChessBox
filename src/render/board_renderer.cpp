// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/board_renderer.hpp"

#include <algorithm>
#include <cstring>

#include "shaders.hpp"

namespace cb::render {
namespace {

struct PushConstants {
  view::Mat4 viewProj{};
  float lightDir[4]{-0.42f, -0.55f, -0.72f, 0.0f};
};
static_assert(sizeof(PushConstants) == 80);

struct BackdropPush {
  float inner[4]{};
  float outer[4]{};
  float params[4]{0.5f, 0.34f, 1.25f, 0.62f};
};
static_assert(sizeof(BackdropPush) == 48);

void toFloat4(const view::Rgba& c, float out[4]) {
  out[0] = c.r;
  out[1] = c.g;
  out[2] = c.b;
  out[3] = c.a;
}

view::Rgba mix(const view::Rgba& a, const view::Rgba& b, float t) {
  return view::Rgba{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t,
                    a.a + (b.a - a.a) * t};
}

Result<void> makeBuffer(const VulkanContext& ctx, VkDeviceSize size,
                        VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
                        VkBuffer& buffer, VkDeviceMemory& memory) {
  VkBufferCreateInfo bci{};
  bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bci.size = size;
  bci.usage = usage;
  if (const VkResult r = vkCreateBuffer(ctx.device(), &bci, nullptr, &buffer);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create buffer: " + describe(r));
  }
  VkMemoryRequirements req{};
  vkGetBufferMemoryRequirements(ctx.device(), buffer, &req);
  const auto type = ctx.findMemoryType(req.memoryTypeBits, props);
  if (!type.has_value()) return fail(type.error().code, type.error().message);
  VkMemoryAllocateInfo mai{};
  mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  mai.allocationSize = req.size;
  mai.memoryTypeIndex = *type;
  if (const VkResult r = vkAllocateMemory(ctx.device(), &mai, nullptr, &memory);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot allocate buffer memory: " + describe(r));
  }
  vkBindBufferMemory(ctx.device(), buffer, memory, 0);
  return {};
}

Result<VkShaderModule> makeShader(const VulkanContext& ctx,
                                  std::span<const std::uint8_t> spv) {
  VkShaderModuleCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  sci.codeSize = spv.size();
  sci.pCode = reinterpret_cast<const std::uint32_t*>(spv.data());
  VkShaderModule m = VK_NULL_HANDLE;
  if (const VkResult r = vkCreateShaderModule(ctx.device(), &sci, nullptr, &m);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create shader module: " + describe(r));
  }
  return m;
}

}  // namespace

view::Vec3 BoardRenderer::cellHalfExtent() {
  return view::Vec3{0.46f, 0.46f, 0.055f};
}

Result<BoardRenderer> BoardRenderer::create(const VulkanContext& ctx) {
  BoardRenderer r;
  r.ctx_ = &ctx;
  r.meshes_ = MeshLibrary::build();
  if (auto ok = r.buildPipeline(); !ok.has_value())
    return fail(ok.error().code, ok.error().message);
  if (auto ok = r.uploadGeometry(); !ok.has_value())
    return fail(ok.error().code, ok.error().message);
  return r;
}

Result<void> BoardRenderer::buildPipeline() {
  const auto vs = makeShader(*ctx_, spv::board_vert_spv_span());
  if (!vs.has_value()) return fail(vs.error().code, vs.error().message);
  vert_ = *vs;
  const auto fs = makeShader(*ctx_, spv::board_frag_spv_span());
  if (!fs.has_value()) return fail(fs.error().code, fs.error().message);
  frag_ = *fs;

  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  push.size = sizeof(PushConstants);
  VkPipelineLayoutCreateInfo plci{};
  plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &push;
  if (const VkResult r = vkCreatePipelineLayout(ctx_->device(), &plci, nullptr, &layout_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create pipeline layout: " + describe(r));
  }

  const VkVertexInputBindingDescription bindings[2]{
      {0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE}};
  const VkVertexInputAttributeDescription attrs[8]{
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, pos)},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal)},
      {2, 0, VK_FORMAT_R32_SFLOAT, offsetof(MeshVertex, height)},
      {3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, center)},
      {4, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, scale)},
      {5, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, color)},
      {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, edge)},
      {7, 1, VK_FORMAT_R32_SFLOAT, offsetof(Instance, edgeMask)}};

  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi.vertexBindingDescriptionCount = 2;
  vi.pVertexBindingDescriptions = bindings;
  vi.vertexAttributeDescriptionCount = 8;
  vi.pVertexAttributeDescriptions = attrs;

  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo vp{};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  // The generated meshes are not consistently wound, and a piece with a missing face is
  // a far worse bug than the handful of triangles culling would have saved.
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  ds.depthTestEnable = VK_TRUE;
  ds.depthWriteEnable = VK_TRUE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS;

  VkPipelineColorBlendAttachmentState blend{};
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = 1;
  cb.pAttachments = &blend;

  const VkDynamicState dynamics[2]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dy{};
  dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dy.dynamicStateCount = 2;
  dy.pDynamicStates = dynamics;

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert_;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag_;
  stages[1].pName = "main";

  const VkFormat colorFormat = OffscreenTarget::kColorFormat;
  VkPipelineRenderingCreateInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &colorFormat;
  rendering.depthAttachmentFormat = OffscreenTarget::kDepthFormat;

  VkGraphicsPipelineCreateInfo gpi{};
  gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gpi.pNext = &rendering;
  gpi.stageCount = 2;
  gpi.pStages = stages;
  gpi.pVertexInputState = &vi;
  gpi.pInputAssemblyState = &ia;
  gpi.pViewportState = &vp;
  gpi.pRasterizationState = &rs;
  gpi.pMultisampleState = &ms;
  gpi.pDepthStencilState = &ds;
  gpi.pColorBlendState = &cb;
  gpi.pDynamicState = &dy;
  gpi.layout = layout_;
  if (const VkResult r = vkCreateGraphicsPipelines(ctx_->device(), VK_NULL_HANDLE, 1,
                                                   &gpi, nullptr, &pipeline_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal,
                "cannot create the graphics pipeline: " + describe(r));
  }
  return {};
}

Result<void> BoardRenderer::buildBackdropPipeline() {
  const auto vs = makeShader(*ctx_, spv::backdrop_vert_spv_span());
  if (!vs.has_value()) return fail(vs.error().code, vs.error().message);
  backdropVert_ = *vs;
  const auto fs = makeShader(*ctx_, spv::backdrop_frag_spv_span());
  if (!fs.has_value()) return fail(fs.error().code, fs.error().message);
  backdropFrag_ = *fs;

  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  push.size = sizeof(BackdropPush);
  VkPipelineLayoutCreateInfo plci{};
  plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  plci.pushConstantRangeCount = 1;
  plci.pPushConstantRanges = &push;
  if (const VkResult r =
          vkCreatePipelineLayout(ctx_->device(), &plci, nullptr, &backdropLayout_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "cannot create the backdrop layout: " + describe(r));
  }

  // No vertex input at all: the shader derives a full-screen triangle from the index.
  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineInputAssemblyStateCreateInfo ia{};
  ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo vp{};
  vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  vp.viewportCount = 1;
  vp.scissorCount = 1;
  VkPipelineRasterizationStateCreateInfo rs{};
  rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rs.polygonMode = VK_POLYGON_MODE_FILL;
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo ms{};
  ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  // It is behind everything, so it neither tests nor writes depth.
  VkPipelineDepthStencilStateCreateInfo ds{};
  ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  VkPipelineColorBlendAttachmentState blend{};
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb{};
  cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  cb.attachmentCount = 1;
  cb.pAttachments = &blend;
  const VkDynamicState dynamics[2]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dy{};
  dy.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dy.dynamicStateCount = 2;
  dy.pDynamicStates = dynamics;

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = backdropVert_;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = backdropFrag_;
  stages[1].pName = "main";

  const VkFormat colorFormat = OffscreenTarget::kColorFormat;
  VkPipelineRenderingCreateInfo rendering{};
  rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  rendering.colorAttachmentCount = 1;
  rendering.pColorAttachmentFormats = &colorFormat;
  rendering.depthAttachmentFormat = OffscreenTarget::kDepthFormat;

  VkGraphicsPipelineCreateInfo gpi{};
  gpi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gpi.pNext = &rendering;
  gpi.stageCount = 2;
  gpi.pStages = stages;
  gpi.pVertexInputState = &vi;
  gpi.pInputAssemblyState = &ia;
  gpi.pViewportState = &vp;
  gpi.pRasterizationState = &rs;
  gpi.pMultisampleState = &ms;
  gpi.pDepthStencilState = &ds;
  gpi.pColorBlendState = &cb;
  gpi.pDynamicState = &dy;
  gpi.layout = backdropLayout_;
  if (const VkResult r = vkCreateGraphicsPipelines(ctx_->device(), VK_NULL_HANDLE, 1,
                                                   &gpi, nullptr, &backdropPipeline_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal,
                "cannot create the backdrop pipeline: " + describe(r));
  }
  return {};
}

Result<void> BoardRenderer::uploadGeometry() {
  const VkDeviceSize vbytes = meshes_.vertices.size() * sizeof(MeshVertex);
  const VkDeviceSize ibytes = meshes_.indices.size() * sizeof(std::uint16_t);
  const auto hostProps =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  if (auto r = makeBuffer(*ctx_, vbytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, hostProps,
                          vertexBuffer_, vertexMem_);
      !r.has_value()) {
    return r;
  }
  if (auto r = makeBuffer(*ctx_, ibytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, hostProps,
                          indexBuffer_, indexMem_);
      !r.has_value()) {
    return r;
  }

  void* mapped = nullptr;
  vkMapMemory(ctx_->device(), vertexMem_, 0, vbytes, 0, &mapped);
  std::memcpy(mapped, meshes_.vertices.data(), vbytes);
  vkUnmapMemory(ctx_->device(), vertexMem_);
  vkMapMemory(ctx_->device(), indexMem_, 0, ibytes, 0, &mapped);
  std::memcpy(mapped, meshes_.indices.data(), ibytes);
  vkUnmapMemory(ctx_->device(), indexMem_);
  return {};
}

Result<void> BoardRenderer::ensureInstanceCapacity(std::size_t count) {
  if (count <= instanceCapacity_) return {};
  if (instanceBuffer_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(ctx_->device());
    vkDestroyBuffer(ctx_->device(), instanceBuffer_, nullptr);
    vkFreeMemory(ctx_->device(), instanceMem_, nullptr);
    instanceBuffer_ = VK_NULL_HANDLE;
    instanceMem_ = VK_NULL_HANDLE;
  }
  const std::size_t capacity = std::max<std::size_t>(count * 2, 4096);
  if (auto r = makeBuffer(
          *ctx_, capacity * sizeof(Instance), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
          instanceBuffer_, instanceMem_);
      !r.has_value()) {
    return r;
  }
  instanceCapacity_ = capacity;
  return {};
}

InstanceSet BoardRenderer::buildInstances(const view::PositionView& p,
                                          const view::ViewConfig& cfg,
                                          const view::SeamMap* seams,
                                          const view::MoveAnimation* anim) const {
  const VariantSpec& v = p.variant();
  const auto placements = view::layout(v.dims, cfg);

  // Appearance per piece type, resolved once: a variant may declare a shape and a
  // height, and anything it leaves out is derived from how the piece moves.
  std::vector<Archetype> shape(v.pieces.size(), Archetype::Tower);
  std::vector<float> height(v.pieces.size(), 1.0f);
  for (std::size_t i = 1; i < v.pieces.size(); ++i) {
    shape[i] = archetypeFor(v.pieces[i]);
    height[i] = heightFor(v.pieces[i]);
  }

  const auto highlighted = p.highlighted();
  const auto isHighlighted = [&](CellId c) {
    return std::find(highlighted.begin(), highlighted.end(), c) != highlighted.end();
  };

  // Two passes so instances of the same shape are contiguous: one draw call per shape.
  std::vector<std::vector<Instance>> byShape(static_cast<std::size_t>(Archetype::Count));
  const view::Vec3 half = cellHalfExtent();

  // One piece, wherever it happens to be this frame. A travelling piece is the same
  // call with a different position, which is what keeps the animation from being a
  // second, subtly different way of drawing a piece.
  const auto emitPiece = [&](Piece piece, float x, float y, float z, bool inCheck) {
    view::Rgba pieceColor =
        piece.colorOf() == Color::White ? theme_.whitePiece : theme_.blackPiece;
    if (options_.showCheck && inCheck) pieceColor = mix(pieceColor, theme_.blood, 0.65f);

    if (options_.flat) {
      // Seen from straight above, a model is a blob, so the flat board's pieces are
      // drawn entirely by the interface: a circle token with the piece's own icon, in
      // screen space, which is also the only layer that can draw a circle and follow the
      // move animation with it. The renderer contributes the board, not the pieces.
      return;
    }

    Instance body{};
    body.center[0] = x;
    body.center[1] = y;
    body.center[2] = z + half.z;
    // The archetype already gives the piece its own height; this blends in the extra
    // scaling that encodes value. At 0 every piece stands at its natural size - which is
    // still different per shape - and nothing ever ends up shorter than its own model.
    const float h = 1.0f + (height[piece.type] - 1.0f) * options_.pieceHeightScale;
    body.scale[0] = 0.8f;
    body.scale[1] = 0.8f;
    body.scale[2] = 0.8f * h;
    toFloat4(pieceColor, body.color);
    byShape[static_cast<std::size_t>(shape[piece.type])].push_back(body);
  };

  for (const view::Placement& pl : placements) {
    const Coord c = v.dims.toCoord(pl.cell);
    int parity = 0;
    for (std::uint8_t a : cfg.screenAxes) parity += c.c[a];
    for (std::uint8_t a : cfg.gridAxes) parity += c.c[a];

    Instance cell{};
    cell.center[0] = pl.x;
    cell.center[1] = pl.y;
    cell.center[2] = pl.z;
    cell.scale[0] = 1.0f;
    cell.scale[1] = 1.0f;
    cell.scale[2] = 1.0f;

    view::Rgba fill = (parity % 2 == 0) ? theme_.boardDark : theme_.boardLight;
    if (pl.cell == p.selected()) {
      fill = theme_.ember;
    } else if (options_.showLegalMoves && isHighlighted(pl.cell)) {
      // A legal destination that would take something is marked differently from an
      // empty one, so a capture never comes as a surprise.
      fill = p.at(pl.cell).empty() ? mix(fill, theme_.moss, 0.72f)
                                   : mix(fill, theme_.blood, 0.62f);
    } else if (options_.showLastMove && (pl.cell == lastFrom_ || pl.cell == lastTo_)) {
      fill = mix(fill, theme_.ember, 0.22f);
    }
    toFloat4(fill, cell.color);

    byShape[static_cast<std::size_t>(Archetype::Cell)].push_back(cell);

    // A seam is drawn as a coloured rail along the face, one instance per face, so the
    // two ends of one identification can carry the same colour. A single tint per cell
    // could not: a corner cell is on two different portals at once.
    if (options_.showSeams && seams != nullptr) {
      for (const view::SeamFace& f : seams->at(pl.cell)) {
        const float dir = f.side == Side::Min ? -1.0f : 1.0f;
        Instance rail{};
        rail.center[0] = pl.x + (f.screenAxis == 0 ? dir * 0.5f : 0.0f);
        rail.center[1] = pl.y + (f.screenAxis == 1 ? dir * 0.5f : 0.0f);
        rail.center[2] = pl.z + half.z * 0.6f;
        rail.scale[0] = (f.screenAxis == 0 ? 0.07f : 0.5f) / half.x;
        rail.scale[1] = (f.screenAxis == 1 ? 0.07f : 0.5f) / half.y;
        rail.scale[2] = 0.055f / half.z;
        toFloat4(f.color, rail.color);
        byShape[static_cast<std::size_t>(Archetype::Cell)].push_back(rail);
      }
    }

    const Piece piece = p.at(pl.cell);
    if (piece.empty()) continue;
    // The travelling piece is drawn where it currently is, not where it has already
    // arrived; skipping it here is what stops it being drawn twice.
    if (anim != nullptr && anim->active() && pl.cell == anim->travellingTo()) continue;

    emitPiece(piece, pl.x, pl.y, pl.z, pl.cell == checkCell_);
  }

  // A plinth under each sub-board, so the cells sit on something instead of floating in
  // the dark. One per slice, because a grid of sub-boards should read as separate boards.
  {
    struct Extent {
      float minX{0}, maxX{0}, minY{0}, maxY{0}, z{0};
      bool seen{false};
    };
    std::vector<Extent> extents(view::enumerateSlices(v.dims, cfg).size());
    for (const view::Placement& pl : placements) {
      Extent& e = extents[pl.slice];
      if (!e.seen) {
        e = Extent{pl.x, pl.x, pl.y, pl.y, pl.z, true};
        continue;
      }
      e.minX = std::min(e.minX, pl.x);
      e.maxX = std::max(e.maxX, pl.x);
      e.minY = std::min(e.minY, pl.y);
      e.maxY = std::max(e.maxY, pl.y);
      e.z = std::min(e.z, pl.z);
    }
    // Warm, dark, and clearly not the ground: a table the board is standing on.
    const view::Rgba wood = mix(theme_.ink, theme_.rule, 0.85f);
    for (const Extent& e : extents) {
      if (!e.seen) continue;
      Instance plinth{};
      plinth.center[0] = (e.minX + e.maxX) * 0.5f;
      plinth.center[1] = (e.minY + e.maxY) * 0.5f;
      plinth.center[2] = e.z - half.z - 0.13f;
      plinth.scale[0] = ((e.maxX - e.minX) * 0.5f + 0.78f) / half.x;
      plinth.scale[1] = ((e.maxY - e.minY) * 0.5f + 0.78f) / half.y;
      plinth.scale[2] = 0.13f / half.z;
      toFloat4(wood, plinth.color);
      // A warm rim around the edge of the table, which is what makes it read as an
      // object rather than as a darker rectangle.
      toFloat4(mix(theme_.emberDeep, theme_.rule, 0.45f), plinth.edge);
      plinth.edgeMask = 15.0f;
      byShape[static_cast<std::size_t>(Archetype::Cell)].push_back(plinth);
    }
  }

  // The travelling piece and any portal it is passing through. Drawn last so they sit
  // above the board they are crossing.
  if (anim != nullptr && anim->active()) {
    const view::MoveAnimation::Sample at = anim->sample();
    const Piece moving = p.at(anim->travellingTo());
    if (at.moving && !moving.empty()) {
      emitPiece(moving, at.x, at.y, at.z + at.lift, false);
    }
    for (const view::MoveAnimation::Portal& portal : anim->openPortals()) {
      // A doorway standing across the seam the piece crossed: thin on the axis the
      // portal faces, wide along the seam, and growing as it opens. It uses the cube
      // archetype, not the flat cell slab - a slab lies on the board however it is
      // scaled, which is why a portal on a vertical file seam used to come out flat.
      const float open = portal.intensity;
      const float ax = std::abs(portal.nx);
      const float ay = std::abs(portal.ny);
      const float az = std::abs(portal.nz);
      const float wide = 0.46f;
      float ex = wide;
      float ey = wide;
      float ez = 0.06f + 0.55f * open;
      if (ax >= ay && ax >= az) {
        ex = 0.05f;
      } else if (ay >= az) {
        ey = 0.05f;
      } else {
        ez = 0.05f;
      }
      Instance iris{};
      iris.center[0] = portal.x;
      iris.center[1] = portal.y;
      iris.center[2] = portal.z + ez;  // stand it on the cell rather than through it
      // The cube's own half-extent is 0.5, so the scale is the wanted half-extent
      // doubled.
      iris.scale[0] = ex * 2.0f;
      iris.scale[1] = ey * 2.0f;
      iris.scale[2] = ez * 2.0f;
      toFloat4(portal.color, iris.color);
      byShape[static_cast<std::size_t>(Archetype::Portal)].push_back(iris);
    }
  }

  InstanceSet out;
  std::size_t total = 0;
  for (const auto& group : byShape) total += group.size();
  out.instances.reserve(total);
  for (std::size_t s = 0; s < byShape.size(); ++s) {
    out.batches[s].first = static_cast<std::uint32_t>(out.instances.size());
    out.batches[s].count = static_cast<std::uint32_t>(byShape[s].size());
    out.instances.insert(out.instances.end(), byShape[s].begin(), byShape[s].end());
  }
  return out;
}

Result<void> BoardRenderer::render(const OffscreenTarget& target, const InstanceSet& set,
                                   const view::OrbitCamera& camera,
                                   const std::function<void(VkCommandBuffer)>& overlay,
                                   BoardRect boardRect) {
  if (auto r = ensureInstanceCapacity(set.instances.size()); !r.has_value()) return r;
  if (!set.instances.empty()) {
    void* mapped = nullptr;
    vkMapMemory(ctx_->device(), instanceMem_, 0, set.instances.size() * sizeof(Instance),
                0, &mapped);
    std::memcpy(mapped, set.instances.data(), set.instances.size() * sizeof(Instance));
    vkUnmapMemory(ctx_->device(), instanceMem_);
  }

  const BoardRect board = boardRect.valid()
                              ? boardRect
                              : BoardRect{0, 0, static_cast<float>(target.width()),
                                          static_cast<float>(target.height())};
  PushConstants push{};
  push.viewProj = camera.viewProj(board.width / board.height);

  return ctx_->submitAndWait([&](VkCommandBuffer cmd) {
    const auto barrier = [&](VkImage image, VkImageAspectFlags aspectMask,
                             VkImageLayout from, VkImageLayout to,
                             VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                             VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
      VkImageMemoryBarrier2 b{};
      b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
      b.srcStageMask = srcStage;
      b.srcAccessMask = srcAccess;
      b.dstStageMask = dstStage;
      b.dstAccessMask = dstAccess;
      b.oldLayout = from;
      b.newLayout = to;
      b.image = image;
      b.subresourceRange = {aspectMask, 0, 1, 0, 1};
      VkDependencyInfo dep{};
      dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
      dep.imageMemoryBarrierCount = 1;
      dep.pImageMemoryBarriers = &b;
      vkCmdPipelineBarrier2(cmd, &dep);
    };

    barrier(target.colorImage(), VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    barrier(target.depthImage(), VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED,
            VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
            0, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
            VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color{};
    color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    color.imageView = target.colorView();
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{theme_.ink.r, theme_.ink.g, theme_.ink.b, 1.0f}};

    VkRenderingAttachmentInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    depth.imageView = target.depthView();
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.clearValue.depthStencil = {1.0f, 0};

    VkRenderingInfo ri{};
    ri.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    ri.renderArea = {{0, 0}, {target.width(), target.height()}};
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &color;
    ri.pDepthAttachment = &depth;

    vkCmdBeginRendering(cmd, &ri);
    // The board gets its own viewport - the area the interface leaves free - while the
    // overlay is drawn across the whole frame.
    VkViewport boardViewport{board.x, board.y, board.width, board.height, 0.0f, 1.0f};
    VkRect2D boardScissor{
        {static_cast<std::int32_t>(board.x), static_cast<std::int32_t>(board.y)},
        {static_cast<std::uint32_t>(board.width),
         static_cast<std::uint32_t>(board.height)}};
    vkCmdSetViewport(cmd, 0, 1, &boardViewport);
    vkCmdSetScissor(cmd, 0, 1, &boardScissor);

    if (!set.instances.empty()) {
      vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
      vkCmdPushConstants(cmd, layout_,
                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                         sizeof(PushConstants), &push);
      const VkDeviceSize zero = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &zero);
      vkCmdBindVertexBuffers(cmd, 1, 1, &instanceBuffer_, &zero);
      vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT16);
      // One draw per shape, not per piece: eight calls for any board of any size.
      for (std::size_t s = 0; s < set.batches.size(); ++s) {
        const auto& batch = set.batches[s];
        if (batch.count == 0) continue;
        const MeshRange& range = meshes_.ranges[s];
        vkCmdDrawIndexed(cmd, range.indexCount, batch.count, range.firstIndex,
                         range.vertexOffset, batch.first);
      }
    }
    if (overlay) {
      VkViewport full{0,
                      0,
                      static_cast<float>(target.width()),
                      static_cast<float>(target.height()),
                      0.0f,
                      1.0f};
      VkRect2D fullScissor{{0, 0}, {target.width(), target.height()}};
      vkCmdSetViewport(cmd, 0, 1, &full);
      vkCmdSetScissor(cmd, 0, 1, &fullScissor);
      overlay(cmd);
    }
    vkCmdEndRendering(cmd);

    barrier(target.colorImage(), VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            VK_ACCESS_2_TRANSFER_READ_BIT);
  });
}

Result<Image> BoardRenderer::renderToImage(const OffscreenTarget& target,
                                           const view::PositionView& p,
                                           const view::ViewConfig& cfg,
                                           const view::OrbitCamera& camera) {
  const InstanceSet set = buildInstances(p, cfg);
  if (auto r = render(target, set, camera); !r.has_value()) {
    return fail(r.error().code, r.error().message);
  }
  auto pixels = target.readPixels();
  if (!pixels.has_value()) return fail(pixels.error().code, pixels.error().message);
  Image img;
  img.width = target.width();
  img.height = target.height();
  img.rgba = std::move(*pixels);
  return img;
}

BoardRenderer& BoardRenderer::operator=(BoardRenderer&& o) noexcept {
  if (this == &o) return *this;
  std::swap(ctx_, o.ctx_);
  std::swap(theme_, o.theme_);
  std::swap(meshes_, o.meshes_);
  std::swap(lastFrom_, o.lastFrom_);
  std::swap(lastTo_, o.lastTo_);
  std::swap(checkCell_, o.checkCell_);
  std::swap(vert_, o.vert_);
  std::swap(frag_, o.frag_);
  std::swap(layout_, o.layout_);
  std::swap(pipeline_, o.pipeline_);
  std::swap(backdropVert_, o.backdropVert_);
  std::swap(backdropFrag_, o.backdropFrag_);
  std::swap(backdropLayout_, o.backdropLayout_);
  std::swap(backdropPipeline_, o.backdropPipeline_);
  std::swap(vertexBuffer_, o.vertexBuffer_);
  std::swap(vertexMem_, o.vertexMem_);
  std::swap(indexBuffer_, o.indexBuffer_);
  std::swap(indexMem_, o.indexMem_);
  std::swap(instanceBuffer_, o.instanceBuffer_);
  std::swap(instanceMem_, o.instanceMem_);
  std::swap(instanceCapacity_, o.instanceCapacity_);
  return *this;
}

BoardRenderer::~BoardRenderer() {
  if (ctx_ == nullptr || ctx_->device() == VK_NULL_HANDLE) return;
  const VkDevice d = ctx_->device();
  if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(d, pipeline_, nullptr);
  if (layout_ != VK_NULL_HANDLE) vkDestroyPipelineLayout(d, layout_, nullptr);
  if (backdropPipeline_ != VK_NULL_HANDLE)
    vkDestroyPipeline(d, backdropPipeline_, nullptr);
  if (backdropLayout_ != VK_NULL_HANDLE)
    vkDestroyPipelineLayout(d, backdropLayout_, nullptr);
  if (backdropVert_ != VK_NULL_HANDLE) vkDestroyShaderModule(d, backdropVert_, nullptr);
  if (backdropFrag_ != VK_NULL_HANDLE) vkDestroyShaderModule(d, backdropFrag_, nullptr);
  if (vert_ != VK_NULL_HANDLE) vkDestroyShaderModule(d, vert_, nullptr);
  if (frag_ != VK_NULL_HANDLE) vkDestroyShaderModule(d, frag_, nullptr);
  for (auto [buf, mem] :
       {std::pair{vertexBuffer_, vertexMem_}, std::pair{indexBuffer_, indexMem_},
        std::pair{instanceBuffer_, instanceMem_}}) {
    if (buf != VK_NULL_HANDLE) vkDestroyBuffer(d, buf, nullptr);
    if (mem != VK_NULL_HANDLE) vkFreeMemory(d, mem, nullptr);
  }
}

}  // namespace cb::render
