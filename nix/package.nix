{
  lib,
  config,
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
  libxkbcommon,
  libwebp,
  glib,
  librsvg,
  jemalloc,
  autoAddDriverRunpath,
}: let
  inherit (builtins) head match readFile;
  version = head (match ".*version: '([^']+)'.*" (readFile ../meson.build));
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
      libxkbcommon
      libwebp
      glib
      librsvg
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
