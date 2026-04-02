# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

---

## Build Commands

```bash
# Full clean rebuild (REQUIRED after any user_settings.h change)
make clean && make tlstest

# Rebuild app only (no wolfssl.lib change)
make tlstest

# Build WolfSSL library only
make wolfssl
```

> Use **GNU make**, not wmake. wmake cannot handle subdirectory path inference for flat obj/ targets.
> `user_settings.h` is NOT a tracked Makefile dependency — always `make clean` after changing it.

## Phase Status (as of 2026-04-01)

| Phase | Status | Notes |
|-------|--------|-------|
| 1 — WolfSSL build | ✅ Complete | WOLFSSL.LIB ~410KB |
| 2 — mTCP↔WolfSSL bridge | ✅ Complete | TLSTEST.EXE working, TLS handshake confirmed on real DOS |
| 3 — Gemini protocol layer | 📋 Next | gemini.c/h, url.c/h |
| 4 — Gemtext parser/renderer | 📋 Planned | |
| 5 — TOFU trust | 📋 Planned | |
| 6 — UI/Navigation | 📋 Planned | |
| 7 — Polish | 📋 Planned | Includes fixing heap corruption on exit |

## Testing on DOS VM

Transfer: `python3 -m http.server 8080` on Linux, then on DOS:
```
C:\NOSGEM> C:\NET\HTGET.EXE -o TLSTEST.EXE http://192.168.0.97:8080/TLSTEST.EXE
C:\NOSGEM> TLSTEST.EXE
```
DOS VM IP: 192.168.0.156. Linux IP: 192.168.0.97.

## Known Issues

- `End: heap is corrupted!` on exit — WolfSSL teardown in 16-bit mode. Does not affect TLS session. Deferred to Phase 7.

---

## C89 Rules (strictly enforced by OpenWatcom)

- No `//` line comments — use `/* */` only
- All variable declarations must appear at the top of a block, before any statements
- No `stdint.h` — use explicit types (`unsigned char`, `unsigned int`, `unsigned long`)
- No VLAs, no designated initializers, no compound literals

---

# NOS-DOS Gemini Client — Build Plan for Claude Code

## Project Overview

```
Project:     NOS-DOS Gemini Client (working name: NOSgem)
Language:    C (ANSI C / C89 compatible)
Toolchain:   OpenWatcom C (wcc for 16-bit, wcc386 if memory requires)
Platform:    DOS (real mode or DOS/4GW extender)
Networking:  mTCP (already integrated in NOS-DOS)
TLS:         WolfSSL with custom I/O callbacks
Goal:        A working text-mode Gemini protocol browser for DOS
```

---

## Phase 1: Foundation — WolfSSL + OpenWatcom Build

**Objective:** Get WolfSSL compiling as a static library under OpenWatcom with a minimal feature set suitable for Gemini TLS connections.

### Tasks:
1. Download WolfSSL source (GPLv2 release)
2. Create `user_settings.h` with minimal defines:
   - `WOLFSSL_WATCOM`
   - `SINGLE_THREADED`
   - `WOLFSSL_SMALL_STACK`
   - `WOLFSSL_USER_IO`
   - `NO_FILESYSTEM`
   - `NO_DEV_RANDOM`
   - `CUSTOM_RAND_GENERATE`
   - Strip all unnecessary cipher suites (keep AES-GCM, ECC, SHA-256)
   - Disable TLS 1.0/1.1, keep TLS 1.2 minimum
   - Disable DSA, RC4, HC128, Rabbit, DES3, PSK, MD4
3. Create an OpenWatcom-compatible Makefile (or wmake file) that compiles WolfSSL source files into a static `.lib`
4. Resolve any 16-bit compilation issues (pointer sizes, segment limits, missing POSIX headers)
5. If 16-bit large model proves unworkable, pivot to wcc386 + DOS/4GW with documentation of why

### Deliverables:
- `WOLFSSL.LIB` that links cleanly
- `user_settings.h` finalized
- Build notes documenting any patches or workarounds needed

### Decision Gate:
If WolfSSL cannot be made to work with OpenWatcom after reasonable effort, evaluate BearSSL as a fallback. If neither works in 16-bit real mode, commit to wcc386 + DOS extender.

---

## Phase 2: Networking Glue — mTCP ↔ WolfSSL Integration

**Objective:** Bridge mTCP's TCP stack to WolfSSL's custom I/O interface so TLS handshakes work over mTCP sockets.

