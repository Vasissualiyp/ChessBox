// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "render/offscreen_target.hpp"
#include "render/vulkan_context.hpp"

struct SDL_Window;

namespace cb::render {

/// An SDL window with a Vulkan swapchain.
///
/// It presents by blitting the offscreen image the renderer already produced, rather
/// than rendering directly into a swapchain image. That keeps exactly one rendering path
/// in the project - the one the headless tests exercise - so what a player sees is what
/// the test suite checked, and the window code stays small enough to read.
class Window {
 public:
  static Result<Window> create(const char* title, std::uint32_t width,
                               std::uint32_t height);

  Window() = default;
  ~Window();
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&& o) noexcept { *this = std::move(o); }
  Window& operator=(Window&& o) noexcept;

  [[nodiscard]] SDL_Window* handle() const noexcept { return window_; }
  [[nodiscard]] const VulkanContext& context() const noexcept { return *ctx_; }
  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

  /// Acquire, blit `source` (left in TRANSFER_SRC_OPTIMAL by the renderer), present.
  Result<void> present(const OffscreenTarget& source);

  /// Rebuild the swapchain, e.g. after a resize.
  Result<void> recreate(std::uint32_t width, std::uint32_t height);

  /// On: FIFO, no tearing. Off: MAILBOX (newest frame, no queue) if the surface offers
  /// it, else IMMEDIATE, else FIFO - always a mode the surface actually supports.
  Result<void> setVsync(bool on);

 private:
  Result<void> buildSwapchain();
  void destroySwapchain();
  [[nodiscard]] VkPresentModeKHR choosePresentMode() const;

  std::unique_ptr<VulkanContext> ctx_;
  SDL_Window* window_{nullptr};
  VkSurfaceKHR surface_{VK_NULL_HANDLE};
  VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
  std::vector<VkImage> images_;
  VkFormat format_{VK_FORMAT_UNDEFINED};
  std::uint32_t width_{0};
  std::uint32_t height_{0};
  VkSemaphore acquired_{VK_NULL_HANDLE};
  VkSemaphore rendered_{VK_NULL_HANDLE};
  bool vsync_{true};
};

}  // namespace cb::render
