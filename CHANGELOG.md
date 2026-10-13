# Changelog

What changed between releases, newest first. The release workflow publishes
the section for the tag it builds, and refuses to build a version that has no
section here (see `RELEASE.md`). Unreleased work goes under `[Unreleased]`.

## [Unreleased]

### Fixed

- **Launching from the command line ignored the executable chosen in the
  settings panel** (`--launch` started whatever the search found); every launch
  now starts the chosen one.
- Without a chosen executable, the settings panel and a launch could pick
  **different files** — they used two searches with different rules. There is
  one now: GOG's own play task first, never a launcher if the game itself is
  there, and for native games no more libraries or Node addons mistaken for the
  game (an Electron game used to resolve to a `package.json`).

## [1.2.0] - 2026-10-09

### Action needed: write your launch options to Steam again

Proton 11 and DXVK 3.0 stopped reading several variables ProtonForge used to
write, so the launch strings it builds have changed. **ProtonForge never writes
to Steam by itself** — launch options you wrote with an earlier version stay in
Steam unchanged, and a game started from Steam keeps using them. Open each game
you configured, close Steam, and click **Write to Launcher**. If you only launch
from ProtonForge, nothing needs doing: a direct launch always uses the current
settings. `--list-games` reports per game whether Steam's stored options are
current (`launchOptionsCurrent`).

### Changed

- **Frame rate limit** is written as `DXVK_CONFIG=dxvk.maxFrameRate=N` (plus
  `VKD3D_FRAME_RATE`). DXVK 3.0 removed `DXVK_FRAME_RATE`, and Valve's Proton
  11 ignores it — the cap had stopped working for D3D8–11 games there.
- **NVAPI** is on by default in every current Proton, so nothing is written
  while it is enabled. Disabling it writes `PROTON_DISABLE_NVAPI=1`;
  `PROTON_ENABLE_NVAPI`, which no current Proton reads, is no longer written.
- **NTSync** is a three-way choice: Proton default, force on
  (`PROTON_USE_NTSYNC`, Proton 10 and older) or off (`PROTON_NO_NTSYNC`,
  Proton 11, where it is on by default).
- **HDR** writes `DXVK_HDR=1` alongside `PROTON_ENABLE_HDR=1` — Proton-CachyOS
  retired the latter. **D7VK** likewise writes `PROTON_D7VK_DDRAW=1`, the name
  Proton-CachyOS reads, alongside `PROTON_USE_D7VK=1`.
- **High Priority** (`PROTON_PRIORITY_HIGH`) is gone: no current Proton reads
  it. A string that has it keeps it as a custom parameter.
- The Proton selector's default is now **Automatic** — Steam's choice for the
  game, else the newest installed Proton — and shows which Proton it resolves
  to. **Latest Proton-CachyOS** is a separate entry that ignores Steam's
  choice, and the "Latest …" entries are only offered for kinds that are
  actually installed.
- **Render presets** describe themselves: each entry names its model and what
  it is for (K: default for DLAA/Quality/Balanced, M: Performance, L: Ultra
  Performance; A–D removed in DLSS 310, E/F deprecated). "LATEST" is now
  called **Recommended**, which is what DLSS 310 made it mean. G/H/I/N/O, which
  were never real models, are no longer offered — a game that has one saved
  keeps it and shows it as "(saved, not in list)".
- **Ray Reconstruction** has its own preset list (D, E and the new F) instead
  of sharing Super Resolution's.

Older launch strings still import: the retired variables are recognised and
rewritten in the new form on the next write.

### Added

- Pin a DLSS version for the DLSS upgrade (`PROTON_DLSS_UPGRADE=310.9.1`,
  Proton-CachyOS and GE-Proton 10-26+).
- Compatibility warnings know the GPU generation (Smooth Motion needs RTX 40
  with driver 580.82+ or RTX 50; Frame Generation RTX 40+; Multi-Frame
  Generation RTX 50) and Valve's own Proton (no d7vk, no DLSS upgrade, no
  descriptor heap in 11.0).
- `--list-games` reports `launchOptionsCurrent`: whether the launch options
  Steam has stored for a game are the ones ProtonForge would write.
- **Dynamic Multi-Frame Generation:** with FG mode "Dynamic", a target frame
  rate and a maximum frame count
  (`NGX_DLSSG_DYNAMIC_TARGET_FRAME_RATE`, `NGX_DLSSG_DYNAMIC_MULTI_FRAME_COUNT_MAX`;
  RTX 50, DXVK-NVAPI 0.9.2+). Support under Proton is still incomplete.
- Ray Reconstruction preset **F** (Ray Reconstruction 2, DLSS 310.9.1+).
- **Smooth Motion: present on graphics queue** (`NVPRESENT_QUEUE_FAMILY=1`)
  makes third-party overlays such as MangoHud work with Smooth Motion, at a
  small performance cost.
- A warning when Smooth Motion and the DLSS Frame Generation override are both
  on — NVIDIA advises against combining them.
- **D3D12 NVIDIA Shader Extensions** (`DXVK_NVAPI_D3D12_NV_SHADER_EXTN=1`,
  experimental): Shader Execution Reordering for DirectX 12 ray and path
  tracing, e.g. in Alan Wake 2, Cyberpunk 2077 and many Unreal Engine 5 games.
  Needs DXVK-NVAPI 0.9.2 and vkd3d-proton 3.0.1 (Valve Proton 11.0-2+).

### Fixed

- **Direct launch picked the wrong Proton:** "Latest Proton-GE" chose
  GE-Proton9-20 over GE-Proton11-7, the default took the *oldest*
  Proton-CachyOS, and Steam's own per-game choice (`proton_11`,
  `proton_experimental`, `proton-stable`, or a custom tool's registered name)
  was never found, so the default was used instead.
- The compatibility warnings now judge the Proton a launch would really use,
  including Steam's per-game choice.
- A `DXVK_CONFIG` or `VKD3D_CONFIG` in Custom Launch Parameters no longer
  silently replaces the frame cap or the descriptor heap; both are merged.
- Foreign `VKD3D_CONFIG` flags imported from Steam (e.g. `dxr`) were dropped
  on the first edit in the settings panel.
- The `.deb` for Debian trixie and Ubuntu 26.04 showed no icons: Qt's SVG
  plugin moved from `libqt6svg6` to `qt6-svg-plugins` there, and the package
  only depended on the former.

## [1.1.0] - 2026-08-11

GOG support: sign in, browse and install GOG games, and launch them through
Proton next to the Steam library. See the GitHub release for the full list.
