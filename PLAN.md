# NOSgem — Project Plan

**Project:** NOS-DOS Gemini Client (NOSgem)
**Language:** C89 / C++ (for mTCP)
**Toolchain:** OpenWatcom (wcc/wpp, 16-bit large model)
**Platform:** MS-DOS 6.22 (VirtualBox VM, real hardware target)
**Networking:** mTCP (Jan 10 2025 release)
**TLS:** WolfSSL 5.9.0
**Last updated:** 2026-04-01

---

## Phase Status

| Phase | Status | Notes |
|-------|--------|-------|
| 1 — WolfSSL build | ✅ Complete | WOLFSSL.LIB ~410KB |
| 2 — mTCP↔WolfSSL bridge | ✅ Complete | TLS handshake + Gemini response confirmed on real DOS |
| 3 — Gemini protocol layer | 📋 Next | gemini.c/h, url.c/h |
| 4 — Gemtext parser/renderer | 📋 Planned | gemtext.c/h, render.c/h |
| 5 — TOFU certificate trust | 🟡 Partial | TOFU logic in tlstest; needs tofu.c/h extraction |
| 6 — UI/Navigation | 📋 Planned | ui.c/h, nav.c/h |
| 7 — Polish & hardening | 📋 Planned | Heap corruption fix, config file, limits |

---

## What Has Been Built

### Phase 1 — WolfSSL Static Library

WolfSSL 5.9.0 compiled to `WOLFSSL.LIB` (~410KB) using OpenWatcom 16-bit large model
cross-compiled from Linux.

**Key files:**
- `user_settings.h` — WolfSSL compile-time configuration (see reference section below)
- `Makefile` — GNU make build system
- `lib/wolfssl/` — WolfSSL 5.9.0 source
- `lib/dos-compat/` — Empty POSIX socket stub headers (sys/socket.h, arpa/inet.h, netinet/in.h)
- `lib/mtcp/mTCP-src_2025-01-10/TCPINC/` — Lowercase symlinks for all mTCP headers

**WolfSSL source files compiled:**

*wolfcrypt:* aes.c, asn.c, coding.c, ecc.c, error.c, hash.c, hmac.c, integer.c,
memory.c, misc.c, random.c, sha256.c, signature.c, rsa.c, kdf.c, pwdbased.c,
wc_encrypt.c, wc_port.c, wolfmath.c

*ssl:* internal.c, keys.c, ssl.c, tls.c, wolfio.c

### Phase 2 — mTCP↔WolfSSL Bridge (CONFIRMED WORKING)

`TLSTEST.EXE` (319KB) successfully:
- Resolves DNS via mTCP
- Opens TCP connection via mTCP
- Completes TLS 1.2 handshake (`ECDHE-ECDSA-AES128-GCM-SHA256`)
- Sends Gemini request: `gemini://geminiprotocol.net/\r\n`
- Receives and prints the full gemtext response from geminiprotocol.net

**Key files:**
- `src/entropy.c` / `src/entropy.h` — DOS RNG (BIOS tick 0040:006C + 8254 PIT)
- `src/tls_io.cpp` / `src/tls_io.h` — mTCP↔WolfSSL I/O bridge (C++ file, C-callable interface)
- `src/tlstest.cpp` — Phase 2 test program
- `src/nosgem.cfg` — mTCP per-application config

**Known issue:** `End: heap is corrupted!` prints on exit. WolfSSL teardown in 16-bit mode.
Does NOT affect TLS session. Deferred to Phase 7.

**Recent additions (current repo state):**
- `.gitignore` — ignores build artifacts and error dumps
- `README.md` — project summary and layout
- TOFU trust logic added to `src/tlstest.cpp`:
  - `C:\known_hosts.txt` text store
  - Format: `host:port SHA256 <hex>`
  - Prompt on first use and on mismatch
  - Hard-fail if the store cannot be written

---

## Phase 3 — Gemini Protocol Layer (NEXT)

**Objective:** Build a proper protocol module on top of the TLS layer.

**Files to create:**
- `src/url.c` / `src/url.h` — URL parsing
- `src/gemini.c` / `src/gemini.h` — Request/response protocol

### url.c/h — Tasks

1. Parse `gemini://host[:port]/path` into components
2. Default port 1965
3. Handle relative URLs (for link navigation in Phase 6)
4. Percent-encode query strings (for status 10 input)

```c
/* C89 — declarations at top of block, no stdint.h */
typedef struct {
    char host[256];
    unsigned int port;
    char path[512];
} nos_url_t;

int nos_url_parse(const char *raw, nos_url_t *out);
void nos_url_resolve(const nos_url_t *base, const char *link, nos_url_t *out);
```