### Tasks:
1. Study mTCP's TCP API (look at `htget.cpp` and `ircjr.cpp` in mTCP source as reference for how apps use it)
2. Write WolfSSL I/O callbacks:
   - `nos_tls_send()` — wraps mTCP's TCP send
   - `nos_tls_recv()` — wraps mTCP's TCP receive
   - Handle mTCP's polling model (mTCP is non-blocking / cooperative, may need polling loops)
3. Write DOS-specific entropy/RNG function:
   - Combine BIOS tick counter (0040:006C), 8254 PIT counter reads, and `time()` for seeding
   - Wire into WolfSSL via `CUSTOM_RAND_GENERATE` hook
4. Write a minimal test program:
   - Connect to a known Gemini server (e.g., `geminiprotocol.net` port 1965)
   - Complete a TLS handshake
   - Send a Gemini request: `gemini://geminiprotocol.net/\r\n`
   - Print the raw response to stdout
   - Disconnect

### Deliverables:
- `tls_io.c` / `tls_io.h` — the mTCP-to-WolfSSL bridge
- `entropy.c` / `entropy.h` — DOS RNG provider
- `tlstest.exe` — test program that proves TLS works over mTCP

### Decision Gate:
If mTCP's API is too awkward to wrap, evaluate switching the TCP layer to Watt-32 (which provides BSD-like sockets and also works with OpenWatcom). This would mean decoupling from the existing NOS-DOS mTCP setup, so it's a last resort.

---

## Phase 3: Gemini Protocol Layer

**Objective:** Implement the Gemini request/response protocol on top of the TLS layer.

### Tasks:
1. Implement Gemini URL parser:
   - Parse `gemini://host[:port]/path`
   - Default port 1965
   - Handle relative URLs for link navigation
2. Implement request function:
   - Open TCP connection to host:port via mTCP
   - Establish TLS via WolfSSL
   - Send `gemini://full-url\r\n`
   - Read response header (status code + meta)
   - Read response body
   - Close connection (Gemini is one request per connection)
3. Handle Gemini status codes:
   - `10` — Input requested (prompt user)
   - `20` — Success (display body)
   - `30` — Redirect (follow URL in meta, limit redirect depth)
   - `40` — Temporary failure (show error)
   - `50` — Permanent failure (show error)
   - `60` — Client certificate required (show message, not implemented initially)
4. Implement DNS resolution (mTCP should provide this, or use its resolver)

### Deliverables:
- `gemini.c` / `gemini.h` — protocol implementation
- `url.c` / `url.h` — URL parsing and manipulation

### Reference:
- Gemini spec: geminiprotocol.net/docs/specification.gmi
- Study `gmni` (C Gemini client) for clean protocol implementation patterns

---

## Phase 4: Gemtext Parser and Text Renderer

**Objective:** Parse gemtext markup and render it to the DOS text-mode console.

