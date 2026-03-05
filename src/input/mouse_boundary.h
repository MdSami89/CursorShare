#pragma once
// =============================================================================
// CursorShare — Boundary-Aware Mouse Handler
// Tracks virtual cursor position on the client and clamps to display edges.
// =============================================================================

#include <atomic>
#include <cstdint>
#include <mutex>


namespace CursorShare {

/// Client display configuration.
struct ClientDisplay {
  int32_t width = 1920;  // Display width in pixels
  int32_t height = 1080; // Display height in pixels
  bool landscape = true; // Orientation (landscape=true, portrait=false)
};

/// Boundary-aware mouse position tracker.
/// Maintains a virtual cursor position for the client device and
/// prevents overflow beyond screen edges.
class MouseBoundary {
public:
  MouseBoundary();
  ~MouseBoundary() = default;

  /// Set the client display configuration.
  void SetClientDisplay(const ClientDisplay &display);

  /// Get current client display config.
  ClientDisplay GetClientDisplay() const;

  /// Apply relative mouse movement and return clamped deltas.
  /// @param dxIn   Raw relative X input
  /// @param dyIn   Raw relative Y input
  /// @param dxOut  Clamped X delta to send to client
  /// @param dyOut  Clamped Y delta to send to client
  void ApplyMovement(int16_t dxIn, int16_t dyIn, int16_t &dxOut,
                     int16_t &dyOut);

  /// Get current virtual cursor position.
  void GetPosition(int32_t &x, int32_t &y) const;

  /// Reset virtual cursor to center of display.
  void ResetToCenter();

  /// Handle orientation change (swap width/height).
  void SetOrientation(bool landscape);

  /// Enable/disable edge dead zone (reduces jitter at edges).
  void SetDeadZone(int32_t pixels) { deadZone_ = pixels; }

private:
  mutable std::mutex mutex_;
  ClientDisplay display_;

  // Virtual cursor position (accumulated)
  int32_t posX_ = 960; // Start at center
  int32_t posY_ = 540;

  // Dead zone at edges (pixels)
  int32_t deadZone_ = 2;

  // Smooth clamping: how aggressively to apply near edges
  float edgeFactor_ = 0.3f;
};

} // namespace CursorShare
