# Ask

![Ask answering a question about a selection and a window](docs/answer.png)

**Ask is a fast, lightweight app for Omarchy that gives you quick AI access.** One hotkey opens a small popup on top of whatever you are doing. Type a question, or let Ask pick up the text you selected and the window you are looking at, and the answer streams in. Press the hotkey again to get rid of it. Think of it as a plugin-like companion to the Omarchy desktop, always one keypress away.

Ask runs on [`fx`](https://github.com/vercel-labs/fx), so it uses whatever account and models you already set up there. It is inspired by Raycast AI and is not affiliated with Raycast.

## Why it is fast and light

- **A resident process.** Ask starts once, hidden, and stays. Each hotkey press is a single local-socket message to that warm process, not a program launch or `uwsm` unit spawn.
- **One small native binary.** C++17 and Qt Quick, linked against the Qt and `layer-shell-qt` packages Omarchy already has. No Electron, no web view, no bundled runtime.
- **Wayland overlay layer.** On Hyprland, Ask sits on `wlr-layer-shell`'s overlay layer so it appears centered on your active monitor above fullscreen windows and special workspaces.
- **Zero idle polling.** Rounding changes arrive over Hyprland's event socket, theme changes come from file watches, and streaming tokens coalesce onto frame ticks.

## What it does

- **Ask anything.** Follow-ups keep the conversation. `Esc` hides the window and the chat waits for you.
- **Selected text.** Highlight text anywhere, press the hotkey, and it appears as a chip under the input. It goes into the question inside an unbreakable dynamic Markdown fence.
- **Window and region screenshots.** Ask offers the window you were in as a chip (`Ctrl+S` captures just that window's buffer via `grim -T`). Press `Ctrl+Shift+S` to select any screen region with `slurp` + `grim -g`.
- **Images and files.** Paste an image or clipboard text with `Ctrl+V`, drop images or UTF-8 text/code files onto the window, or pick files natively via `xdg-desktop-portal` (`Ctrl+O`).
- **Code blocks that work.** Fenced code blocks render with their language label and a one-click `Copy` button (`Ctrl+Shift+Y` copies the latest code block from the keyboard).
- **Turn controls.** Regenerate the last answer (`Ctrl+R`), pull the last question back into the input to edit (`Ctrl+E`), or retry a failed request without losing your attachments.
- **AI commands + custom commands.** With a selection attached, the empty state offers Summarize, Explain This, Fix Spelling and Grammar, Improve Writing, Make Shorter, Translate (localized to your Omarchy language alongside English), and Explain Code. Add your own in `~/.config/ask/commands.json`.
- **Models, reasoning effort, and fast mode.** `Ctrl+K` switches models (`fx models --json`), adjusts reasoning effort (`low`, `medium`, `high`, `max`) when supported, and toggles Fast mode. Choices persist in `~/.config/ask/settings.json`.
- **Recent chats.** `Ctrl+H` reopens or deletes recent conversations (`~/.local/state/ask/chats/index.json`, bounded to 30 chats with automatic workspace cleanup).
- **Headless and scripted CLI.** `ask query` streams answers to `stdout` (or emits `--json`) from prompts, piped `stdin`, `--selection`, `--file`, and `--command`, without starting Wayland or QML.
- **Follows Omarchy.** Full dark and light `colors.toml` palettes, Hyprland corner rounding, and live Omarchy language switching (English and Danish included).

| Selection and window chips | Model picker |
| --- | --- |
| ![Chips under the input](docs/screen.png) | ![Model picker](docs/models.png) |

## Requirements

- Omarchy, with Hyprland 0.56 or newer
- Qt 6.x (`qt6-base`, `qt6-declarative`, `qt6-wayland`) and `layer-shell-qt`
- [`fx`](https://github.com/vercel-labs/fx), installed and signed in (`PATH` or `~/.local/bin/fx`). It is not a pacman package.
- [`grim`](https://gitlab.freedesktop.org/emersion/grim) 1.5+ and [`slurp`](https://github.com/emersion/slurp), for window and region screenshots
- `wl-clipboard`, for primary selection
- `xdg-desktop-portal`, for native file picking

## Install

```sh
git clone <this repository> ask
cd ask
bin/install        # builds, then makepkg -fsi, so Ask shows up in the launcher
```

`bin/build` alone produces `build/ask` without installing anything.

## Hyprland setup

Add these to `~/.config/hypr/bindings.lua` (or your Omarchy Hyprland Lua config):

```lua
o.bind("SUPER + PERIOD", "Ask AI", "ask")
o.launch_on_start("ask --background")
```

Passing `"ask"` directly (rather than `{ launch = "ask" }`) avoids spawning a transient `uwsm-app` systemd unit on every toggle keypress. Because Ask uses `wlr-layer-shell`, no window rule is needed on Wayland; if you run with `ASK_NO_LAYER_SHELL=1`, add `o.window("ask", { float = true, center = true, size = { 750, 475 } })`.

## CLI usage

```sh
ask                                         # Toggle the popup window
ask --background                            # Start the resident instance hidden
ask open --new --file notes.md "Summarize"  # Open the popup with files or a prompt
ask open --command summarize                # Open the popup and run an AI command
ask query "Explain TCP slow start"          # Stream Markdown to stdout (no GUI)
git diff | ask query "Write a commit msg"   # Pipe stdin into a headless query
ask query --selection --command fix --json  # Structured JSON output for scripts/agents
```

Run `ask --help` for every flag (`--model`, `--effort`, `--fast`, `--no-save`, `--file`, `--selection`, `--command`, `--json`).

## Custom commands

Create `~/.config/ask/commands.json` to add commands to the empty state, `Ctrl+K` actions panel, and `--command <id>`:

```json
[
  {
    "id": "review",
    "title": "Find Bugs",
    "prompt": "Review the selected code for bugs, races, and edge cases.",
    "requiresSelection": true
  }
]
```

## Keys

| Key | Action |
| --- | --- |
| Hotkey (yours) | Show, or hide if already visible |
| Enter | Send |
| Shift+Enter | New line |
| Esc | Close the open panel, else stop generating, else hide the window |
| Ctrl+K | Actions panel: commands, models, effort, fast mode, history |
| Ctrl+H | Recent chats (`Ctrl+D` or `Del` deletes the highlighted chat) |
| Ctrl+S | Attach or remove the active window screenshot |
| Ctrl+Shift+S | Capture a screen region |
| Ctrl+O | Attach image or text/code files |
| Ctrl+V | Paste an image (or clipboard text when input is empty) |
| Backspace (empty input) | Remove the last chip |
| Up (empty input) | Recall the last question, or pick an example or AI command |
| Ctrl+R | Regenerate the last answer |
| Ctrl+E | Edit the last question |
| Ctrl+N | New chat |
| Ctrl+Shift+C | Copy the last answer |
| Ctrl+Shift+Y | Copy the last fenced code block |
| Ctrl+Q | Quit |
| F1 or Ctrl+/ | Shortcuts overlay |

Ask takes text, so it does not use the usual Omarchy single-key shortcuts (`Q`, Space, `?`).

### Not supported

Ask does not paste its answer back into the app you came from. That needs synthetic keyboard input, which is unreliable across terminals and browsers. Copy the answer (`Ctrl+Shift+C`) or code block (`Ctrl+Shift+Y`) instead.

## Privacy

Ask sends nothing anywhere itself. Everything you ask goes through `fx` to the provider you configured there.

- **Local socket.** Scoped to your user account (`QLocalServer::UserAccessOption`).
- **Selected text.** Read from primary selection via `wl-paste` (bounded to 20,000 characters and 150 ms) only when opening the popup or when `ask query --selection` asks for it. Never written to disk until sent in a chat you keep.
- **Screenshots and pasted images.** Saved in `$XDG_RUNTIME_DIR/ask/` (mode `0700`, files `0600`) and deleted as soon as a successful request finishes, when removed, on New Chat, or on quit. Failed requests keep your chips attached so you can retry without recapturing.
- **Attached files.** Read in place (UTF-8 text up to 64 KiB, images passed via `--image`). Ask never modifies or deletes your originals.
- **Chats and settings.** Recent chats live in `~/.local/state/ask/chats/` (capped at 30 conversations; older and orphaned workspaces are pruned). `~/.config/ask/settings.json` stores your chosen model, effort, and fast-mode preference.

External text (selections, attached files, window titles) and model output are treated as untrusted: prompts isolate untrusted text in collision-proof dynamic fences with explicit system-prompt framing, chips render as plain text, and Markdown/HTML images are stripped before rendering so answers never make network requests.

## Development

```sh
bin/build          # build/ask
bin/test           # Qt Test, offscreen, with fake fx, grim, slurp, wl-paste and hyprctl
bin/screenshots    # rebuilds docs/*.png from the real QML, offscreen, with made-up content
```

Set `ASK_TRACE=1` to print startup and toggle timings to `stderr`, `ASK_INSTANCE=<name>` to run an isolated instance alongside an installed one, and `ASK_NO_LAYER_SHELL=1` to force standard floating window management on Wayland.

## License

MIT, see [LICENSE](LICENSE). Third-party software and trademarks are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Acknowledgements

- [fx](https://github.com/vercel-labs/fx) by Vercel Labs does the actual asking.
- [Omarchy](https://omarchy.org/) and its apps Omacut, Monologue, and Hype by David Heinemeier Hansson, whose theme, portal, CLI, translation, and build patterns Ask adapts.
- [grim](https://gitlab.freedesktop.org/emersion/grim), [slurp](https://github.com/emersion/slurp), [wl-clipboard](https://github.com/bugaevc/wl-clipboard), [layer-shell-qt](https://invent.kde.org/plasma/layer-shell-qt), [Hyprland](https://github.com/hyprwm/Hyprland), and [Qt](https://www.qt.io/).
- Raycast, for showing how good a quick-ask popup can feel. Raycast is a trademark of Raycast Technologies Ltd, and Ask is not affiliated with it.
- Built with [Claude Code](https://claude.com/claude-code).
