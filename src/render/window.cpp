// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/window.hpp"

#include <algorithm>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

namespace cb::render {

Result<Window> Window::create(const char* title, std::uint32_t width,
                              std::uint32_t height) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    return fail(ErrorCode::Unsupported,
                std::string("SDL_Init failed: ") + SDL_GetError());
  }
  Window w;
  w.window_ = SDL_CreateWindow(title, static_cast<int>(width), static_cast<int>(height),
                               SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
  if (w.window_ == nullptr) {
    return fail(ErrorCode::Unsupported,
                std::string("cannot create a window: ") + SDL_GetError());
  }

  // SDL tells us which instance extensions the platform's surface needs; hard-coding
  // them is how a renderer ends up working on exactly one window system.
  std::uint32_t extCount = 0;
  const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&extCount);
  ContextOptions opts;
  opts.needSwapchain = true;
  for (std::uint32_t i = 0; i < extCount; ++i)
    opts.instanceExtensions.emplace_back(exts[i]);

  auto ctx = VulkanContext::create(opts);
  if (!ctx.has_value()) return fail(ctx.error().code, ctx.error().message);
  w.ctx_ = std::make_unique<VulkanContext>(std::move(*ctx));

  if (!SDL_Vulkan_CreateSurface(w.window_, w.ctx_->instance(), nullptr, &w.surface_)) {
    return fail(ErrorCode::Unsupported,
                std::string("cannot create a Vulkan surface: ") + SDL_GetError());
  }

  VkBool32 presentSupported = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(w.ctx_->physicalDevice(), w.ctx_->queueFamily(),
                                       w.surface_, &presentSupported);
  if (presentSupported != VK_TRUE) {
    return fail(ErrorCode::Unsupported, "the graphics queue on '" + w.ctx_->deviceName() +
                                            "' cannot present to this surface");
  }

  w.width_ = width;
  w.height_ = height;
  if (auto r = w.buildSwapchain(); !r.has_value()) {
    return fail(r.error().code, r.error().message);
  }
  if (auto r = w.buildFrameResources(); !r.has_value()) {
    return fail(r.error().code, r.error().message);
  }
  return w;
}

Result<void> Window::setVsync(bool on) {
  if (on == vsync_) return {};
  vsync_ = on;
  return recreate(width_, height_);
}

VkPresentModeKHR Window::choosePresentMode() const {
  if (vsync_) return VK_PRESENT_MODE_FIFO_KHR;
  std::uint32_t count = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(ctx_->physicalDevice(), surface_, &count,
                                            nullptr);
  std::vector<VkPresentModeKHR> modes(count);
  vkGetPhysicalDeviceSurfacePresentModesKHR(ctx_->physicalDevice(), surface_, &count,
                                            modes.data());
  for (const VkPresentModeKHR m : modes) {
    if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
  }
  for (const VkPresentModeKHR m : modes) {
    if (m == VK_PRESENT_MODE_IMMEDIATE_KHR) return m;
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

Result<void> Window::buildSwapchain() {
  VkSurfaceCapabilitiesKHR caps{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx_->physicalDevice(), surface_, &caps);

  std::uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_->physicalDevice(), surface_, &formatCount,
                                       nullptr);
  std::vector<VkSurfaceFormatKHR> formats(formatCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(ctx_->physicalDevice(), surface_, &formatCount,
                                       formats.data());
  if (formats.empty())
    return fail(ErrorCode::Unsupported, "the surface offers no formats");

  VkSurfaceFormatKHR chosen = formats.front();
  for (const VkSurfaceFormatKHR& f : formats) {
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
      chosen = f;
      break;
    }
  }
  format_ = chosen.format;

  if (caps.currentExtent.width != 0xFFFFFFFFu) {
    width_ = caps.currentExtent.width;
    height_ = caps.currentExtent.height;
  }

  VkSwapchainCreateInfoKHR sci{};
  sci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  sci.surface = surface_;
  sci.minImageCount = std::max(caps.minImageCount, 2u);
  sci.imageFormat = chosen.format;
  sci.imageColorSpace = chosen.colorSpace;
  sci.imageExtent = {width_, height_};
  sci.imageArrayLayers = 1;
  // The image is rendered into directly now, sampled by the optional defocus pass, and
  // resolved into for MSAA - so it needs all three usages, not just TRANSFER_DST.
  sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                   VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  // FIFO is always supported and is exactly right for a turn-based game: no tearing, no
  // spinning the GPU for frames nobody asked for. With v-sync off, take the fastest mode
  // the surface actually offers.
  sci.presentMode = choosePresentMode();
  sci.clipped = VK_TRUE;

  if (const VkResult r = vkCreateSwapchainKHR(ctx_->device(), &sci, nullptr, &swapchain_);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Unsupported, "cannot create a swapchain: " + describe(r));
  }

  std::uint32_t imageCount = 0;
  vkGetSwapchainImagesKHR(ctx_->device(), swapchain_, &imageCount, nullptr);
  images_.resize(imageCount);
  vkGetSwapchainImagesKHR(ctx_->device(), swapchain_, &imageCount, images_.data());

  // A target per image, wrapping it as the colour attachment. The view, MSAA, depth and
  // scratch images each target allocates belong to it and go with it.
  targets_.clear();
  VkSemaphoreCreateInfo sci2{};
  sci2.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  for (VkImage image : images_) {
    auto t = OffscreenTarget::wrapColor(*ctx_, image, format_, width_, height_);
    if (!t.has_value()) return fail(t.error().code, t.error().message);
    targets_.push_back(std::move(*t));
    VkSemaphore finished = VK_NULL_HANDLE;
    if (const VkResult r = vkCreateSemaphore(ctx_->device(), &sci2, nullptr, &finished);
        r != VK_SUCCESS) {
      return fail(ErrorCode::Internal,
                  "cannot create a present semaphore: " + describe(r));
    }
    renderFinished_.push_back(finished);
  }
  return {};
}

