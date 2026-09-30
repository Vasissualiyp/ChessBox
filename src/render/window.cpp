// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/window.hpp"

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

  VkSemaphoreCreateInfo sci{};
  sci.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  vkCreateSemaphore(w.ctx_->device(), &sci, nullptr, &w.acquired_);
  vkCreateSemaphore(w.ctx_->device(), &sci, nullptr, &w.rendered_);

  w.width_ = width;
  w.height_ = height;
  if (auto r = w.buildSwapchain(); !r.has_value()) {
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
  sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
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
  return {};
}

void Window::destroySwapchain() {
  if (ctx_ && swapchain_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(ctx_->device());
    vkDestroySwapchainKHR(ctx_->device(), swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    images_.clear();
  }
}

Result<void> Window::recreate(std::uint32_t width, std::uint32_t height) {
  destroySwapchain();
  width_ = width;
  height_ = height;
  return buildSwapchain();
}

Result<void> Window::present(const OffscreenTarget& source) {
  std::uint32_t index = 0;
  const VkResult acquire = vkAcquireNextImageKHR(ctx_->device(), swapchain_, UINT64_MAX,
                                                 acquired_, VK_NULL_HANDLE, &index);
  if (acquire == VK_ERROR_OUT_OF_DATE_KHR) return recreate(width_, height_);
  if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
    return fail(ErrorCode::Internal,
                "cannot acquire a swapchain image: " + describe(acquire));
  }

  VkCommandBufferAllocateInfo ai{};
  ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  ai.commandPool = ctx_->commandPool();
  ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  ai.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(ctx_->device(), &ai, &cmd);

  VkCommandBufferBeginInfo bi{};
  bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &bi);

  const auto toLayout = [&](VkImageLayout from, VkImageLayout to,
                            VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess) {
    VkImageMemoryBarrier2 b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    b.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.srcAccessMask = srcAccess;
    b.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    b.dstAccessMask = dstAccess;
    b.oldLayout = from;
    b.newLayout = to;
    b.image = images_[index];
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkDependencyInfo dep{};
    dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
  };

  toLayout(VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
           VK_ACCESS_2_TRANSFER_WRITE_BIT);

  // A blit rather than a copy, so a window that is not exactly the render size still
  // shows the whole board instead of a cropped corner.
  VkImageBlit2 region{};
  region.sType = VK_STRUCTURE_TYPE_IMAGE_BLIT_2;
  region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.srcOffsets[1] = {static_cast<std::int32_t>(source.width()),
                          static_cast<std::int32_t>(source.height()), 1};
  region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.dstOffsets[1] = {static_cast<std::int32_t>(width_),
                          static_cast<std::int32_t>(height_), 1};

  VkBlitImageInfo2 blit{};
  blit.sType = VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2;
  blit.srcImage = source.colorImage();
  blit.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  blit.dstImage = images_[index];
  blit.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  blit.regionCount = 1;
  blit.pRegions = &region;
  blit.filter = VK_FILTER_LINEAR;
  vkCmdBlitImage2(cmd, &blit);

  toLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
           VK_ACCESS_2_TRANSFER_WRITE_BIT, 0);
  vkEndCommandBuffer(cmd);

  const VkPipelineStageFlags wait = VK_PIPELINE_STAGE_TRANSFER_BIT;
  VkSubmitInfo si{};
  si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  si.waitSemaphoreCount = 1;
  si.pWaitSemaphores = &acquired_;
  si.pWaitDstStageMask = &wait;
  si.commandBufferCount = 1;
  si.pCommandBuffers = &cmd;
  si.signalSemaphoreCount = 1;
  si.pSignalSemaphores = &rendered_;
  vkQueueSubmit(ctx_->queue(), 1, &si, VK_NULL_HANDLE);

  VkPresentInfoKHR pi{};
  pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  pi.waitSemaphoreCount = 1;
  pi.pWaitSemaphores = &rendered_;
  pi.swapchainCount = 1;
  pi.pSwapchains = &swapchain_;
  pi.pImageIndices = &index;
  const VkResult presented = vkQueuePresentKHR(ctx_->queue(), &pi);

  // One submission in flight, waited on before the buffer is reused. A turn-based game
  // redraws on input, so pipelining would add complexity for no benefit.
  vkQueueWaitIdle(ctx_->queue());
  vkFreeCommandBuffers(ctx_->device(), ctx_->commandPool(), 1, &cmd);

  if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
    return recreate(width_, height_);
  }
  if (presented != VK_SUCCESS) {
    return fail(ErrorCode::Internal, "present failed: " + describe(presented));
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
  std::swap(format_, o.format_);
  std::swap(width_, o.width_);
  std::swap(height_, o.height_);
  std::swap(acquired_, o.acquired_);
  std::swap(rendered_, o.rendered_);
  return *this;
}

Window::~Window() {
  if (ctx_) {
    destroySwapchain();
    if (acquired_ != VK_NULL_HANDLE)
      vkDestroySemaphore(ctx_->device(), acquired_, nullptr);
    if (rendered_ != VK_NULL_HANDLE)
      vkDestroySemaphore(ctx_->device(), rendered_, nullptr);
    if (surface_ != VK_NULL_HANDLE)
      vkDestroySurfaceKHR(ctx_->instance(), surface_, nullptr);
  }
  if (window_ != nullptr) SDL_DestroyWindow(window_);
}

}  // namespace cb::render
