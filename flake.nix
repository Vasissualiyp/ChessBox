# SPDX-License-Identifier: GPL-3.0-or-later
{
  description = "ChessBox - a generalizable FOSS sandbox for chess-like variants";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin" ];
      forAll = f: nixpkgs.lib.genAttrs systems (system: f nixpkgs.legacyPackages.${system});
    in {
      devShells = forAll (pkgs:
        let
          # Toolchain shared by every shell. gcc and clang are both present
          # because the exit gate builds with both (docs/plan/00-roadmap.md).
          core = with pkgs; [
            gcc cmake ninja ccache
            clang-tools llvmPackages.clang
            gdb lcov gcovr
            catch2_3 tomlplusplus
            python3 git
          ] ++ lib.optionals stdenv.hostPlatform.isLinux [ valgrind ];

          gfx = with pkgs; [
            sdl3 vulkan-headers vulkan-loader vulkan-validation-layers
            vulkan-tools shaderc spirv-tools vulkan-memory-allocator
          ];

          mkShell = extra: pkgs.mkShell {
            packages = core ++ extra;
            # Debug builds are -O0, and _FORTIFY_SOURCE emits a #warning there -
            # which is fatal under -Werror. Drop the hardening flag in the shell
            # rather than weakening our own warning settings.
            hardeningDisable = [ "fortify" "fortify3" ];
            # Catch2/toml++ come from nix, not FetchContent: builds stay hermetic.
            CMAKE_PREFIX_PATH = "${pkgs.catch2_3}:${pkgs.tomlplusplus}";
            shellHook = ''
              export CCACHE_DIR="$PWD/.cache/ccache"
              echo "ChessBox dev shell. See AGENTS.md. Presets: dev asan tsan release coverage bench"
            '';
          };
        in {
          default = mkShell [ ];
          gfx = mkShell gfx;
        });

      packages = forAll (pkgs: {
        default = pkgs.stdenv.mkDerivation {
          pname = "chessbox";
          version = "0.0.0";
          src = self;
          nativeBuildInputs = with pkgs; [ cmake ninja ];
          buildInputs = with pkgs; [ catch2_3 tomlplusplus ];
          cmakeBuildType = "Release";
          doCheck = true;
          checkPhase = "ctest --output-on-failure -L 'unit|property|arch'";
          meta = with pkgs.lib; {
            description = "Generalizable FOSS sandbox for chess-like variants";
            license = licenses.gpl3Plus;
            platforms = platforms.unix;
          };
        };
      });

      checks = forAll (pkgs: {
        # `nix flake check` is the gate. Keep it honest: it builds and tests.
        build = self.packages.${pkgs.system}.default;
      });

      formatter = forAll (pkgs: pkgs.nixpkgs-fmt);
    };
}
