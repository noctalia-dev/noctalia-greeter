#pragma once

#include "render/core/texture_handle.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class RenderContext;
struct WebPAnimDecoder;

/// Streams an animated WebP onto a single, reused GPU texture. Unlike the shell
/// lockscreen, the greeter has no timer: playback is advanced by the per-frame
/// render tick (advance()), decoding and uploading the next frame in place via
/// TextureManager::updateSubImage. Video memory stays bounded to roughly one
/// frame regardless of clip length. Used as the greeter background when the
/// configured wallpaper path is an animated WebP.
///
/// Single-threaded by design: all work runs on the render/main thread.
class AnimatedWebpBackground {
public:
  enum class StartResult {
    NotAnimated,      ///< not a WebP, a still WebP, or an oversized canvas -> use the static path (do not retry)
    TransientFailure, ///< a readable animated WebP but a transient error (e.g. unreadable file) -> retry later
    Started,          ///< streaming; first frame already uploaded
  };

  AnimatedWebpBackground() = default;
  ~AnimatedWebpBackground();

  AnimatedWebpBackground(const AnimatedWebpBackground&) = delete;
  AnimatedWebpBackground& operator=(const AnimatedWebpBackground&) = delete;

  /// Begins playback if `path` is an animated WebP within the canvas-size cap,
  /// uploading the first frame immediately. `onFailure` fires once if a later
  /// decode error stops playback, so the owner can drop the (now deleted)
  /// texture and fall back to a static background.
  StartResult start(RenderContext& ctx, const std::string& path, std::function<void()> onFailure);

  /// Full teardown: releases the GPU texture and frees the decoder. Safe when
  /// inactive. Must run while `ctx` (from start) is still valid.
  void stop();

  /// Advances playback by `deltaMs` of elapsed frame time, decoding and
  /// uploading the next frame(s) whose display duration has elapsed and looping
  /// at the end. Returns true if a new frame was uploaded this call, so the owner
  /// can mark its node dirty. No-op (returns false) when inactive.
  bool advance(float deltaMs);

  [[nodiscard]] bool active() const noexcept { return m_decoder != nullptr; }
  [[nodiscard]] const std::string& path() const noexcept { return m_path; }
  [[nodiscard]] TextureHandle texture() const noexcept { return m_texture; }

  /// Milliseconds until the next frame is due (0 if due now), or -1 when inactive.
  /// Lets the owner sleep exactly until the next frame instead of polling.
  [[nodiscard]] int millisUntilNextFrame() const noexcept;

private:
  struct DecoderDeleter {
    void operator()(WebPAnimDecoder* decoder) const noexcept;
  };

  bool decodeAndUploadNext();
  void failStop();

  RenderContext* m_ctx = nullptr;
  std::string m_path;
  std::vector<std::uint8_t> m_bytes; // owns the encoded data the decoder references
  std::unique_ptr<WebPAnimDecoder, DecoderDeleter> m_decoder;
  int m_width = 0;
  int m_height = 0;
  int m_prevTimestampMs = 0;        // cumulative end time of the last decoded frame (libwebp semantics)
  int m_currentFrameDurationMs = 0; // on-screen duration of the currently shown frame
  float m_accumulatedMs = 0.0F;     // elapsed time banked toward the next frame
  TextureHandle m_texture{};
  std::function<void()> m_onFailure;
};
