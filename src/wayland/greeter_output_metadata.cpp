#include "wayland/greeter_output_metadata.h"

#include <algorithm>
#include <utility>
#include <wayland-client.h>

void GreeterOutputMetadata::onGlobal(
    wl_registry* registry, std::uint32_t name, std::string_view interface, std::uint32_t version
) {
  if (interface != "wl_output")
    return;
  auto output = std::make_unique<Output>();
  output->owner = this;
  output->proxy =
      static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4U)));
  static const wl_output_listener listener = {
      .geometry =
          [](void* data, wl_output*, int x, int y, int, int, int, const char* make, const char* model, int) {
            auto& out = *static_cast<Output*>(data);
            const std::string nextMake = make ? make : "";
            const std::string nextModel = model ? model : "";
            out.changed = out.changed || out.x != x || out.y != y || out.make != nextMake || out.model != nextModel;
            out.x = x;
            out.y = y;
            out.make = nextMake;
            out.model = nextModel;
          },
      .mode = [](void*, wl_output*, std::uint32_t, int, int, int) {},
      .done =
          [](void* data, wl_output*) {
            auto& out = *static_cast<Output*>(data);
            if (std::exchange(out.changed, false) && out.owner->m_onChanged)
              out.owner->m_onChanged();
          },
      .scale = [](void*, wl_output*, int) {},
      .name = [](void*, wl_output*, const char*) {},
      .description = [](void*, wl_output*, const char*) {},
  };
  wl_output_add_listener(output->proxy, &listener, output.get());
  m_outputs.emplace(name, std::move(output));
}

void GreeterOutputMetadata::onGlobalRemove(std::uint32_t name) {
  if (const auto it = m_outputs.find(name); it != m_outputs.end()) {
    wl_output_destroy(it->second->proxy);
    m_outputs.erase(it);
  }
}

void GreeterOutputMetadata::releaseGlobals() {
  for (const auto& [name, output] : m_outputs)
    wl_output_destroy(output->proxy);
  m_outputs.clear();
}

const GreeterOutputMetadata::Output* GreeterOutputMetadata::find(std::uint32_t name) const {
  const auto it = m_outputs.find(name);
  return it == m_outputs.end() ? nullptr : it->second.get();
}