Result<void> Window::buildFrameResources() {
  VkCommandBufferAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  ai.commandPool = ctx_->commandPool();
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = kFramesInFlight;
  std::array<VkCommandBuffer, kFramesInFlight> buffers{};
  if (const VkResult r = vkAllocateCommandBuffers(ctx_->device(), &ai, buffers.data());
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal,
                "cannot allocate frame command buffers: " + describe(r));
  }

  VkFenceCreateInfo fci{};
  fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  // Signalled, so the first frame does not wait on a fence nobody has submitted.
  fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  VkSemaphoreCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
    slots_[i].commandBuffer = buffers[i];
    if (const VkResult r = vkCreateFence(ctx_->device(), &fci, nullptr, &slots_[i].fence);
        r != VK_SUCCESS) {
      return fail(ErrorCode::Internal, "cannot create a frame fence: " + describe(r));
    }
    if (const VkResult r =
            vkCreateSemaphore(ctx_->device(), &sci, nullptr, &slots_[i].imageAvailable);
        r != VK_SUCCESS) {
      return fail(ErrorCode::Internal,
                  "cannot create an acquire semaphore: " + describe(r));
    }
  }
  return {};
}

void Window::destroyFrameResources() {
  if (ctx_ == nullptr || ctx_->device() == VK_NULL_HANDLE) return;
  VkDevice d = ctx_->device();
  for (Slot& slot : slots_) {
    if (slot.fence != VK_NULL_HANDLE) vkDestroyFence(d, slot.fence, nullptr);
    if (slot.imageAvailable != VK_NULL_HANDLE)
      vkDestroySemaphore(d, slot.imageAvailable, nullptr);
    slot.fence = VK_NULL_HANDLE;
    slot.imageAvailable = VK_NULL_HANDLE;
  }
  if (!slots_.empty() && slots_[0].commandBuffer != VK_NULL_HANDLE) {
    std::array<VkCommandBuffer, kFramesInFlight> buffers{};
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i)
      buffers[i] = slots_[i].commandBuffer;
    vkFreeCommandBuffers(d, ctx_->commandPool(), kFramesInFlight, buffers.data());
    for (Slot& slot : slots_) slot.commandBuffer = VK_NULL_HANDLE;
  }
}

