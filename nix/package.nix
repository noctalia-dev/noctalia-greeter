{
  lib,
  stdenv,
  fetchFromGitHub,
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
  libjxl,
  glib,
  librsvg,
  libxml2,
  nlohmann_json,
  tomlplusplus,
  stb,
  jemalloc,
}: let
  inherit (builtins) head match readFile;
  version = head (match ".*version: '([^']+)'.*" (readFile ../meson.build));
  # nixpkgs' stb snapshot predates stb_image_resize2.h. Keep using the system
  # package, but update its source to the revision the project previously
  # vendored until nixpkgs updates it.
  stbWithResize2 = stb.overrideAttrs {
    version = "0-unstable-2026-03-13";
    src = fetchFromGitHub {
      owner = "nothings";
      repo = "stb";
      rev = "904aa67e1e2d1dec92959df63e700b166d5c1022";
      hash = "sha256-MXKd03wQV55UITAEgMxrQHXjRhbjZGx7XEHdm1EvimI=";
    };
  };
in
  stdenv.mkDerivation {
    pname = "noctalia-greeter";
    inherit version;

    src = lib.cleanSourceWith {
      src = ./..;
      filter =
        path: type:
        lib.cleanSourceFilter path type
        && !(type == "directory" && builtins.elem (baseNameOf path) [
          "build"
          "build-asan"
          "build-release"
        ]);
    };

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
      libjxl
      glib
      librsvg
      libxml2
      nlohmann_json
      tomlplusplus
      stbWithResize2
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
