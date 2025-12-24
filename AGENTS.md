# AI Agents

This project uses specialized AI agents for development. See [CLAUDE.md](CLAUDE.md#ai-agents) for full documentation.

## Quick Reference

| Agent | Purpose | Invoke With |
|-------|---------|-------------|
| **Project Manager** | Issue tracking, releases | "What work is ready?" |
| **TFT Designer** | Visual design, colors, accessibility | "Design a screen for [feature]" |
| **ESP8266 Developer** | Feature implementation | "Help me implement [feature]" |
| **Release Manager** | Firmware validation | "Validate the firmware for release" |
| **Documentation** | Keep docs current | "Update documentation for [feature]" |

Agent files are in `.claude/agents/`.

## TFT Designer Agent

The TFT Designer creates beautiful, accessible designs for the 240x240 TFT display.

**Capabilities**:
- Generate color palettes using [Colormind AI](http://colormind.io/api/)
- Verify color accessibility with [Colour Contrast Checker](https://colourcontrast.cc/)
- Design screen layouts optimized for 240x240 pixels
- Convert colors to/from RGB565 format
- Test designs on physical device before release

**Invoke for**:
- New screen types or layouts
- Color scheme updates
- Accessibility audits
- Theme modifications (dark/light mode)

```bash
# Example invocations
"Design a countdown screen"
"Generate an accessible color palette"
"Check if 0x07FF on 0x0841 is accessible"
"Create a layout for [feature]"
```

## Agent Workflow

### For Visual Features

```
Project Manager → TFT Designer → ESP8266 Developer → TFT Designer → Release Manager → Documentation
    (plan)         (design)       (implement)         (review)        (validate)       (document)
```

### Standard Development

```
Project Manager → ESP8266 Developer → Release Manager → Documentation
    (plan)          (implement)         (validate)       (document)
```

## Issue Tracking

```bash
bd ready              # Find available work
bd update <id> --status in_progress  # Claim work
bd close <id>         # Complete work
bd sync               # Sync with git
```

## Session Completion (MANDATORY)

```bash
git status && git add . && bd sync && git commit -m "..." && bd sync && git push
```

Work is NOT complete until `git push` succeeds.
