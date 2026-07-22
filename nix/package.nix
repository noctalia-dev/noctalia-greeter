{
  lib,
  stdenv,
  fetchurl,
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
    src = fetchurl {
      url = "https://github.com/nothings/stb/archive/904aa67e1e2d1dec92959df63e700b166d5c1022.tar.gz";
      hash = "sha256-h32Bx1qTYIJeLm2Ut5PXqa/+l+Lb7pfIP8hWY8JfDCc=";
    };
  };
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
