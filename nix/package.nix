{
  lib,
  stdenv,
  meson,
  ninja,
  pkg-config,
  wayland-scanner,
  wayland,
  wayland-protocols,
  wlroots_0_20,
  libGL,
  libglvnd,
  freetype,
  fontconfig,
  cairo,
  pango,
  harfbuzz,
  libxkbcommon,
  libwebp,
  glib,
  librsvg,
  jemalloc,
  tomlplusplus,
  nlohmann_json,
  stb,
  fetchFromGitHub,
}: let
  inherit (builtins) head match readFile;
  version = head (match ".*version: '([^']+)'.*" (readFile ../meson.build));
  stb' = stb.overrideAttrs (_: {
    version = "unstable-2025-10-26";
    src = fetchFromGitHub {
      owner = "nothings";
      repo = "stb";
      rev = "f1c79c02822848a9bed4315b12c8c8f3761e1296";
      hash = "sha256-BlyXJtAI7WqXCTT3ylww8zoG0hBxaojJnQDvdQOXJPE=";
    };
  });
in
  stdenv.mkDerivation {
    pname = "noctalia-greeter";
    inherit version;

    src = lib.cleanSource ./..;

    postPatch = ''
      # Remove -march=native and -mtune=native for reproducible builds
      sed -i "s/'-march=native', '-mtune=native',//" meson.build
    '';

    nativeBuildInputs = [
        meson
        ninja
        pkg-config
        wayland-scanner
        jemalloc
    ];

    buildInputs = [
      wayland
      wayland-protocols
      wlroots_0_20
      libGL
      libglvnd
      freetype
      fontconfig
      cairo
      pango
      harfbuzz
      libxkbcommon
      libwebp
      glib
      librsvg
      tomlplusplus
      nlohmann_json
      stb'
    ];

    mesonBuildType = "release";

    ninjaFlags = ["-v"];

    meta = with lib; {
      description = "Noctalia Greeter - A minimal login greeter for greetd that matches the look and feel of Noctalia Shell";
      homepage = "https://github.com/noctalia-dev/noctalia-greeter";
      license = licenses.mit;
      platforms = platforms.linux;
      mainProgram = "noctalia-greeter";
    };
  }
