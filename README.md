# Jarvis

A voice-driven AI agent for macOS that autonomously executes multi-step missions on your computer.

You speak. Jarvis thinks. Tasks get done.

---

## What it does

Jarvis listens for voice commands, reasons about what needs to happen, and executes tools until the job is complete — without you touching the keyboard.

**Example:** *"Open WhatsApp in Chrome, find a contact named X, and send him a message."*
Jarvis handles the browser interaction autonomously via Chrome remote debugging, navigates to the contact, and sends the message.

---

## Architecture

```
Voice Input (STT)
      │
      ▼
 Jarvis Core  ←──── Memory (persistent context file)
      │
      ├── Tool Execution (dylib plugins, loaded/unloaded at runtime)
      │         ├── run_python (Playwright, shell, etc.)
      │         ├── web_search
      │         ├── file I/O
      │         └── ... (extensible)
      │
      ├── Mission Dispatch
      │         └── Background sub-agents that run autonomously,
      │             iterate with a timed interval, and return
      │             attachments to the main context
      │
      └── Voice Output (TTS)
```

### Key design decisions

- **XML-structured LLM output** — Jarvis parses `<speech>`, `<think>`, and `<tool>` blocks from the model's response. The `<think>` block is mandatory before every tool call, enforcing chain-of-thought before action.
- **Mission system** — Jarvis can dispatch background agents with a natural language goal. Each agent runs a tool-execution loop, sleeps between iterations at a self-determined interval, and can pass attachments back.
- **Runtime plugin loading** — Tools are compiled as dylibs and loaded/unloaded at runtime. New capabilities can be added without restarting the agent.
- **Self-extending memory** — Jarvis created its own memory system using the `create_tool` capability. Memory is written to a file and injected into context each session.
- **Mute / text fallback** — Jarvis can be muted via voice command and switched to text input mode on demand.

---

## Stack

- **Language:** C++ (no STL, custom core library)
- **LLM:** Claude (Anthropic API) via custom HTTP client
- **STT:** Whisper (local, runs on-device)
- **TTS:** Local macOS TTS
- **Browser control:** Chrome DevTools Protocol (remote debugging port 9222)
- **Build:** CMake + Ninja

---

## Project structure

```
AI-agent/
├── LLM/          # LLM query abstraction and config
├── Missions/     # Background mission dispatch and agent loop
├── STT/          # Speech-to-text (Whisper wrapper)
├── TTS/          # Text-to-speech client
├── Tools/        # Dynamically loaded tool plugins
├── third_party/  # pugixml, nlohmann/json, etc.
└── main.cpp      # Core agent loop
```

---

## Building

```bash
git clone --recurse-submodules https://github.com/yahya-systems/AI-agent
cd AI-agent
cmake -B build -G Ninja
cmake --build build
```

Requires: macOS, CMake ≥ 3.20, Clang, an `ANTHROPIC_API_KEY` environment variable.

---

## Status

Active development. Core loop, tool execution, mission dispatch, memory, and voice I/O are all functional. Attachment system between missions and core context is in progress.
