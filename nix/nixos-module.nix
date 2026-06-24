{
  lib,
  pkgs,
  config,
  ...
}:
let
  cfg = config.programs.noctalia-greeter;
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

    settings.cursor = {
      theme = lib.mkOption {
        type = lib.types.nullOr lib.types.str;
        default = null;
        example = "Adwaita";
        description = ''
          Cursor theme name for the greeter.
        '';
      };

      size = lib.mkOption {
        type = lib.types.nullOr lib.types.int;
        default = null;
        example = 24;
        description = "Cursor size for the greeter.";
      };

      package = lib.mkOption {
        type = lib.types.nullOr lib.types.package;
        default = null;
        example = lib.literalExpression "pkgs.adwaita-icon-theme";
        description = ''
          Package providing the cursor theme.
        '';
      };
    };

    settings.keyboard = {
      layout = lib.mkOption {
        type = lib.types.nullOr lib.types.str;
        default = config.services.xserver.xkb.layout or null;
        example = "us";
        description = "Keyboard layout for the greeter.";
      };

      variant = lib.mkOption {
        type = lib.types.nullOr lib.types.str;
        default = config.services.xserver.xkb.variant or null;
        example = "colemak";
        description = "Keyboard variant for the greeter.";
      };

      options = lib.mkOption {
        type = lib.types.nullOr lib.types.str;
        default = config.services.xserver.xkb.options or null;
        example = "grp:win_space_toggle";
        description = "Keyboard options for the greeter.";
      };
    };
  };

  config =
    let
      user = config.services.greetd.settings.default_session.user;
      cursor = cfg.settings.cursor;
      keyboard = cfg.settings.keyboard;
      cursorEnv =
        lib.optional (cursor.theme != null) "XCURSOR_THEME=${cursor.theme}"
        ++ lib.optional (cursor.size != null) "XCURSOR_SIZE=${toString cursor.size}"
        ++ lib.optional (cursor.package != null) "XCURSOR_PATH=${cursor.package}/share/icons"
        ++ lib.optional (keyboard.layout != null) "XKB_DEFAULT_LAYOUT=${toString keyboard.layout}"
        ++ lib.optional (keyboard.variant != null) "XKB_DEFAULT_VARIANT=${toString keyboard.variant}"
        ++ lib.optional (keyboard.options != null) "XKB_DEFAULT_OPTIONS=${toString keyboard.options}";
      envPrefix = lib.optionalString (
        cursorEnv != [ ]
      ) "${pkgs.coreutils}/bin/env ${lib.concatStringsSep " " cursorEnv} ";
    in
    lib.mkIf cfg.enable {
      environment.systemPackages = [
        cfg.package
      ];

      systemd.tmpfiles.settings."10-noctalia-greeter" = {
        "/var/lib/noctalia-greeter".d = {
          inherit user;
          group =
            if config.users.users.${user}.group != "" then config.users.users.${user}.group else "greeter";
          mode = "0750";
        };
      };

      services.greetd = {
        enable = lib.mkDefault true;
        settings.default_session.command = lib.mkDefault "${envPrefix}${cfg.package}/bin/noctalia-greeter-session -- ${cfg.greeter-args}";
      };

      assertions = [
        {
          assertion = (config.users.users.${user} or { }) != { };
          message = "noctalia-greeter: user ${user} does not exist. Please create it before referencing it.";
        }
      ];
    };
}
