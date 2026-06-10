{
  lib,
  pkgs,
  config,
  ...
}: let
  cfg = config.programs.noctalia-greeter;
in {
  options.programs.noctalia-greeter = {
    enable = lib.mkEnableOption "Whether to enable Noctalia Greeter, A minimal login greeter for greetd.";

    package = lib.mkOption {
      type = lib.types.package;
      description = "The noctalia-greeter package to use.";
    };
    
    greeter-args = lib.mkOption {
      type = lib.types.str;
      default = "";
      description = "Arguments to add onto noctalia-greeter-session command.";
    };
  };

  config = let
      user = config.services.greetd.settings.default_session.user;
    in lib.mkIf cfg.enable {
    environment.systemPackages = [
      cfg.package
    ];

    systemd.tmpfiles.settings."10-noctalia-greeter" =  {
      "/var/lib/noctalia-greeter".d = {
        inherit user;
        group = if config.users.users.${user}.group != "" then config.users.users.${user}.group else "greeter";
        mode = "0750";
      };
    };

    services.greetd = {
      enable = lib.mkDefault true;
      settings.default_session.command = lib.mkDefault "${cfg.package}/bin/noctalia-greeter-session -- ${cfg.greeter-args}";
    };

    assertions = [
      {
        assertion = (config.users.users.${user} or { }) != { };
        message = "noctalia-greeter: user ${user} does not exist. Please create it before referencing it.";
      }
    ];
  };
}
