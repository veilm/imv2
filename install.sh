#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
build_dir="${repo_root}/build-wayland"
target="/usr/local/bin/imv"

cd "${repo_root}"

if [[ -d "${build_dir}" ]]; then
  meson setup "${build_dir}" --reconfigure -Dwindows=wayland
else
  meson setup "${build_dir}" -Dwindows=wayland
fi

ninja -C "${build_dir}"

if [[ -w "$(dirname "${target}")" ]]; then
  install -Dm755 "${build_dir}/imv" "${target}"
elif command -v sudo >/dev/null 2>&1; then
  sudo install -Dm755 "${build_dir}/imv" "${target}"
else
  printf 'Need write access to %s\n' "$(dirname "${target}")" >&2
  exit 1
fi

printf 'Installed %s\n' "${target}"
