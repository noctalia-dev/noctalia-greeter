{
  lib,
  pkgs,
  config,
  ...
}: let
  cfg = config.programs.noctalia-greeter;
  user = config.services.greetd.settings.default_session.user;
in {
  options.programs.noctalia-greeter = {
    enable = lib.mkEnableOption "Whether to enable Noctalia Greeter, A minimal login greeter for greetd.";

    package = lib.mkOption {
      type = lib.types.nullOr lib.types.package;
      description = "The noctalia-greeter package to use.";
    };
    
    settings = lib.mkOption {
      type = types.lines;
      description = "Configuration options for the Greeter.";
    };
  };

  config = lib.mkIf cfg.enable {
    environment.systemPackages = [
      cfg.package
      pkgs.cage
      pkgs.wlr-randr
    ];

    greeter-config = pkgs.runCommand "greeter.conf" { } ''
      cp ${pkgs.writeText "greeter.conf" cfg.settings} "/var/lib/noctalia-greeter/greeter.conf"
    '';
    
    security.polkit.enable = true;
    services.dbus.packages = [ cfg.package ];
    
    systemd.tmpfiles.settings."10-noctalia-greeter" = {
      "/var/lib/noctalia-greeter".d = {
        inherit user;
        group = if config.users.users.${user}.group != "" then config.users.users.${user}.group else "greeter";
        mode = "0750";
      };
    };

    services.greetd = {
      enable = lib.mkDefault true;
      settings.default_session.command = lib.mkDefault "${cfg.package}/bin/noctalia-greeter-session";
    };

    assertions = [
      {
        assertion = (config.users.users.${user} or { }) != { };
        message = "noctalia-greeter: user ${user} does not exist. Please create it before referencing it.";
      }
    ];
  };
}