### gemini.c/h — Tasks

1. Open connection: `nos_tls_connect()` from tls_io.h
2. Send request: `gemini://host/path\r\n`
3. Read response header: `<STATUS> <META>\r\n` (max 1024 bytes per spec)
4. Handle status codes:
   - `10` — Input (prompt user, re-request with `?query`)
   - `20` — Success (read body, return to caller)
   - `3x` — Redirect (follow META as URL, max 5 hops)
   - `4x` / `5x` — Failure (return error string from META)
   - `6x` — Client cert required (return error, not implemented yet)
5. Read response body (status 20) into a buffer or stream to caller
6. Close connection (Gemini: one request per connection)

```c
typedef struct {
    int  status;
    char meta[1024];
} nos_gemini_resp_t;

int nos_gemini_request(const nos_url_t *url, nos_gemini_resp_t *resp,
                       char *body_buf, unsigned int body_max,
                       unsigned int *body_len);
```

**Constraints:**
- C89 only — all declarations before statements, `/* */` comments only
- No `stdint.h` — use `unsigned char`, `unsigned int`, `unsigned long`
- Gemini response body can be large — may need to stream rather than buffer all at once
- Max response size limit: start at 512KB, make configurable in Phase 7

---

## Phase 4 — Gemtext Parser and Text Renderer

**Files to create:** `src/gemtext.c/h`, `src/render.c/h`

### Gemtext line types
```
text          -> plain text, word-wrap to 80 cols
=> URL label  -> link (number for navigation)
# heading     -> H1
## heading    -> H2
### heading   -> H3
* item        -> list item
> quote       -> blockquote
```(toggle)   -> preformatted block (no wrap)
```

### Tasks
1. Line-by-line parser — identify type from prefix
2. Word-wrap text lines at 80 columns
3. Number links: display as `[1] label` for keyboard navigation
4. DOS text-mode renderer:
   - Direct console output (printf sufficient for MVP, INT 10h / B800:0000 for speed later)
   - Color using DOS text attributes: links=cyan, headings=yellow, quotes=green, pre=white
5. Page/scroll support: buffer rendered lines, Page Up/Down

---

## Phase 5 — TOFU Certificate Trust

**Files to create:** `src/tofu.c/h`

### Tasks
1. Extract server cert SHA-256 fingerprint via WolfSSL API after handshake
2. `C:\known_hosts.txt` text file: `host:port SHA256 <hex>` (one per line)
   - Chosen to be DOS-version-agnostic and easy to inspect/edit
3. Trust logic:
   - New host → prompt user → save
   - Known host + match → silent
   - Known host + changed → WARN prominently, prompt accept/reject
4. Wire into `nos_tls_connect()` after handshake, before returning

**WolfSSL call for fingerprint:** `wolfSSL_get_peer_certificate()` → `wolfSSL_X509_get_der()` + `wc_Sha256*`

---

## Phase 6 — User Interface and Navigation

**Files to create:** `src/ui.c/h`, `src/nav.c/h`

### Command loop
| Key | Action |
|-----|--------|
| URL (typed) | Navigate to URL |
| Number | Follow link N |
| `b` / Backspace | Go back |
| `r` | Reload |
| `q` | Quit |
| `a` | Add bookmark |
| `B` | Show bookmarks |
| Esc | Cancel load |

### Nav stack
- Simple stack of visited URLs (back history)
- Bookmarks saved to `BOOKMARKS.GEM` (local gemtext file)
- `HOMEPAGE.GEM` shown on startup

### Status 10 handling
- Display META as prompt
- Accept text input from user
- URL-encode, append as `?query`, re-request

---

## Phase 7 — Polish and Hardening

1. **Fix heap corruption on exit** — investigate WolfSSL teardown in 16-bit mode
2. Network timeouts (currently 15s recv timeout in tls_io.cpp)
3. Out-of-memory handling
4. Max response body size (512KB default)
5. Long line edge cases in gemtext
6. Configuration file: `NOSGEM.CFG`
   - Home page URL
   - Timeout values
   - Max response size
   - Color scheme
7. `WOLFSSL_ALERT_WATCH` can be removed once stable
8. Test against diverse Gemini capsules
9. Write `docs/README.MD`

---

## Build System Reference

### Build commands (always run from project root)

```bash
# Full clean rebuild — REQUIRED after any user_settings.h change
make clean && make tlstest

# Rebuild app only (wolfssl.lib unchanged)
make tlstest