void Window::destroySwapchain() {
  if (ctx_ && swapchain_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(ctx_->device());
    vkDestroySwapchainKHR(ctx_->device(), swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    images_.clear();
    targets_.clear();  // destroys each wrapped image's view and our attachments
    for (VkSemaphore s : renderFinished_) vkDestroySemaphore(ctx_->device(), s, nullptr);
    renderFinished_.clear();
  }
}

Result<void> Window::recreate(std::uint32_t width, std::uint32_t height) {
  destroySwapchain();
  width_ = width;
  height_ = height;
  return buildSwapchain();
}

Result<Window::Frame> Window::beginFrame() {
  Slot& slot = slots_[frameCursor_];
  // The slot is only reused once its previous submission has finished; that fence is
  // what makes the per-frame instance buffer and blur descriptors safe to overwrite.
  vkWaitForFences(ctx_->device(), 1, &slot.fence, VK_TRUE, UINT64_MAX);

  std::uint32_t index = 0;
  const VkResult acquired =
      vkAcquireNextImageKHR(ctx_->device(), swapchain_, UINT64_MAX, slot.imageAvailable,
                            VK_NULL_HANDLE, &index);
  if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
    if (auto r = recreate(width_, height_); !r.has_value())
      return fail(r.error().code, r.error().message);
    return Frame{};  // the swapchain was rebuilt; skip this frame
  }
  if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
    return fail(ErrorCode::Internal,
                "cannot acquire a swapchain image: " + describe(acquired));
  }
  // Suboptimal still gives a usable image; present will ask for a rebuild afterwards.
  if (acquired == VK_SUBOPTIMAL_KHR) needsRecreate_ = true;

  vkResetCommandBuffer(slot.commandBuffer, 0);
  VkCommandBufferBeginInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(slot.commandBuffer, &bi);

  Frame frame;
  frame.slot = frameCursor_;
  frame.imageIndex = index;
  frame.commandBuffer = slot.commandBuffer;
  frame.target = &targets_[index];
  return frame;
}

Result<void> Window::submitFrame(const Frame& frame) {
  VkCommandBuffer cmd = frame.commandBuffer;
  VkImage image = targets_[frame.imageIndex].colorImage();

  // The renderer leaves the colour image in COLOR_ATTACHMENT_OPTIMAL; presenting needs
  // PRESENT_SRC_KHR. Doing it here keeps that one transition beside the present it feeds.
  VkImageMemoryBarrier2 b{};
  b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
  b.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
  b.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
  b.dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
  b.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  b.image = image;
  b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  VkDependencyInfo dep{};
  dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
  dep.imageMemoryBarrierCount = 1;
  dep.pImageMemoryBarriers = &b;
  vkCmdPipelineBarrier2(cmd, &dep);
  vkEndCommandBuffer(cmd);

  Slot& slot = slots_[frame.slot];
  const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &slot.imageAvailable;
  si.pWaitDstStageMask = &waitStage;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &renderFinished_[frame.imageIndex];

  vkResetFences(ctx_->device(), 1, &slot.fence);
  if (const VkResult r = vkQueueSubmit(ctx_->queue(), 1, &si, slot.fence);
      r != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "queue submit failed: " + describe(r));
  }
  return {};
}

Result<void> Window::presentFrame(const Frame& frame) {
  VkPresentInfoKHR pi{};
  pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &renderFinished_[frame.imageIndex];
  pi.swapchainCount = 1;
  pi.pSwapchains = &swapchain_;
  pi.pImageIndices = &frame.imageIndex;
  const VkResult presented = vkQueuePresentKHR(ctx_->queue(), &pi);

  frameCursor_ = (frameCursor_ + 1) % kFramesInFlight;

  if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
    needsRecreate_ = true;
  } else if (presented != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "present failed: " + describe(presented));
  }
  if (needsRecreate_) {
    needsRecreate_ = false;
    return recreate(width_, height_);
  }
  return {};
}

Window& Window::operator=(Window&& o) noexcept {
  if (this == &o) return *this;
  std::swap(ctx_, o.ctx_);
  std::swap(window_, o.window_);
  std::swap(surface_, o.surface_);
  std::swap(swapchain_, o.swapchain_);
  std::swap(images_, o.images_);
  std::swap(targets_, o.targets_);
  std::swap(renderFinished_, o.renderFinished_);
  std::swap(slots_, o.slots_);
  std::swap(frameCursor_, o.frameCursor_);
  std::swap(format_, o.format_);
  std::swap(width_, o.width_);
  std::swap(height_, o.height_);
  std::swap(vsync_, o.vsync_);
  std::swap(needsRecreate_, o.needsRecreate_);
  return *this;
}

Window::~Window() {
  if (ctx_) {
    destroySwapchain();
    destroyFrameResources();
    if (surface_ != VK_NULL_HANDLE)
      vkDestroySurfaceKHR(ctx_->instance(), surface_, nullptr);
  }
  if (window_ != nullptr) SDL_DestroyWindow(window_);
}

}  // namespace cb::render
