// =============================================================================
// CursorShare — Boundary-Aware Mouse Handler (Implementation)
// =============================================================================

#include "mouse_boundary.h"
#include <algorithm>
#include <cmath>

namespace CursorShare {

MouseBoundary::MouseBoundary() { ResetToCenter(); }

void MouseBoundary::SetClientDisplay(const ClientDisplay &display) {
  std::lock_guard<std::mutex> lock(mutex_);
  display_ = display;

  // Clamp current position to new bounds
  posX_ = std::clamp(posX_, 0, display_.width - 1);
  posY_ = std::clamp(posY_, 0, display_.height - 1);
}

ClientDisplay MouseBoundary::GetClientDisplay() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return display_;
}

void MouseBoundary::ApplyMovement(int16_t dxIn, int16_t dyIn, int16_t &dxOut,
                                  int16_t &dyOut) {
  std::lock_guard<std::mutex> lock(mutex_);

  // Proposed new position
  int32_t newX = posX_ + static_cast<int32_t>(dxIn);
  int32_t newY = posY_ + static_cast<int32_t>(dyIn);

  // Hard clamp to display boundaries
  int32_t maxX = display_.width - 1;
  int32_t maxY = display_.height - 1;

  newX = std::clamp(newX, 0, maxX);
  newY = std::clamp(newY, 0, maxY);

  // Apply edge dead zone: reduce sensitivity near edges to prevent jitter
  if (deadZone_ > 0) {
    // If we're at the very edge, don't apply micro-movements that
    // would just bounce off the edge
    if (posX_ <= deadZone_ && dxIn < 0 && std::abs(dxIn) <= deadZone_) {
      newX = posX_;
    }
    if (posX_ >= maxX - deadZone_ && dxIn > 0 && std::abs(dxIn) <= deadZone_) {
      newX = posX_;
    }
    if (posY_ <= deadZone_ && dyIn < 0 && std::abs(dyIn) <= deadZone_) {
      newY = posY_;
    }
    if (posY_ >= maxY - deadZone_ && dyIn > 0 && std::abs(dyIn) <= deadZone_) {
      newY = posY_;
    }
  }

  // Compute actual deltas after clamping
  dxOut = static_cast<int16_t>(newX - posX_);
  dyOut = static_cast<int16_t>(newY - posY_);

  // Update tracked position
  posX_ = newX;
  posY_ = newY;
}

void MouseBoundary::GetPosition(int32_t &x, int32_t &y) const {
  std::lock_guard<std::mutex> lock(mutex_);
  x = posX_;
  y = posY_;
}

void MouseBoundary::ResetToCenter() {
  std::lock_guard<std::mutex> lock(mutex_);
  posX_ = display_.width / 2;
  posY_ = display_.height / 2;
}

void MouseBoundary::SetOrientation(bool landscape) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (display_.landscape != landscape) {
    // Swap width and height
    std::swap(display_.width, display_.height);
    display_.landscape = landscape;

    // Clamp and re-center
    posX_ = std::clamp(posX_, 0, display_.width - 1);
    posY_ = std::clamp(posY_, 0, display_.height - 1);
  }
}

} // namespace CursorShare