# Build WolfSSL library only
make wolfssl
```

> **Use GNU make, not wmake.** wmake cannot handle subdirectory path inference
> for flat obj/ targets.
>
> `user_settings.h` is NOT tracked as a Makefile dependency — always `make clean`
> after changing it.

### Compiler flags

| Flag | For | Reason |
|------|-----|--------|
| `-ml` | wcc + wpp | 16-bit large memory model |
| `-os` | wcc only | Optimize for size (invalid for wpp) |
| `-oh -ok -ot` | wpp only | wpp size/speed opts (no -os) |
| `-zq` | both | Quiet output |
| `-w3` | both | Warning level 3 |
| `-s` | both | Disable stack overflow checks |
| `-ei` | both | Enum = int |
| `-zp2` | both | 2-byte struct alignment |
| `-DWOLFSSL_USER_SETTINGS` | both | Use our user_settings.h |
| `-I.` | both | Root dir for user_settings.h |
| `-Ilib/dos-compat` | both | Empty POSIX socket stubs |
| `-Ilib/wolfssl` | both | WolfSSL headers |

### Linker

```
wlink system dos option stack=8192 name TLSTEST.EXE file {objs} library WOLFSSL.LIB
```

Stack is 8KB. May need to increase if stack pressure grows in later phases.

---

## DOS VM / Test Workflow

| Item | Value |
|------|-------|
| DOS VM | MS-DOS 6.22, VirtualBox |
| DOS VM IP | 192.168.0.156 |
| Linux dev IP | 192.168.0.97 |
| mTCP config | `C:\NET\MTCP.CFG`, `MTCPCFG` env var set |
| TZ | Set in AUTOEXEC.BAT |
| Working dir | `C:\NOSGEM\` |

### Transfer workflow

Linux (from project root):
```bash
python3 -m http.server 8080
```

DOS:
```
C:\NOSGEM> C:\NET\HTGET.EXE -o TLSTEST.EXE http://192.168.0.97:8080/TLSTEST.EXE
C:\NOSGEM> TLSTEST.EXE
```

Use `http://` — HTGET does not support TLS.

### Capturing output

Long responses scroll off the 25-line screen:
```
C:\NOSGEM> TLSTEST.EXE > OUT.TXT
C:\NOSGEM> TYPE OUT.TXT | MORE
```

stderr cannot be redirected in COMMAND.COM (error messages visible but not capturable).

---

## WolfSSL Configuration Reference

### user_settings.h — every define and why

| Define | Reason |
|--------|--------|
| `WOLFSSL_WATCOM` | Compiler identification |
| `WC_16BIT_CPU` | word32=unsigned long, word16=unsigned int |
| `USE_INTEGER_HEAP_MATH` | SP math refuses WC_16BIT_CPU; use portable heap math |
| `MP_16BIT` | mp_digit=unsigned int (16-bit), mp_word=unsigned long (32-bit) |
| `DIGIT_BIT 15` | Numeric literal required — sizeof() invalid in preprocessor |
| `SINGLE_THREADED` | DOS has no threads |
| `WOLFSSL_SMALL_STACK` | Move large buffers to heap to reduce stack usage |
| `WOLFSSL_SMALL_STACK_STATIC` | Make scratch buffers static (thread-unsafe but DOS is single-threaded) |
| `WOLFSSL_USER_IO` | Supply nos_tls_send/nos_tls_recv callbacks instead of BSD sockets |
| `NO_FILESYSTEM` | DOS has no POSIX cert file API |
| `NO_DEV_RANDOM` | No /dev/random on DOS |
| `CUSTOM_RAND_GENERATE nos_rand_byte` | DOS RNG: BIOS tick 0040:006C + 8254 PIT |
| `NO_OLD_TLS` | Disable TLS 1.0/1.1 |
| `NO_DH` | ECDHE only; avoids dh.c |
| `NO_DSA`, `NO_RC4`, `NO_HC128`, `NO_RABBIT`, `NO_DES3`, `NO_PSK`, `NO_MD4` | Unused algorithms |
| `NO_MD5`, `NO_SHA` | Not needed for TLS 1.2 with SHA-256 |
| `NO_SHA384`, `NO_SHA512` | Save space; AES-128-GCM-SHA256 is sufficient |
| `HAVE_AESGCM`, `HAVE_AES_DECRYPT` | AES-GCM cipher |
| `HAVE_ECC` | ECDHE key exchange + ECDSA cert verification |
| `ECC_TIMING_RESISTANT`, `TFM_TIMING_RESISTANT` | Side-channel security |
| `HAVE_TLS_EXTENSIONS` | Gate for SNI, Supported Curves, Extended Master Secret |
| `HAVE_SNI` | Server Name Indication — required for virtual hosting |
| `HAVE_SUPPORTED_CURVES` | Supported Groups extension — required for ECDHE negotiation |
| `HAVE_EXTENDED_MASTER` | Extended Master Secret — required by modern Go TLS servers |
| `WOLFSSL_ALERT_WATCH` | Record TLS alert history for diagnostics (remove in Phase 7) |
| `NO_WOLFSSL_SERVER` | Client-only build |
| `NO_SESSION_CACHE` | Gemini is one-request-per-connection |
| `WOLFSSL_NO_SOCK` | No BSD socket layer; custom I/O only |
| `WOLFSSL_BASE16` | SHA-256 fingerprint hex display (Phase 5 TOFU) |
| `WOLFSSL_HAVE_MIN`, `WOLFSSL_HAVE_MAX` | Avoid conflicts with DOS runtime macros |
| `NO_WRITEV`, `NO_MAIN_DRIVER`, `BENCH_EMBEDDED` | DOS compatibility |

