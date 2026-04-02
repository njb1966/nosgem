# NOSgem Makefile -- GNU make, cross-compiling DOS 16-bit via OpenWatcom
#
# Targets:
#   make wolfssl    -- build WOLFSSL.LIB
#   make tlstest    -- build TLSTEST.EXE (Phase 2 integration test)
#   make            -- build NOSGEM.EXE  (full app, later phases)
#   make clean      -- remove all generated files

CC   = wcc      # C compiler   (16-bit large model)
CXX  = wpp      # C++ compiler (16-bit large model)
AR   = wlib
ASM  = wasm

MTCP_INC = lib/mtcp/mTCP-src_2025-01-10/TCPINC
MTCP_SRC = lib/mtcp/mTCP-src_2025-01-10/TCPLIB

# -0       : 8086 compatible code (safe baseline for DOS)
# -ml      : large memory model
# -os      : optimise for size
# -zq      : quiet (suppress banner)
# -w3      : warning level 3
# -s       : disable stack overflow checks (saves space, mTCP does this)
# -ei      : use int-sized enums (mTCP requires this)
# -zp2     : 2-byte struct alignment (mTCP requires this)
# CFG_H    : per-application mTCP config (must be defined for mTCP headers)

# Note: -os is not valid for wpp; use -oh -ok -ot for C++ optimisation.
#       CFG_H quoting: use single quotes around the whole -D flag in make
#       so the shell preserves the inner double quotes.
CFLAGS = -ml -os -zq -w3 -s -ei -zp2 \
         '-DCFG_H="nosgem.cfg"' \
         -Isrc \
         -I$(MTCP_INC) \
         -DWOLFSSL_USER_SETTINGS \
         -I. \
         -Ilib/dos-compat \
         -Ilib/wolfssl

CXXFLAGS = -ml -oh -ok -ot -zq -w3 -s -ei -zp2 \
           '-DCFG_H="nosgem.cfg"' \
           -Isrc \
           -I$(MTCP_INC) \
           -DWOLFSSL_USER_SETTINGS \
           -I. \
           -Ilib/dos-compat \
           -Ilib/wolfssl

WOLFLIB = WOLFSSL.LIB
OBJDIR  = obj

# ---------------------------------------------------------------------------
# WolfSSL sources
# ---------------------------------------------------------------------------
WC_SRC = \
    lib/wolfssl/wolfcrypt/src/aes.c \
    lib/wolfssl/wolfcrypt/src/asn.c \
    lib/wolfssl/wolfcrypt/src/coding.c \
    lib/wolfssl/wolfcrypt/src/ecc.c \
    lib/wolfssl/wolfcrypt/src/error.c \
    lib/wolfssl/wolfcrypt/src/hash.c \
    lib/wolfssl/wolfcrypt/src/hmac.c \
    lib/wolfssl/wolfcrypt/src/integer.c \
    lib/wolfssl/wolfcrypt/src/memory.c \
    lib/wolfssl/wolfcrypt/src/misc.c \
    lib/wolfssl/wolfcrypt/src/random.c \
    lib/wolfssl/wolfcrypt/src/sha256.c \
    lib/wolfssl/wolfcrypt/src/signature.c \
    lib/wolfssl/wolfcrypt/src/rsa.c \
    lib/wolfssl/wolfcrypt/src/kdf.c \
    lib/wolfssl/wolfcrypt/src/pwdbased.c \
    lib/wolfssl/wolfcrypt/src/wc_encrypt.c \
    lib/wolfssl/wolfcrypt/src/wc_port.c \
    lib/wolfssl/wolfcrypt/src/wolfmath.c

SSL_SRC = \
    lib/wolfssl/src/internal.c \
    lib/wolfssl/src/keys.c \
    lib/wolfssl/src/ssl.c \
    lib/wolfssl/src/tls.c \
    lib/wolfssl/src/wolfio.c

