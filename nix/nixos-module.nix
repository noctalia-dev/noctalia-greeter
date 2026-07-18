{
  lib,
  pkgs,
  config,
  ...
}:
let
  cfg = config.programs.noctalia-greeter;
  tomlFormat = pkgs.formats.toml { };

  generateToml =
    name: value:
    if lib.isString value then
      pkgs.writeText name value
    else if builtins.isPath value || lib.isStorePath value then
      value
    else
      tomlFormat.generate name value;
in
{
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

    settings = lib.mkOption {
      type =
        with lib.types;
        oneOf [
          tomlFormat.type
          str
          path
        ];
      default = { };
      description = ''
        Settings for noctalia-greeter written to greeter.toml.
        Can be written as:
          - A Nix attrset (converted to TOML via nixpkgs' tomlFormat)
          - A raw TOML string
          - A path to a `.toml` file
      '';
      example = lib.literalExpression ''
        {
          cursor = {
            theme = "Adwaita";
            size = 24;
          };
          keyboard = {
            layout = "us";
          };
        }
      '';
    };
  };

  config =
    let
      user = config.services.greetd.settings.default_session.user;
      group =
        if config.users.users.${user}.group != "" then config.users.users.${user}.group else "greeter";
    in
    lib.mkIf cfg.enable {
      environment.systemPackages = [
        cfg.package
      ];

      systemd.tmpfiles.settings."10-noctalia-greeter" = {
        "/var/lib/noctalia-greeter".d = {
          inherit user group;
          mode = "0750";
        };
      }
      // lib.optionalAttrs (cfg.settings != { }) {
        "/var/lib/noctalia-greeter/greeter.toml".C = {
          argument = "${generateToml "greeter.toml" cfg.settings}";
          inherit user group;
          mode = "0644";
        };
      };

      services.greetd = {
        enable = lib.mkDefault true;
        settings.default_session.command = lib.mkDefault "${cfg.package}/bin/noctalia-greeter-session -- ${cfg.greeter-args}";
      };

      services.accounts-daemon.enable = lib.mkDefault true;

      assertions = [
        {
          assertion = (config.users.users.${user} or { }) != { };
          message = "noctalia-greeter: user ${user} does not exist. Please create it before referencing it.";
        }
      ];
    };
}
