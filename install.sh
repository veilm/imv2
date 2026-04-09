#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${repo_root}/build-wayland"
prefix="/usr/local"

cd "${repo_root}"

if [[ -d "${build_dir}" ]]; then
  meson setup "${build_dir}" --reconfigure -Dwindows=wayland --prefix="${prefix}"
else
  meson setup "${build_dir}" -Dwindows=wayland --prefix="${prefix}"
fi

ninja -C "${build_dir}"

# Use Meson's install step so binaries, config, desktop entries, and man pages
# stay in sync with the build definition.
if [[ -w "${prefix}" ]]; then
  meson install -C "${build_dir}" --no-rebuild
elif command -v sudo >/dev/null 2>&1; then
  sudo meson install -C "${build_dir}" --no-rebuild
else
  printf 'Need write access to %s\n' "${prefix}" >&2
  exit 1
fi

printf 'Installed imv under %s\n' "${prefix}"