### Gemtext line types to handle:
```
text line          → plain text, word-wrap to 80 columns
=> URL label       → link line, number the links for navigation
# heading          → heading level 1 (highlight/color)
## heading         → heading level 2
### heading        → heading level 3
* item             → unordered list item
> quote            → blockquote
```(preformatted)  → preformatted toggle block
```

### Tasks:
1. Write line-by-line gemtext parser
2. Write text-mode renderer:
   - Use DOS console (INT 10h or direct video memory at B800:0000 for speed)
   - Color scheme using DOS text attributes (e.g., links in cyan, headings in yellow, quotes in green)
   - Word wrapping for text lines at 80 columns
   - Numbered links: display as `[1] Link label` so user can type a number to follow
3. Implement scrolling:
   - Page up / Page down
   - Either buffer the full rendered page in memory or re-render from source
4. Handle preformatted blocks (display as-is, no wrapping)

### Deliverables:
- `gemtext.c` / `gemtext.h` — parser
- `render.c` / `render.h` — text-mode display engine

---

## Phase 5: TOFU Certificate Trust

**Objective:** Implement Trust On First Use certificate validation instead of carrying a CA bundle.

### Tasks:
1. Extract server certificate fingerprint (SHA-256) during TLS handshake via WolfSSL API
2. Implement TOFU data store:
   - Store in `TOFU.DAT` file (binary records)
   - Each record: hostname, SHA-256 fingerprint, first-seen date, expiry
3. Implement trust logic:
   - New host → prompt user to accept, save fingerprint
   - Known host + matching fingerprint → proceed silently
   - Known host + CHANGED fingerprint → WARN user prominently, ask to accept/reject
4. Wire into the connection flow between TLS handshake and Gemini request

### Deliverables:
- `tofu.c` / `tofu.h` — TOFU store and validation logic

---

## Phase 6: User Interface and Navigation

**Objective:** Make it feel like a usable browser.

### Tasks:
1. Implement command/navigation loop:
   - Type a URL → navigate to it
   - Type a link number → follow that link
   - `b` or Backspace → go back (history stack)
   - `r` → reload current page
   - `q` → quit
   - `Escape` → cancel loading
2. Implement navigation history:
   - Simple stack of visited URLs
   - Back command pops the stack
3. Implement input handling for status code 10:
   - Display prompt from server meta line
   - Accept user text input
   - URL-encode and append to query, re-request
4. Implement bookmarks:
   - `a` → add current URL to `BOOKMARKS.GEM` file
   - `B` → display bookmarks as a local gemtext page with links
5. Implement a home page:
   - `HOMEPAGE.GEM` local file shown on startup
   - User-editable

### Deliverables:
- `ui.c` / `ui.h` — input handling, command processing
- `nav.c` / `nav.h` — history stack, bookmark management

---

## Phase 7: Polish and Hardening

### Tasks:
1. Handle network timeouts gracefully
2. Handle out-of-memory conditions
3. Limit maximum response body size (configurable, e.g., 512KB)
4. Handle long lines and edge cases in gemtext
5. Configuration file (`NOSGEM.CFG`) for:
   - Home page URL
   - Color scheme
   - Timeout values
   - Maximum response size
6. Clean error messages for all failure modes
7. Test against a variety of Gemini servers/capsules
8. Write a `README` with build instructions and usage

---

## File Structure

```
NOSGEM/
├── Makefile            (wmake compatible)
├── user_settings.h     (WolfSSL config)
├── src/
│   ├── main.c          (entry point, main loop)
│   ├── gemini.c/.h     (protocol layer)
│   ├── url.c/.h        (URL parsing)
│   ├── gemtext.c/.h    (gemtext parser)
│   ├── render.c/.h     (text-mode display)
│   ├── tls_io.c/.h     (WolfSSL ↔ mTCP bridge)
│   ├── entropy.c/.h    (DOS RNG)
│   ├── tofu.c/.h       (certificate trust store)
│   ├── ui.c/.h         (user input handling)
│   └── nav.c/.h        (history, bookmarks)
├── lib/
│   └── wolfssl/        (WolfSSL source, built to .lib)
├── cfg/
│   ├── NOSGEM.CFG      (default config)
│   └── HOMEPAGE.GEM    (default start page)
└── docs/
    └── README.MD
```

---

## Build Targets

```makefile
# 16-bit large model (preferred if it fits)
wcc -ml -os -DWOLFSSL_USER_SETTINGS -Ilib/wolfssl src/*.c WOLFSSL.LIB

# 32-bit with DOS extender (fallback if memory constrained)
wcc386 -os -DWOLFSSL_USER_SETTINGS -Ilib/wolfssl src/*.c WOLFSSL.LIB
```

---

## Working Priorities

```
MUST HAVE (MVP):
  ✓ TLS connection to Gemini servers
  ✓ Send request, receive response
  ✓ Parse and display gemtext
  ✓ Follow links by number
  ✓ Basic navigation (back, URL entry, quit)

SHOULD HAVE:
  ✓ TOFU certificate trust
  ✓ Input prompt handling (status 10)
  ✓ Redirect following (status 30)
  ✓ Bookmarks

NICE TO HAVE:
  ○ Color themes
  ○ Config file
  ○ Download non-text responses to disk
  ○ Client certificate support (status 60)
```

---

## Notes for Claude Code

- This project targets a constrained and unusual environment. Assume nothing from modern POSIX/Linux is available unless explicitly provided by mTCP or OpenWatcom's C runtime.
- All code must be C89 compatible. No C99 features (no `//` comments, no mixed declarations/code, no `stdint.h` unless OpenWatcom provides it).
- Test incrementally. Each phase should produce something that compiles and runs before moving on.
- When in doubt about DOS-specific behavior (interrupts, memory, video), ask rather than assume.
- The mTCP integration is the most project-specific part. Study Michael Brutman's mTCP example applications as the authoritative reference for how to use that API.

---

