// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/board_renderer.hpp"

#include <cstring>

#include "shaders.hpp"

namespace cb::render {
namespace {

struct Vertex {
  float pos[3];
  float normal[3];
};

/// A unit cube with flat normals: 24 vertices so each face has its own, which is what
/// makes the single directional term read as distinct faces rather than a blob.
const std::vector<Vertex>& unitCube() {
  static const std::vector<Vertex> verts = [] {
    std::vector<Vertex> v;
    const float h = 0.5f;
    const int faces[6][3] = {{0, 0, 1},  {0, 0, -1}, {1, 0, 0},
                             {-1, 0, 0}, {0, 1, 0},  {0, -1, 0}};
    for (const auto& n : faces) {
      // Two in-plane axes for this face.
      float a[3]{0, 0, 0};
      float b[3]{0, 0, 0};
      if (n[0] != 0) {
        a[1] = 1;
        b[2] = 1;
      } else if (n[1] != 0) {
        a[0] = 1;
        b[2] = 1;
      } else {
        a[0] = 1;
        b[1] = 1;
      }
      const float signs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
      for (const auto& s : signs) {
        Vertex vert{};
        for (int k = 0; k < 3; ++k) {
          vert.pos[k] = static_cast<float>(n[k]) * h + a[k] * s[0] * h + b[k] * s[1] * h;
          vert.normal[k] = static_cast<float>(n[k]);
        }
        v.push_back(vert);
      }
    }
    return v;
  }();
  return verts;
}

const std::vector<std::uint16_t>& cubeIndices() {
  static const std::vector<std::uint16_t> idx = [] {
    std::vector<std::uint16_t> out;
    for (std::uint16_t f = 0; f < 6; ++f) {
      const std::uint16_t base = static_cast<std::uint16_t>(f * 4);
      static constexpr std::uint16_t kQuad[6]{0, 1, 2, 0, 2, 3};
      for (std::uint16_t i : kQuad) out.push_back(static_cast<std::uint16_t>(base + i));
    }
    return out;
  }();
  return idx;
}

struct PushConstants {
  view::Mat4 viewProj{};
  float lightDir[3]{-0.4f, -0.5f, -0.75f};
  float pad{0};
};
static_assert(sizeof(PushConstants) == 80);

void toFloat4(const Image::Rgba& c, float out[4]) {
  out[0] = static_cast<float>(c.r) / 255.0f;
  out[1] = static_cast<float>(c.g) / 255.0f;
  out[2] = static_cast<float>(c.b) / 255.0f;
  out[3] = static_cast<float>(c.a) / 255.0f;
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
  return view::Vec3{0.45f, 0.45f, 0.06f};
}

Result<BoardRenderer> BoardRenderer::create(const VulkanContext& ctx) {
  BoardRenderer r;
  r.ctx_ = &ctx;
  if (auto ok = r.buildPipeline(); !ok.has_value()) {
    return fail(ok.error().code, ok.error().message);
  }
  if (auto ok = r.uploadGeometry(); !ok.has_value()) {
    return fail(ok.error().code, ok.error().message);
  }
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
      {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX},
      {1, sizeof(Instance), VK_VERTEX_INPUT_RATE_INSTANCE}};
  const VkVertexInputAttributeDescription attrs[5]{
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)},
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
      {2, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, center)},
      {3, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Instance, scale)},
      {4, 1, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Instance, color)}};

  VkPipelineVertexInputStateCreateInfo vi{};
  vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vi.vertexBindingDescriptionCount = 2;
  vi.pVertexBindingDescriptions = bindings;
  vi.vertexAttributeDescriptionCount = 5;
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
  rs.cullMode = VK_CULL_MODE_BACK_BIT;
  // The projection flips Y for Vulkan's clip space, which flips winding with it.
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

  // Dynamic rendering: attachment formats are declared here, and there is no
  // VkRenderPass or VkFramebuffer object to keep in step with the target.
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

Result<void> BoardRenderer::uploadGeometry() {
  const auto& verts = unitCube();
  const auto& idx = cubeIndices();
  indexCount_ = static_cast<std::uint32_t>(idx.size());

  const VkDeviceSize vbytes = verts.size() * sizeof(Vertex);
  const VkDeviceSize ibytes = idx.size() * sizeof(std::uint16_t);
  const auto hostProps =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

  // Host-visible rather than staged into device-local: it is one cube, written once, and
  // a staging copy would be more code than the data.
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
  std::memcpy(mapped, verts.data(), vbytes);
  vkUnmapMemory(ctx_->device(), vertexMem_);
  vkMapMemory(ctx_->device(), indexMem_, 0, ibytes, 0, &mapped);
  std::memcpy(mapped, idx.data(), ibytes);
  vkUnmapMemory(ctx_->device(), indexMem_);
  return {};
}

