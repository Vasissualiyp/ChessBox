// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "render/vulkan_context.hpp"

namespace cb::render {

/// A colour image plus a depth buffer, rendered into with dynamic rendering and read
/// back to host memory.
///
/// Having this - rather than only a swapchain - is what makes the renderer testable: a
/// test renders a frame and asserts on pixels, with no window, no display server and no
/// human looking at it.
class OffscreenTarget {
 public:
  static Result<OffscreenTarget> create(const VulkanContext& ctx, std::uint32_t width,
                                        std::uint32_t height);

  OffscreenTarget() = default;
  ~OffscreenTarget();
  OffscreenTarget(const OffscreenTarget&) = delete;
  OffscreenTarget& operator=(const OffscreenTarget&) = delete;
  OffscreenTarget(OffscreenTarget&& o) noexcept { *this = std::move(o); }
  OffscreenTarget& operator=(OffscreenTarget&& o) noexcept;

  [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
  [[nodiscard]] std::uint32_t height() const noexcept { return height_; }
  [[nodiscard]] VkFormat colorFormat() const noexcept { return kColorFormat; }
  [[nodiscard]] VkImageView colorView() const noexcept { return colorView_; }
  [[nodiscard]] VkImageView depthView() const noexcept { return depthView_; }
  [[nodiscard]] VkImage colorImage() const noexcept { return color_; }
  [[nodiscard]] VkImage depthImage() const noexcept { return depth_; }

  /// A second colour image the same size as the first.
  ///
  /// A separable blur needs somewhere to put the horizontal pass before the vertical one
  /// reads it back, and ping-ponging between two images means the result lands in the
  /// first one again with no copy. Nothing else uses it, and a frame that does not blur
  /// never touches it.
  [[nodiscard]] VkImage scratchImage() const noexcept { return scratch_; }
  [[nodiscard]] VkImageView scratchView() const noexcept { return scratchView_; }

  static constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
  static constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

  /// Read the colour image back as tightly packed RGBA8 rows.
  [[nodiscard]] Result<std::vector<std::uint8_t>> readPixels() const;

 private:
  Result<void> allocate(const VulkanContext& ctx);

  const VulkanContext* ctx_{nullptr};
  std::uint32_t width_{0};
  std::uint32_t height_{0};
  VkImage color_{VK_NULL_HANDLE};
  VkDeviceMemory colorMem_{VK_NULL_HANDLE};
  VkImageView colorView_{VK_NULL_HANDLE};
  VkImage scratch_{VK_NULL_HANDLE};
  VkDeviceMemory scratchMem_{VK_NULL_HANDLE};
  VkImageView scratchView_{VK_NULL_HANDLE};
  VkImage depth_{VK_NULL_HANDLE};
  VkDeviceMemory depthMem_{VK_NULL_HANDLE};
  VkImageView depthView_{VK_NULL_HANDLE};
};

}  // namespace cb::render
