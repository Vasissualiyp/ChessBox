// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "render/offscreen_target.hpp"
#include "render/vulkan_context.hpp"

struct SDL_Window;

namespace cb::render {

/// An SDL window with a Vulkan swapchain, and a small ring of frames in flight.
///
/// It renders straight into the acquired swapchain image: each image has a render target
/// wrapping it (with its own multisample, depth and scratch attachments), so there is no
/// offscreen image and no blit on the interactive path (ADR-0018). The headless path -
/// `--shot` and the tests - still renders into an `OffscreenTarget` and reads it back;
/// both paths share one renderer and one shader set, and this class is the only place a
/// display is required.
class Window {
 public:
  /// Two frames may be in flight: the CPU records the next while the GPU draws the last.
  static constexpr std::uint32_t kFramesInFlight = 2;

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
  [[nodiscard]] VkFormat colorFormat() const noexcept { return format_; }
  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

  /// One in-flight frame. `beginFrame` waits for this slot and acquires an image, and
  /// begins `commandBuffer`; the caller records the board and the interface into it; then
  /// `submitFrame` ends and submits it and `presentFrame` presents it. A frame whose
  /// `target` is null means the swapchain went out of date and was rebuilt - skip it.
  struct Frame {
    std::uint32_t slot{0};
    std::uint32_t imageIndex{0};
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    OffscreenTarget* target{nullptr};
    [[nodiscard]] bool valid() const noexcept { return target != nullptr; }
  };

  Result<Frame> beginFrame();
  Result<void> submitFrame(const Frame& frame);
  Result<void> presentFrame(const Frame& frame);

  /// Rebuild the swapchain and its per-image targets, e.g. after a resize.
  Result<void> recreate(std::uint32_t width, std::uint32_t height);

  /// On: FIFO, no tearing. Off: MAILBOX (newest frame, no queue) if the surface offers
  /// it, else IMMEDIATE, else FIFO - always a mode the surface actually supports.
  Result<void> setVsync(bool on);

 private:
  struct Slot {
    VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
    VkSemaphore imageAvailable{VK_NULL_HANDLE};
  };

  Result<void> buildSwapchain();
  Result<void> buildFrameResources();
  void destroySwapchain();
  void destroyFrameResources();
  [[nodiscard]] VkPresentModeKHR choosePresentMode() const;

  std::unique_ptr<VulkanContext> ctx_;
  SDL_Window* window_{nullptr};
  VkSurfaceKHR surface_{VK_NULL_HANDLE};
  VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
  std::vector<VkImage> images_;
  /// One target per swapchain image. The colour image is the swapchain image; the MSAA,
  /// depth and scratch attachments are allocated with it.
  std::vector<OffscreenTarget> targets_;
  /// One per swapchain image: present waits for its render to finish.
  std::vector<VkSemaphore> renderFinished_;
  std::array<Slot, kFramesInFlight> slots_{};
  std::uint32_t frameCursor_{0};
  VkFormat format_{VK_FORMAT_UNDEFINED};
  std::uint32_t width_{0};
  std::uint32_t height_{0};
  bool vsync_{true};
  bool needsRecreate_{false};
};

}  // namespace cb::render