Result<void> BoardRenderer::ensureInstanceCapacity(std::size_t count) {
  if (count <= instanceCapacity_) return {};
  if (instanceBuffer_ != VK_NULL_HANDLE) {
    vkDestroyBuffer(ctx_->device(), instanceBuffer_, nullptr);
    vkFreeMemory(ctx_->device(), instanceMem_, nullptr);
    instanceBuffer_ = VK_NULL_HANDLE;
    instanceMem_ = VK_NULL_HANDLE;
  }
  // Grow generously so a resize is rare rather than per frame.
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

std::vector<Instance> BoardRenderer::buildInstances(const view::PositionView& p,
                                                    const view::ViewConfig& cfg) const {
  const VariantSpec& v = p.variant();
  const auto placements = view::layout(v.dims, cfg);

  std::vector<Instance> out;
  out.reserve(placements.size() * 2);

  const auto highlighted = p.highlighted();
  const auto isHighlighted = [&](CellId c) {
    return std::find(highlighted.begin(), highlighted.end(), c) != highlighted.end();
  };

  for (const view::Placement& pl : placements) {
    // Checkerboard from the parity of the screen-axis coordinates, so the pattern stays
    // meaningful in any number of dimensions and in any projection.
    const Coord c = v.dims.toCoord(pl.cell);
    int parity = 0;
    for (std::uint8_t a : cfg.screenAxes) parity += c.c[a];
    for (std::uint8_t a : cfg.gridAxes) parity += c.c[a];

    Instance cell{};
    cell.center[0] = pl.x;
    cell.center[1] = pl.y;
    cell.center[2] = pl.z;
    const view::Vec3 half = cellHalfExtent();
    cell.scale[0] = half.x * 2.0f;
    cell.scale[1] = half.y * 2.0f;
    cell.scale[2] = half.z * 2.0f;
    if (pl.cell == p.selected()) {
      toFloat4(theme_.selected, cell.color);
    } else if (isHighlighted(pl.cell)) {
      toFloat4(theme_.legalTarget, cell.color);
    } else {
      toFloat4(parity % 2 == 0 ? theme_.darkCell : theme_.lightCell, cell.color);
    }
    out.push_back(cell);

    const Piece piece = p.at(pl.cell);
    if (piece.empty()) continue;

    // Piece types are distinguished by height rather than by a mesh: M4 ships one
    // material and no models, and a readable height ordering is more useful than a
    // pawn-shaped blob would be. Meshes are a later polish milestone.
    const float tallness =
        0.35f + 0.09f * static_cast<float>(std::min<std::uint16_t>(piece.type, 8));
    Instance body{};
    body.center[0] = pl.x;
    body.center[1] = pl.y;
    body.center[2] = pl.z + half.z + tallness * 0.5f;
    body.scale[0] = 0.55f;
    body.scale[1] = 0.55f;
    body.scale[2] = tallness;
    toFloat4(piece.colorOf() == Color::White ? theme_.whitePiece : theme_.blackPiece,
             body.color);
    out.push_back(body);
  }
  return out;
}

Result<void> BoardRenderer::render(const OffscreenTarget& target,
                                   const std::vector<Instance>& instances,
                                   const view::OrbitCamera& camera) {
  if (auto r = ensureInstanceCapacity(instances.size()); !r.has_value()) return r;
  if (!instances.empty()) {
    void* mapped = nullptr;
    vkMapMemory(ctx_->device(), instanceMem_, 0, instances.size() * sizeof(Instance), 0,
                &mapped);
    std::memcpy(mapped, instances.data(), instances.size() * sizeof(Instance));
    vkUnmapMemory(ctx_->device(), instanceMem_);
  }

  PushConstants push{};
  const float aspect =
      static_cast<float>(target.width()) / static_cast<float>(target.height());
  push.viewProj = camera.viewProj(aspect);

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

    // Both attachments start UNDEFINED and must reach the layout that
    // vkCmdBeginRendering declares for them. Forgetting the depth image here is exactly
    // the kind of mistake that draws a plausible-looking picture on one driver and
    // garbage on another - the validation layers caught it immediately, which is why a
    // validation message is treated as a failing test.
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
    color.clearValue.color = {{static_cast<float>(theme_.background.r) / 255.0f,
                               static_cast<float>(theme_.background.g) / 255.0f,
                               static_cast<float>(theme_.background.b) / 255.0f, 1.0f}};

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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    VkViewport viewport{0,
                        0,
                        static_cast<float>(target.width()),
                        static_cast<float>(target.height()),
                        0.0f,
                        1.0f};
    VkRect2D scissor{{0, 0}, {target.width(), target.height()}};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdPushConstants(cmd, layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(PushConstants), &push);

    if (!instances.empty()) {
      const VkDeviceSize zero = 0;
      vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &zero);
      vkCmdBindVertexBuffers(cmd, 1, 1, &instanceBuffer_, &zero);
      vkCmdBindIndexBuffer(cmd, indexBuffer_, 0, VK_INDEX_TYPE_UINT16);
      // One draw for the entire board, however many cells it has.
      vkCmdDrawIndexed(cmd, indexCount_, static_cast<std::uint32_t>(instances.size()), 0,
                       0, 0);
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
  const auto instances = buildInstances(p, cfg);
  if (auto r = render(target, instances, camera); !r.has_value()) {
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
  std::swap(vert_, o.vert_);
  std::swap(frag_, o.frag_);
  std::swap(layout_, o.layout_);
  std::swap(pipeline_, o.pipeline_);
  std::swap(vertexBuffer_, o.vertexBuffer_);
  std::swap(vertexMem_, o.vertexMem_);
  std::swap(indexBuffer_, o.indexBuffer_);
  std::swap(indexMem_, o.indexMem_);
  std::swap(indexCount_, o.indexCount_);
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
