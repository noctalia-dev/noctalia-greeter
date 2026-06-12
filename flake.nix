{
  description = "Noctalia Greeter - A minimal login greeter for greetd that matches the look and feel of Noctalia Shell";

  inputs = {
    nixpkgs.url = "https://channels.nixos.org/nixos-unstable/nixexprs.tar.xz";
  };

  outputs = {
    self,
    nixpkgs,
  }: let
    inherit (nixpkgs.lib) genAttrs;

    systems = [
      "x86_64-linux"
      "aarch64-linux"
    ];

    forEachSystem = perSystem:
      genAttrs systems (
        system: let
          pkgs = nixpkgs.legacyPackages.${system};
        in
          perSystem {inherit pkgs system;}
      );
  in {
    overlays.default = final: prev: {
      noctalia-greeter = final.callPackage ./nix/package.nix {};
    };

    packages = forEachSystem (
      {pkgs, ...}: {
        default = pkgs.callPackage ./nix/package.nix {};
      }
    );

    devShells = forEachSystem (
      {
        pkgs,
        system,
      }: {
        default = pkgs.callPackage ./nix/devshell.nix {
          noctalia-greeter = self.packages.${system}.default;
        };
      }
    );

    nixosModules.default = {
      pkgs,
      lib,
      ...
    }: {
      imports = [./nix/nixos-module.nix];
      programs.noctalia-greeter.package = lib.mkDefault self.packages.${pkgs.stdenv.hostPlatform.system}.default;
    };
  };
}
