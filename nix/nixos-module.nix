{
  lib,
  pkgs,
  config,
  options,
  ...
}:
let
  cfg = config.services.displayManager.noctalia-greeter;
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
  options.services.displayManager.noctalia-greeter = {
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

    passwordless-sync-users = lib.mkOption {
      type = with lib.types; listOf str;
      default = [ ];
      description = ''
        Local users allowed to apply the constrained appearance-only sync through
        Polkit without authentication. The constrained sync operation never accepts
        session command configuration. Leave empty to require administrator
        authentication for every sync; that workflow remains fully supported.
      '';
      example = [ "alice" ];
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
        Full declarative greeter.toml, symlinked into the Nix store and replaced on every
        activation. Configure everything here: session/user defaults, appearance (scheme,
        palette, wallpaper, font, ...), output, idle, cursor, keyboard, auth.
        Sync/UI mutable data lives in sync.toml and is not managed by this option.
        Accepts a Nix attrset, raw TOML string, or path to a `.toml` file.
        See examples/greeter.toml in the noctalia-greeter repository.
      '';
      example = lib.literalExpression ''
        {
          session.default = "niri";
          appearance = {
            scheme = "Synced";
            password_style = "default";
            palette = {
              primary = "#fff59b";
              on_primary = "#0e0e43";
              # … remaining required palette keys …
            };
          };
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
    lib.mkIf cfg.enable (lib.mkMerge [
      {
        environment.systemPackages = [
          cfg.package
        ];

        systemd.tmpfiles.settings."10-noctalia-greeter" =
          {
            "/var/lib/noctalia-greeter".d = {
              inherit user group;
              mode = "0750";
            };
          }
          // lib.optionalAttrs (cfg.settings != { }) {
            "/var/lib/noctalia-greeter/greeter.toml"."L+" = {
              argument = "${generateToml "greeter.toml" cfg.settings}";
              inherit user group;
            };
          };

        services.greetd = {
          enable = lib.mkDefault true;
          settings.default_session.command = lib.mkDefault "${cfg.package}/bin/noctalia-greeter-session -- ${cfg.greeter-args}";
        };

        services.accounts-daemon.enable = lib.mkDefault true;

        security.polkit =
          {
            enable = lib.mkDefault true;
          }
          // lib.optionalAttrs (options.security.polkit ? enablePkexecWrapper) {
            enablePkexecWrapper = lib.mkDefault true;
          };

        assertions = [
          {
            assertion = (config.users.users.${user} or { }) != { };
            message = "noctalia-greeter: user ${user} does not exist. Please create it before referencing it.";
          }
          {
            assertion = lib.all (name: builtins.hasAttr name config.users.users) cfg.passwordless-sync-users;
            message = "noctalia-greeter: every passwordless sync user must be a configured system user.";
          }
        ];
      }

      (lib.mkIf (cfg.passwordless-sync-users != [ ]) {
        security.polkit.extraConfig = lib.mkAfter ''
          polkit.addRule(function(action, subject) {
            var allowedUsers = ${builtins.toJSON cfg.passwordless-sync-users};
            if (action.id == "org.noctalia.greeter.sync-appearance" &&
                action.lookup("program") == "${cfg.package}/bin/noctalia-greeter-apply-appearance" &&
                action.lookup("user") == "root" &&
                subject.local && subject.active &&
                allowedUsers.indexOf(subject.user) >= 0) {
              return polkit.Result.YES;
            }
          });
        '';
      })
    ]);
}
