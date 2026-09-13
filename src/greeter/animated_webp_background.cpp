#include "greeter/animated_webp_background.h"

#include "core/log.h"
#include "render/core/image_decoder.h" // kMaxWebpCanvasBytes
#include "render/core/texture_manager.h"
#include "render/render_context.h"
#include "util/file_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <utility>
#include <webp/decode.h>
#include <webp/demux.h>

namespace {

  constexpr Logger kLog("greeter");

  // Floor on a frame's on-screen duration. Guards a 0ms (or malformed negative)
  // inter-frame delta from advancing every tick, and keeps the effective cadence
  // at or under one display refresh.
  constexpr int kMinFrameDurationMs = 16;

  // Cap on how many frames a single advance() may decode, so a large delta after
  // a long pause (e.g. the greeter output being blanked and woken) cannot decode
  // a whole clip in one tick; the backlog is dropped instead.
  constexpr int kMaxFramesPerAdvance = 8;

  // Reject the file before reading it all into memory: a multi-GB file would OOM
  // the pre-auth greeter. A real 24fps 1080p clip is a few MB.
  constexpr std::uint64_t kMaxWebpFileBytes = 64ULL * 1024 * 1024;

  // Tighter than the still-image canvas cap: the animation decoder holds two full
  // canvases plus a GPU texture, so keep the canvas well under GL texture limits.
  constexpr std::uint64_t kMaxAnimatedCanvasBytes = 64ULL * 1024 * 1024;

} // namespace

void AnimatedWebpBackground::DecoderDeleter::operator()(WebPAnimDecoder* decoder) const noexcept {
  WebPAnimDecoderDelete(decoder);
}

AnimatedWebpBackground::~AnimatedWebpBackground() { stop(); }

AnimatedWebpBackground::StartResult
AnimatedWebpBackground::start(RenderContext& ctx, const std::string& path, std::function<void()> onFailure) {
  stop();

  std::error_code sizeEc;
  const auto fileSize = std::filesystem::file_size(path, sizeEc);
  if (sizeEc || fileSize == 0) {
    return StartResult::TransientFailure; // missing/unreadable now; may appear later
  }
  if (fileSize > kMaxWebpFileBytes) {
    kLog.warn("WebP \"{}\" is {} bytes, over the cap; ignoring", path, static_cast<std::uint64_t>(fileSize));
    return StartResult::NotAnimated;
  }

  std::vector<std::uint8_t> bytes = FileUtils::readBinaryFile(path);
  if (bytes.empty()) {
    return StartResult::TransientFailure; // missing/unreadable now; may appear later
  }
  if (bytes.size() > kMaxWebpFileBytes) {
    return StartResult::NotAnimated; // grew past the cap between stat and read
  }

  WebPBitstreamFeatures features;
  if (WebPGetFeatures(bytes.data(), bytes.size(), &features) != VP8_STATUS_OK) {
    return StartResult::NotAnimated; // not a WebP
  }
  if (features.has_animation == 0) {
    return StartResult::NotAnimated; // still WebP -> static path
  }
  // Reject an oversized canvas BEFORE WebPAnimDecoderNew: a VP8X header can
  // declare dimensions independent of the (small) file size, and the decoder
  // would allocate the full canvas twice up front (an OOM vector).
  if (features.width <= 0
      || features.height <= 0
      || static_cast<std::uint64_t>(features.width) * static_cast<std::uint64_t>(features.height) * 4
          > kMaxAnimatedCanvasBytes) {
    kLog.warn("animated WebP \"{}\" canvas {}x{} exceeds size cap; ignoring", path, features.width, features.height);
    return StartResult::NotAnimated;
  }

  WebPAnimDecoderOptions options;
  if (WebPAnimDecoderOptionsInit(&options) == 0) {
    return StartResult::TransientFailure;
  }
  options.color_mode = MODE_RGBA; // non-premultiplied RGBA8, matches TextureDataFormat::Rgba
  options.use_threads = 0;

  // The decoder references the encoded bytes without copying them, so they must
  // outlive it. Build decoder + bytes as locals and commit to members only once
  // every probe passes (moving a vector keeps the same buffer the decoder holds).
  const WebPData webpData{.bytes = bytes.data(), .size = bytes.size()};
  std::unique_ptr<WebPAnimDecoder, DecoderDeleter> decoder(WebPAnimDecoderNew(&webpData, &options));
  if (!decoder) {
    kLog.warn("failed to open WebP animation \"{}\"", path);
    return StartResult::NotAnimated;
  }

  WebPAnimInfo info;
  if (WebPAnimDecoderGetInfo(decoder.get(), &info) == 0 || info.canvas_width == 0 || info.canvas_height == 0) {
    kLog.warn("failed to read WebP animation info \"{}\"", path);
    return StartResult::NotAnimated;
  }

  m_ctx = &ctx;
  m_path = path;
  m_bytes = std::move(bytes);
  m_decoder = std::move(decoder);
  m_width = static_cast<int>(info.canvas_width);
  m_height = static_cast<int>(info.canvas_height);
  m_prevTimestampMs = 0;
  m_currentFrameDurationMs = kMinFrameDurationMs;
  m_accumulatedMs = 0.0F;
  m_onFailure = std::move(onFailure);

  m_texture = m_ctx->textureManager().createEmpty(m_width, m_height, TextureDataFormat::Rgba, TextureFilter::Linear);
  if (!m_texture.id.valid()) {
    stop();
    return StartResult::TransientFailure; // GPU allocation failed; may succeed later
  }

  kLog.info("streaming animated WebP background \"{}\" ({}x{}, {} frames)", path, m_width, m_height, info.frame_count);

  // Upload the first frame immediately so there is no blank flash before the
  // render loop starts advancing us.
  if (!decodeAndUploadNext()) {
    stop();
    return StartResult::NotAnimated; // frame 0 is undecodable -> treat as broken/still
  }
  return StartResult::Started;
}