### Defines that must NOT be used

| Define | Why removed / forbidden |
|--------|------------------------|
| `ALT_ECC_SIZE` | Incompatible with USE_INTEGER_HEAP_MATH |
| `TFM_ECC256` | TFM is fast-math; conflicts with heap math |
| `WOLFSSL_SMALL_STACK_CACHE` | DECL_MP_INT_SIZE_DYN creates arrays; cache tries to assign — illegal in C |
| `NO_RSA` | geminiprotocol.net may use RSA on other paths; keep RSA available |

### WolfSSL patches applied to library source

1. `lib/wolfssl/wolfssl/ssl.h` lines ~4503-4506: Changed `unsigned short` to `word16`
   in `wolfSSL_UseSNI()` and `wolfSSL_CTX_UseSNI()` declarations to match the
   implementation (which uses `word16` = `unsigned int` in WC_16BIT_CPU mode).

---

## Hard-Won Build Gotchas

1. **GNU make, not wmake** — wmake can't infer obj/ paths for subdirectory sources
2. **`make clean` after every `user_settings.h` change** — not a tracked dependency
3. **`-os` is wcc-only** — use `-oh -ok -ot` for wpp (C++)
4. **CFG_H quoting** — must be `'-DCFG_H="nosgem.cfg"'` to survive shell expansion
5. **`#include <stdio.h>` first** — mTCP headers use `FILE*` and will fail without it
6. **mTCP header symlinks** — all-caps originals, lowercase symlinks required on Linux
7. **`COMPILE_DNS` and friends in nosgem.cfg** — without them UTILS.CPP silently skips
   parsing NAMESERVER from the runtime mtcp.cfg, causing DNS to fail
8. **`DIGIT_BIT` must be a numeric literal** — `sizeof()` is invalid in preprocessor `#if`
9. **wolfio.h POSIX conflict** — OpenWatcom defines both `__WATCOMC__` and `__LINUX__`,
   so stub headers in `lib/dos-compat/` are required even with `WOLFSSL_USER_IO`
10. **ECDSA, not RSA** — geminiprotocol.net uses P-256 ECDSA cert; RSA-only ClientHello
    causes handshake_failure (alert code 40)
11. **`HAVE_SUPPORTED_CURVES` is not auto-defined** — must be explicit in user_settings.h;
    without it the Supported Groups extension is omitted and ECDHE negotiation fails

---

## File Structure (current)

```
NOSGEM/
├── Makefile
├── user_settings.h          WolfSSL config
├── PLAN.md                  this file
├── CLAUDE.md                build quick-reference
├── src/
│   ├── entropy.c/h          DOS RNG (BIOS ticks + PIT)
│   ├── tls_io.cpp/h         WolfSSL <-> mTCP I/O bridge
│   ├── nosgem.cfg           mTCP per-app config
│   └── tlstest.cpp          Phase 2 test (TLS + Gemini request)
├── lib/
│   ├── wolfssl/             WolfSSL 5.9.0 source (patched ssl.h)
│   ├── dos-compat/          Stub POSIX headers
│   └── mtcp/                mTCP source (with lowercase symlinks in TCPINC/)
└── obj/                     Build artifacts (flat, generated by make)
```

Files to be created in Phase 3+:
```
src/url.c/h          URL parsing
src/gemini.c/h       Gemini protocol layer
src/gemtext.c/h      Gemtext parser          (Phase 4)
src/render.c/h       DOS text-mode renderer  (Phase 4)
src/tofu.c/h         TOFU cert trust store   (Phase 5)
src/ui.c/h           Input handling          (Phase 6)
src/nav.c/h          History + bookmarks     (Phase 6)
src/main.c           Entry point + main loop (Phase 6)
cfg/NOSGEM.CFG       Default config          (Phase 7)
cfg/HOMEPAGE.GEM     Default start page      (Phase 7)
```
