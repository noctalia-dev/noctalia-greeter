#include "greeter/appearance_sync.h"
#include "greeter/greeter_config_store.h"
#include "greeter/greeter_preferences.h"
#include "render/codecs/image_encoder.h"
#include "tools/secure_appearance_sync.h"

#include <array>
#include <cerrno>
#include <filesystem>
#include <format>
#include <fstream>
#include <glib.h>
#include <iterator>
#include <print>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
  std::string failCommitPath;
  void require(bool condition, const std::string& message) {
    if (!condition)
      throw std::runtime_error(message);
  }
  std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    require(file.is_open(), "fixture read failed");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
  }
  void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary);
    out << bytes;
    out.close();
    require(!out.fail() && ::chmod(path.c_str(), 0600) == 0, "fixture write failed");
  }
  struct Fixture {
    std::filesystem::path root, staging, state;
    std::optional<std::string> oldState;
    Fixture() {
      char pattern[] = "/tmp/noctalia-customization-sync-XXXXXX";
      const auto* path = ::mkdtemp(pattern);
      require(path, "mkdtemp failed");
      root = path;
      staging = root / "staging";
      state = root / "state";
      std::filesystem::create_directory(staging);
      std::filesystem::create_directory(state);
      ::chmod(staging.c_str(), 0700);
      ::chmod(state.c_str(), 0700);
      if (const auto* prior = std::getenv(greeter::appearance::kSyncedDataDirEnv))
        oldState = prior;
      require(::setenv(greeter::appearance::kSyncedDataDirEnv, state.c_str(), 1) == 0, "setenv failed");
      write(state / "greeter.toml", "[user]\ndefault='admin-user'\n[layout.login]\nwidth=500\n");
      write(
          state / "sync.toml",
          "[session]\nlast='remembered'\n[session.power]\nshutdown='administrator-command'\n[layout]\nlogin_mode='full'"
          "\n[widgets]\nwidget_order=[]\n"
      );
    }
    ~Fixture() {
      failCommitPath.clear();
      if (oldState)
        ::setenv(greeter::appearance::kSyncedDataDirEnv, oldState->c_str(), 1);
      else
        ::unsetenv(greeter::appearance::kSyncedDataDirEnv);
      greeter::config::clearConfigDiagnostics();
      std::filesystem::remove_all(root);
    }
    std::string appearance(bool wallpaper = false) const {
      std::string text = "[appearance]\nscheme='Synced'\n";
      text += "[appearance.palette]\n";
      for (const auto key : greeter::appearance::requiredPaletteKeys())
        text += std::string(key) + "='#123456'\n";
      if (wallpaper)
        text += "[appearance.wallpaper]\npath='/var/lib/noctalia-greeter/wallpaper.png'\nfill_mode='crop'\n";
      return text;
    }
    std::string image(unsigned color = 128) const {
      const std::array<std::uint8_t, 4> pixel{static_cast<std::uint8_t>(color), 64, 32, 255};
      const auto bytes = encodePng(pixel.data(), 1, 1);
      gchar* digest = g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes.data(), bytes.size());
      require(digest, "hash failed");
      const std::string hash(digest);
      g_free(digest);
      write(
          staging / ("widget-" + hash + ".png"), std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size())
      );
      return hash;
    }
    std::string widgets(const std::string& hash, bool disabled = false) const {
      return "[widgets]\nwidget_order=['picture']\n[widgets.widget.picture]\ntype='sticker'\nplacement='anchor'"
             "\nanchor='top-left'\n"
          + std::string(disabled ? "enabled=false\n" : "")
          + "[widgets.widget.picture.settings]\nimage_path='widget-assets/"
          + hash
          + ".png'\n"
          + "[assets]\nimages=['widget-"
          + hash
          + ".png']\n";
    }
    bool apply(std::string& error) const {
      error.clear();
      return greeter::secure_sync::detail::applyConstrainedPayloadForTesting(staging, error);
    }
    bool validate(std::string& error) const {
      error.clear();
      return greeter::secure_sync::detail::validateConstrainedPayloadForTesting(staging, error);
    }
  };

  void testInstallAndReset() {
    Fixture fixture;
    const auto admin = read(fixture.state / "greeter.toml");
    const auto hash = fixture.image();
    const auto payload = fixture.appearance()
        + "[layout]\nlogin_mode='auto'\n[layout.login]\nwidth=400\nrotation=0.2\nflip_x=false\n"
        + "[layout.outputs.DP-1.login]\nx=100\n[layout.outputs.DP-2.login]\nx=-100\n"
        + fixture.widgets(hash);
    write(fixture.staging / "sync.toml", payload);
    std::string error;
    require(fixture.apply(error), "install failed: " + error);
    auto sync = greeter::config::loadSync(fixture.state / "sync.toml");
    require(
        sync.layout->outputs.at("DP-1").components[0].x == 100
            && sync.layout->outputs.at("DP-2").components[0].x == -100,
        "output layouts were not installed independently"
    );
    const auto installed = fixture.state / "widget-assets" / (hash + ".png");
    require(
        sync.layout
            && sync.layout->loginPresentation == greeter::LoginPresentation::Auto
            && sync.layout->components[0].rotation == 0.2
            && sync.layout->components[0].flipX == false,
        "core snapshot changed"
    );
    require(
        sync.widgets
            && sync.widgets->instances->size() == 1
            && std::get<std::string>(sync.widgets->instances->front().settings.at("image_path")) == installed.string(),
        "installed image reference not rewritten"
    );
    require(read(installed) == read(fixture.staging / ("widget-" + hash + ".png")), "image bytes changed");
    struct stat imageStat{};
    require(
        ::stat(installed.c_str(), &imageStat) == 0
            && imageStat.st_uid == ::geteuid()
            && (imageStat.st_mode & 0777) == 0644,
        "installed image metadata wrong"
    );
    require(read(fixture.staging / "sync.toml") == payload, "caller staging was rewritten");
    require(
        sync.sessionLast == "remembered"
            && sync.sessionPowerShutdown == "administrator-command"
            && read(fixture.state / "greeter.toml") == admin,
        "sync changed administrator or session settings"
    );
    require(greeter::loadGreeterPreferences().layout.components[0].width == 500, "administrator layout lost priority");
    const auto inode = imageStat.st_ino;
    require(fixture.apply(error), "image reuse failed: " + error);
    require(::stat(installed.c_str(), &imageStat) == 0 && imageStat.st_ino == inode, "valid image was replaced");
    std::filesystem::remove(fixture.staging / ("widget-" + hash + ".png"));
    write(fixture.staging / "sync.toml", fixture.appearance());
    require(fixture.apply(error), "appearance-only apply failed: " + error);
    sync = greeter::config::loadSync(fixture.state / "sync.toml");
    require(sync.layout && sync.widgets && std::filesystem::exists(installed), "omission erased customization");
    write(fixture.staging / "sync.toml", fixture.appearance() + "[layout]\n[widgets]\nwidget_order=[]\n");
    require(fixture.apply(error), "empty widget list failed: " + error);
    sync = greeter::config::loadSync(fixture.state / "sync.toml");
    require(
        !sync.layout && sync.widgets && sync.widgets->instances && sync.widgets->instances->empty(),
        "reset/empty semantics lost"
    );
    require(!std::filesystem::exists(installed), "unused Sync image was not pruned");
    write(fixture.staging / "sync.toml", fixture.appearance() + "[widgets]\n");
    require(fixture.apply(error), "widget defaults reset failed: " + error);
    require(!greeter::config::loadSync(fixture.state / "sync.toml").widgets, "default request became an empty list");
  }

  void testImageRetention() {
    Fixture fixture;
    const auto adminHash = fixture.image(20);
    write(fixture.staging / "sync.toml", fixture.appearance() + fixture.widgets(adminHash));
    std::string error;
    require(fixture.apply(error), "initial retention install failed: " + error);
    auto adminWidgets = fixture.widgets(adminHash, true);
    adminWidgets.erase(adminWidgets.find("[assets]"));
    write(fixture.state / "greeter.toml", adminWidgets);
    const auto adminBytes = read(fixture.state / "greeter.toml");
    std::filesystem::remove(fixture.staging / ("widget-" + adminHash + ".png"));
    const auto syncHash = fixture.image(21);
    write(fixture.staging / "sync.toml", fixture.appearance() + fixture.widgets(syncHash));
    require(fixture.apply(error), "second retention install failed: " + error);
    require(
        !greeter::loadGreeterPreferences().widgets->instances->front().enabled, "administrator collection lost priority"
    );
    const auto assets = fixture.state / "widget-assets";
    write(assets / "keep.txt", "unmanaged");
    std::filesystem::create_symlink(fixture.root / "missing", assets / (std::string(64, 'a') + ".png"));
    std::filesystem::remove(fixture.staging / ("widget-" + syncHash + ".png"));
    write(fixture.staging / "sync.toml", fixture.appearance() + "[widgets]\n");
    require(fixture.apply(error), "retention reset failed: " + error);
    require(std::filesystem::exists(assets / (adminHash + ".png")), "disabled administrator image pruned");
    require(!std::filesystem::exists(assets / (syncHash + ".png")), "unreferenced Sync image retained");
    require(
        read(assets / "keep.txt") == "unmanaged"
            && std::filesystem::is_symlink(assets / (std::string(64, 'a') + ".png"))
            && read(fixture.state / "greeter.toml") == adminBytes,
        "cleanup changed unmanaged assets or administrator configuration"
    );
    write(fixture.state / "greeter.toml", "[widgets]\nwidget_order='invalid'\n");
    require(
        fixture.apply(error) && std::filesystem::exists(assets / (adminHash + ".png")),
        "cleanup ran despite administrator diagnostics"
    );
  }

  void testImageQuotas() {
    Fixture fixture;
    std::string order = "[widgets]\nwidget_order=[", entries, manifest = "[assets]\nimages=[";
    for (unsigned i = 0; i < 32; ++i) {
      const auto hash = fixture.image(i);
      const auto id = "image" + std::to_string(i);
      order += "'" + id + "',";
      manifest += "'widget-" + hash + ".png',";
      entries += std::format(
          "[widgets.widget.{}]\ntype='sticker'\nplacement='anchor'\nanchor='center'\n"
          "[widgets.widget.{}.settings]\nimage_path='widget-assets/{}.png'\n",
          id, id, hash
      );
    }
    write(fixture.staging / "sync.toml", fixture.appearance() + order + "]\n" + entries + manifest + "]\n");
    std::string error;
    require(fixture.validate(error), "32 images did not receive a separate file quota: " + error);
    fixture.image(32);
    require(!fixture.validate(error) && error.contains("too many files"), "33 staged images admitted");
    for (const auto& entry : std::filesystem::directory_iterator(fixture.staging))
      std::filesystem::remove(entry.path());
    write(fixture.staging / "sync.toml", fixture.appearance());
    const auto oversized = fixture.staging / ("widget-" + std::string(64, 'a') + ".png");
    write(oversized, "");
    constexpr auto limit = 16U * 1024U * 1024U;
    std::filesystem::resize_file(oversized, limit + 1);
    require(!fixture.validate(error) && error.contains("allowed size"), "oversized staged image admitted");
    std::filesystem::resize_file(oversized, limit);
    for (char c : {'b', 'c', 'd', 'e'}) {
      const auto path = fixture.staging / ("widget-" + std::string(64, c) + ".png");
      write(path, "");
      std::filesystem::resize_file(path, limit);
    }
    require(!fixture.validate(error) && error.contains("allowed size"), "aggregate staged image limit bypassed");
  }

  void testRollback() {
    for (const bool existingSync : {true, false}) {
      Fixture fixture;
      const auto hash = fixture.image();
      write(fixture.state / "wallpaper.png", "old wallpaper");
      write(fixture.staging / "wallpaper.png", "new wallpaper");
      struct stat before{};
      require(::stat((fixture.state / "wallpaper.png").c_str(), &before) == 0, "wallpaper stat failed");
      const auto oldSync = read(fixture.state / "sync.toml");
      const auto oldAdmin = read(fixture.state / "greeter.toml");
      if (!existingSync)
        std::filesystem::remove(fixture.state / "sync.toml");
      write(fixture.staging / "sync.toml", fixture.appearance(true) + fixture.widgets(hash));
      failCommitPath = (fixture.state / "sync.toml").string();
      std::string error;
      require(!fixture.apply(error), "injected commit failure succeeded");
      failCommitPath.clear();
      require(read(fixture.state / "wallpaper.png") == "old wallpaper", "failed apply changed wallpaper");
      struct stat after{};
      require(
          ::stat((fixture.state / "wallpaper.png").c_str(), &after) == 0
              && before.st_ino == after.st_ino
              && before.st_mode == after.st_mode
              && before.st_uid == after.st_uid,
          "rollback lost previous wallpaper inode or metadata"
      );
      require(!std::filesystem::exists(fixture.state / "widget-assets"), "failed apply leaked images/directory");
      require(read(fixture.state / "greeter.toml") == oldAdmin, "failed apply changed administrator config");
      if (existingSync)
        require(read(fixture.state / "sync.toml") == oldSync, "failed apply changed existing Sync");
      else
        require(!std::filesystem::exists(fixture.state / "sync.toml"), "failed apply created Sync");
      require(
          ::stat(fixture.state.c_str(), &after) == 0 && (after.st_mode & 0777) == 0700,
          "rollback changed directory mode"
      );
      for (const auto& entry : std::filesystem::directory_iterator(fixture.state))
        require(!entry.path().filename().string().starts_with('.'), "rollback leaked a temporary or backup");
    }
  }

  void testRejections() {
    for (const auto* suffix :
         {"[session]\nlast='injected'\n", "[session.power]\nshutdown='injected'\n", "[user]\ndefault='root'\n",
          "[clock]\nenabled=false\n", "[layout.clock]\nvisible=false\n", "[layout.login]\nvisible=false\n",
          "[layout.outputs.DP-1.login]\nvisible=false\n", "[layout.outputs.DP-1.login]\nwidth=20\n",
          "[layout.outputs.DP-1.outputs.DP-2.login]\nx=0\n", "[layout.outputs.DP-1.clock]\nvisible=true\n",
          "[layout.login]\noutput='DP-1'\n", "[layout.login]\nwidth=32\n", "[layout.login]\nscale=5\n",
          "[layout.branding]\nflip_x=1\n", "[widgets]\nwidget_order='bad'\n", "[assets]\nimages=['../image.png']\n",
          "[widgets]\nwidget_order=['button']\n[widgets.widget.button]\ntype='button'\nplacement='anchor'\nanchor='"
          "center'\n[widgets.widget.button.settings]\ncommand='injected'\n"}) {
      Fixture fixture;
      const auto before = read(fixture.state / "sync.toml");
      write(fixture.staging / "sync.toml", fixture.appearance() + suffix);
      std::string error;
      require(!fixture.apply(error) && !error.empty(), "unsafe payload accepted: " + std::string(suffix));
      require(read(fixture.state / "sync.toml") == before, "invalid input changed state");
    }
    for (const auto* addition :
         {"[appearance]\nfont_family=1\n", "[appearance]\nhide_logo=true\n", "[appearance.wallpaper]\nblur=nan\n",
          "[appearance.wallpaper]\nunknown=true\n"}) {
      Fixture fixture;
      auto payload = fixture.appearance();
      if (std::string_view(addition).starts_with("[appearance]\n"))
        payload = std::string(addition) + payload.substr(payload.find("[appearance.palette]"));
      else
        payload += addition;
      write(fixture.staging / "sync.toml", payload);
      std::string error;
      require(!fixture.validate(error), "unsupported appearance input accepted");
    }
    Fixture fixture;
    const auto hash = fixture.image();
    const auto name = "widget-" + hash + ".png";
    auto payload = fixture.appearance() + fixture.widgets(hash, true);
    write(fixture.staging / "sync.toml", payload);
    write(fixture.staging / name, "corrupt PNG");
    std::string error;
    require(!fixture.validate(error), "disabled Sticker bypassed image validation");
    const auto validHash = fixture.image(23);
    payload = fixture.appearance() + fixture.widgets(validHash);
    std::filesystem::remove(fixture.staging / name);
    write(fixture.staging / "sync.toml", payload);
    require(fixture.validate(error), "valid managed payload rejected: " + error);
    const auto source = fixture.staging / ("widget-" + validHash + ".png");
    std::filesystem::create_hard_link(source, fixture.root / "other.png");
    require(!fixture.validate(error), "hard-linked staged PNG admitted");
    std::filesystem::remove(fixture.root / "other.png");
    std::filesystem::rename(source, fixture.root / "other.png");
    std::filesystem::create_symlink(fixture.root / "other.png", source);
    require(!fixture.validate(error), "symlinked staged PNG admitted");
  }

  void testManifestAndLiveState() {
    Fixture fixture;
    const auto hash = fixture.image();
    const auto payload = fixture.appearance() + fixture.widgets(hash);
    write(fixture.staging / "sync.toml", payload);
    std::string error;
    const auto png = fixture.staging / ("widget-" + hash + ".png");
    std::filesystem::remove(png);
    require(!fixture.validate(error), "missing staged image admitted");
    fixture.image();
    const auto extra = fixture.image(19);
    require(!fixture.validate(error), "unlisted staged image admitted");
    std::filesystem::remove(fixture.staging / ("widget-" + extra + ".png"));
    write(fixture.state / "sync.toml", "[widgets]\nwidget_order='invalid'\n");
    require(
        !fixture.apply(error) && read(fixture.state / "sync.toml").contains("invalid"), "invalid live Sync overwritten"
    );
    write(fixture.state / "sync.toml", "[session]\nlast='saved'\n");
    std::filesystem::create_directory(fixture.state / "outside");
    std::filesystem::create_directory_symlink(fixture.state / "outside", fixture.state / "widget-assets");
    require(
        !fixture.apply(error) && std::filesystem::is_empty(fixture.state / "outside"),
        "image directory symlink followed"
    );
    std::filesystem::remove(fixture.state / "widget-assets");
    std::filesystem::create_directory(fixture.state / "widget-assets");
    write(fixture.state / "widget-assets" / (hash + ".png"), "corrupt existing image");
    require(
        !fixture.apply(error) && read(fixture.state / "widget-assets" / (hash + ".png")) == "corrupt existing image",
        "existing corrupt hash-named image overwritten"
    );
  }
} // namespace

extern "C" int __real_rename(const char*, const char*);
extern "C" int __wrap_rename(const char* source, const char* destination) {
  if (!failCommitPath.empty() && failCommitPath == destination) {
    errno = ENOSPC;
    return -1;
  }
  return __real_rename(source, destination);
}

int main() {
  try {
    testInstallAndReset();
    testImageRetention();
    testImageQuotas();
    testRollback();
    testRejections();
    testManifestAndLiveState();
    std::puts("customization sync: validation, install, preservation and rollback passed");
    return 0;
  } catch (const std::exception& error) {
    std::println(stderr, "customization sync: {}", error.what());
    return 1;
  }
}