void AnimatedWebpBackground::stop() {
  if (m_texture.id.valid() && m_ctx != nullptr) {
    m_ctx->textureManager().unload(m_texture);
  }
  m_texture = {};
  m_decoder.reset();
  m_bytes.clear();
  m_bytes.shrink_to_fit();
  m_onFailure = nullptr;
  m_ctx = nullptr;
  m_path.clear();
  m_width = 0;
  m_height = 0;
  m_prevTimestampMs = 0;
  m_currentFrameDurationMs = 0;
  m_accumulatedMs = 0.0F;
}

bool AnimatedWebpBackground::advance(float deltaMs) {
  if (!m_decoder) {
    return false;
  }
  m_accumulatedMs += deltaMs;
  bool uploaded = false;
  for (int decoded = 0; m_accumulatedMs >= static_cast<float>(m_currentFrameDurationMs); ++decoded) {
    if (decoded >= kMaxFramesPerAdvance) {
      m_accumulatedMs = 0.0F; // drop the backlog after a long pause
      break;
    }
    m_accumulatedMs -= static_cast<float>(m_currentFrameDurationMs);
    if (!decodeAndUploadNext()) {
      failStop();
      return uploaded;
    }
    uploaded = true;
  }
  return uploaded;
}

bool AnimatedWebpBackground::decodeAndUploadNext() {
  if (m_ctx == nullptr || !m_decoder) {
    return false;
  }

  // Loop: rewind to the first frame once the clip is exhausted.
  if (WebPAnimDecoderHasMoreFrames(m_decoder.get()) == 0) {
    WebPAnimDecoderReset(m_decoder.get());
    m_prevTimestampMs = 0;
  }

  std::uint8_t* frame = nullptr;
  int timestampMs = 0;
  if (WebPAnimDecoderGetNext(m_decoder.get(), &frame, &timestampMs) == 0 || frame == nullptr) {
    return false;
  }
  m_ctx->textureManager().updateSubImage(m_texture, frame, 0, 0, m_width, m_height, TextureDataFormat::Rgba);

  // The returned timestamp is the frame's cumulative END time; the delta from the
  // previous end is how long this frame should remain on screen.
  const int duration = timestampMs - m_prevTimestampMs;
  m_prevTimestampMs = timestampMs;
  m_currentFrameDurationMs = std::max(duration, kMinFrameDurationMs);
  return true;
}

int AnimatedWebpBackground::millisUntilNextFrame() const noexcept {
  if (!m_decoder) {
    return -1;
  }
  const float remaining = static_cast<float>(m_currentFrameDurationMs) - m_accumulatedMs;
  return remaining <= 0.0F ? 0 : static_cast<int>(std::ceil(remaining));
}

void AnimatedWebpBackground::failStop() {
  kLog.warn("WebP animation decode failed, stopping playback \"{}\"", m_path);
  // Capture the callback before stop() clears it, then notify the owner so it can
  // drop the now-deleted texture and fall back to a static background.
  auto onFailure = m_onFailure;
  stop();
  if (onFailure) {
    onFailure();
  }
}
