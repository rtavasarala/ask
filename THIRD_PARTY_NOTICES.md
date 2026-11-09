# Third-party notices

Ask is MIT licensed (see `LICENSE`). It builds on, and works with, the software below. None of it is copied into this repository except the scaffolding noted for Omarchy.

## Used at runtime as separate programs

These are not bundled or linked. Ask starts them as external commands, so their licenses apply to them and not to Ask.

| Software | Used for | Author | License |
| --- | --- | --- | --- |
| [fx](https://github.com/vercel-labs/fx) | Runs every question (`fx ask`) and lists models (`fx models`) | Vercel Labs | Apache-2.0, [LICENSE](https://github.com/vercel-labs/fx/blob/main/LICENSE) |
| [grim](https://gitlab.freedesktop.org/emersion/grim) | Captures a single window (`grim -T`) | Simon Ser | MIT |
| [wl-clipboard](https://github.com/bugaevc/wl-clipboard) | Reads the primary selection (`wl-paste --primary`) | Sergey Bugaev and contributors | GPL-3.0-or-later |
| [Hyprland](https://github.com/hyprwm/Hyprland) | Names the active window through its IPC socket and `hyprctl`; Ask reads its rounding setting | The Hyprland contributors (hyprwm) | BSD-3-Clause |

## Linked libraries

| Software | Used for | Author | License |
| --- | --- | --- | --- |
| [Qt 6](https://www.qt.io/) (Core, Gui, Qml, Quick, Quick Controls 2, Network) | The application, dynamically linked from the system package | The Qt Company and contributors | LGPL-3.0. Because Ask links Qt dynamically, you can replace Qt with another compatible build. |

## Omarchy

Ask is made for [Omarchy](https://omarchy.org/). The theme watching (`src/theme.cpp`), the translation loader (`src/translator.cpp`), and the build scaffolding (`bin/`, the PKGBUILD layout) are adapted from the Omarchy apps Omacut and Monologue and from the `omarchy-app` templates.

Copyright (c) David Heinemeier Hansson. MIT licensed. The attribution is repeated in `LICENSE`.

## Fonts and icons

Ask bundles no fonts and no third-party icons. Text uses the fonts of your system. The "✦" mark is a Unicode character drawn by your system font. The application icon (`pkgbuild/ask.svg`) was drawn for this project and is covered by the project's MIT license.

## Trademarks

Raycast is a trademark of Raycast Technologies Ltd. Ask is an independent project inspired by the way Raycast AI works. It is not affiliated with, endorsed by, or sponsored by Raycast. Omarchy, Hyprland, Vercel, fx, and Qt are names of their respective owners and are used here only to say what Ask works with.

## How it was made

Ask was built with [Claude Code](https://claude.com/claude-code), Anthropic's coding agent, working from a human's design and review.