# ---------------------------------------------------------------------------
# mTCP library sources (compiled into each app, not a separate .lib)
# ---------------------------------------------------------------------------
MTCP_CPP_SRC = \
    $(MTCP_SRC)/ARP.CPP \
    $(MTCP_SRC)/DNS.CPP \
    $(MTCP_SRC)/ETH.CPP \
    $(MTCP_SRC)/ETHTYPE.CPP \
    $(MTCP_SRC)/IP.CPP \
    $(MTCP_SRC)/IPTYPE.CPP \
    $(MTCP_SRC)/PACKET.CPP \
    $(MTCP_SRC)/TCP.CPP \
    $(MTCP_SRC)/TCPSOCKM.CPP \
    $(MTCP_SRC)/TIMER.CPP \
    $(MTCP_SRC)/TRACE.CPP \
    $(MTCP_SRC)/UDP.CPP \
    $(MTCP_SRC)/UTILS.CPP

MTCP_ASM_SRC = $(MTCP_SRC)/IPASM.ASM

# ---------------------------------------------------------------------------
# tlstest application sources
# ---------------------------------------------------------------------------
TLSTEST_CPP_SRC = \
    src/tls_io.cpp \
    src/tlstest.cpp

TLSTEST_C_SRC = \
    src/entropy.c

# ---------------------------------------------------------------------------
# Derived object lists (all flat in OBJDIR)
# ---------------------------------------------------------------------------
WC_OBJS       = $(patsubst %.c,$(OBJDIR)/%.obj,$(notdir $(WC_SRC)))
SSL_OBJS      = $(patsubst %.c,$(OBJDIR)/%.obj,$(notdir $(SSL_SRC)))
WOLF_OBJS     = $(WC_OBJS) $(SSL_OBJS)

MTCP_CPP_OBJS = $(patsubst %.CPP,$(OBJDIR)/%.obj,$(notdir $(MTCP_CPP_SRC)))
MTCP_ASM_OBJS = $(patsubst %.ASM,$(OBJDIR)/%.obj,$(notdir $(MTCP_ASM_SRC)))
MTCP_OBJS     = $(MTCP_CPP_OBJS) $(MTCP_ASM_OBJS)

TLSTEST_CPP_OBJS = $(patsubst %.cpp,$(OBJDIR)/%.obj,$(notdir $(TLSTEST_CPP_SRC)))
TLSTEST_C_OBJS   = $(patsubst %.c,$(OBJDIR)/%.obj,$(notdir $(TLSTEST_C_SRC)))
TLSTEST_OBJS     = $(TLSTEST_CPP_OBJS) $(TLSTEST_C_OBJS) $(MTCP_OBJS)

# VPATH: let make find sources by basename
VPATH = lib/wolfssl/wolfcrypt/src:lib/wolfssl/src:$(MTCP_SRC):src

# ---------------------------------------------------------------------------
# Build rules
# ---------------------------------------------------------------------------
.PHONY: all wolfssl tlstest clean

all: NOSGEM.EXE

wolfssl: $(WOLFLIB)

tlstest: TLSTEST.EXE

$(OBJDIR):
	mkdir -p $(OBJDIR)

# C sources -> obj
$(OBJDIR)/%.obj: %.c | $(OBJDIR)
	$(CC) $(CFLAGS) -fo=$@ $<

# C++ sources -> obj (lowercase .cpp)
$(OBJDIR)/%.obj: %.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -fo=$@ $<

# C++ sources -> obj (uppercase .CPP from mTCP)
$(OBJDIR)/%.obj: %.CPP | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -fo=$@ $<

# ASM sources -> obj (uppercase .ASM from mTCP)
$(OBJDIR)/%.obj: %.ASM | $(OBJDIR)
	$(ASM) -0 -ml -fo=$@ $<

# WolfSSL static library
$(WOLFLIB): $(WOLF_OBJS)
	@rm -f $(WOLFLIB)
	$(AR) -q $(WOLFLIB) $(foreach o,$(WOLF_OBJS),+$(o))

# TLSTEST.EXE -- Phase 2 integration test
TLSTEST.EXE: $(TLSTEST_OBJS) $(WOLFLIB)
	wlink system dos \
	    option stack=8192 \
	    name $@ \
	    $(foreach o,$(TLSTEST_OBJS),file $(o)) \
	    library $(WOLFLIB)

# NOSGEM.EXE -- full application (later phases)
NOSGEM.EXE: $(WOLFLIB)
	@echo "NOSGEM.EXE: not yet implemented (Phase 3+)"

clean:
	rm -rf $(OBJDIR) $(WOLFLIB) TLSTEST.EXE NOSGEM.EXE
