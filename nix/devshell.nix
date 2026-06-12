{
  pkgs,
  noctalia-greeter,
}:
pkgs.mkShell {
  inputsFrom = [noctalia-greeter];

  nativeBuildInputs = with pkgs; [
    # Workflow & Hooks
    just
    lefthook

    # Formatting & linting (required by Justfile)
    llvmPackages_22.clang-tools
    cppcheck
    gnugrep
    gnused
    findutils

    # Debugging
    gdb
  ];

  shellHook = ''
    echo " Noctalia greeter dev-shell | 'just --list' to see available tasks"
  '';
}
