#pragma once

#include "wayland/wayland_extension.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

struct wl_output;
class WaylandConnection;

// wl_output geometry fields intentionally omitted by the shared connection.
// Observe only that metadata; transport, scale, roles, and input stay in lib/.
class GreeterOutputMetadata final : public WaylandExtension {
public:
  struct Output {
    GreeterOutputMetadata* owner = nullptr;
    wl_output* proxy = nullptr;
    int x = 0;
    int y = 0;
    std::string make;
    std::string model;
    bool changed = false;
  };

  explicit GreeterOutputMetadata(WaylandConnection&) {}
  void onGlobal(wl_registry* registry, std::uint32_t name, std::string_view interface, std::uint32_t version) override;
  void onGlobalRemove(std::uint32_t name) override;
  void releaseGlobals() override;
  const Output* find(std::uint32_t name) const;
  void setOnChanged(std::function<void()> callback) { m_onChanged = std::move(callback); }

private:
  std::unordered_map<std::uint32_t, std::unique_ptr<Output>> m_outputs;
  std::function<void()> m_onChanged;
};
