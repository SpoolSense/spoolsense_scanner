CXX = g++
CC = gcc
CXXFLAGS = -std=c++17 -Wall -Wextra -DNATIVE_TEST -I../../src -I../../include -I. \
           -I../../lib/openprinttag -I../../lib/opentag3d -I../../lib/tigertag -I../../lib/bambutag
CFLAGS = -std=c11 -DNATIVE_TEST -I../../lib/openprinttag

OPT_OBJS = openprinttag_lib.o cbor_native.o
OT3D_OBJS = opentag3d_lib.o

.PHONY: all clean test

all: test_printer_manager test_opt_ndef_bounds test_diagnostics test_tft_landscape_layout test_tigertag_parser test_bambutag_parser test_write_uid_guard test_type2_write_guard test_cc_sizing test_spool_cache test_opentag3d_runner test_opentag3d_patch test_opentag3d_layout test_write_arbitration test_write_outcome_ring

openprinttag_lib.o: ../../lib/openprinttag/openprinttag_lib.c
	$(CC) $(CFLAGS) -c -o $@ $<

cbor_native.o: ../../lib/openprinttag/cbor_native.c
	$(CC) $(CFLAGS) -c -o $@ $<

opentag3d_lib.o: ../../lib/opentag3d/opentag3d_lib.c ../../lib/opentag3d/opentag3d_lib.h ../../lib/opentag3d/opentag3d_v1_map.h ../../lib/opentag3d/opentag3d_v2_map.h
	$(CC) $(CFLAGS) -I../../lib/opentag3d -c -o $@ $<

test_printer_manager: test_printer_manager.cpp native_stubs.cpp MockPrinterStrategy.h test_helpers.h ../../src/IPrinterStrategy.h ../../src/PrinterManager.cpp $(OPT_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ test_printer_manager.cpp native_stubs.cpp $(OPT_OBJS)

test_opt_ndef_bounds: test_opt_ndef_bounds.c $(OPT_OBJS)
	$(CC) $(CFLAGS) -o $@ test_opt_ndef_bounds.c $(OPT_OBJS)

test_diagnostics: test_diagnostics.cpp ../../src/DiagnosticsUtil.cpp ../../src/DiagnosticsUtil.h ../../src/DiagnosticsTypes.h
	$(CXX) $(CXXFLAGS) -o $@ test_diagnostics.cpp ../../src/DiagnosticsUtil.cpp

test_tft_landscape_layout: test_tft_landscape_layout.cpp ../../src/TFTLandscapeLayout.cpp ../../src/TFTLandscapeLayout.h
	$(CXX) $(CXXFLAGS) -o $@ test_tft_landscape_layout.cpp ../../src/TFTLandscapeLayout.cpp

test_tigertag_parser: test_tigertag_parser.cpp ../../lib/tigertag/TigerTagParser.cpp ../../lib/tigertag/TigerTagParser.h
	$(CXX) $(CXXFLAGS) -o $@ test_tigertag_parser.cpp ../../lib/tigertag/TigerTagParser.cpp

test_bambutag_parser: test_bambutag_parser.cpp ../../lib/bambutag/BambuTagParser.cpp ../../lib/bambutag/BambuTagParser.h
	$(CXX) $(CXXFLAGS) -o $@ test_bambutag_parser.cpp ../../lib/bambutag/BambuTagParser.cpp

test_write_uid_guard: test_write_uid_guard.cpp ../../src/WriteUidGuard.h
	$(CXX) $(CXXFLAGS) -o $@ test_write_uid_guard.cpp

test_type2_write_guard: test_type2_write_guard.cpp ../../src/Type2WriteGuard.h
	$(CXX) $(CXXFLAGS) -o $@ test_type2_write_guard.cpp

test_opentag3d_layout: test_opentag3d_layout.cpp ../../src/OpenTag3DLayout.h $(OT3D_OBJS) opentag3d_patch.o
	$(CXX) $(CXXFLAGS) -I../../lib/opentag3d -o $@ test_opentag3d_layout.cpp $(OT3D_OBJS) opentag3d_patch.o

test_cc_sizing: test_cc_sizing.cpp ../../src/NFCTypes.h
	$(CXX) $(CXXFLAGS) -o $@ test_cc_sizing.cpp

test_spool_cache: test_spool_cache.cpp ../../src/SpoolCacheJson.h
	$(CXX) $(CXXFLAGS) -o $@ test_spool_cache.cpp

test_opentag3d_runner: test_opentag3d_runner.c $(OT3D_OBJS)
	$(CC) $(CFLAGS) -I../../lib/opentag3d -o $@ test_opentag3d_runner.c $(OT3D_OBJS)

opentag3d_patch.o: ../../lib/opentag3d/opentag3d_patch.c ../../lib/opentag3d/opentag3d_patch.h
	$(CC) $(CFLAGS) -I../../lib/opentag3d -c -o $@ $<

test_opentag3d_patch: test_opentag3d_patch.c $(OT3D_OBJS) opentag3d_patch.o
	$(CC) $(CFLAGS) -I../../lib/opentag3d -o $@ test_opentag3d_patch.c $(OT3D_OBJS) opentag3d_patch.o

opentag3d_arbitrate.o: ../../lib/opentag3d/opentag3d_arbitrate.c ../../lib/opentag3d/opentag3d_arbitrate.h
	$(CC) $(CFLAGS) -I../../lib/opentag3d -c -o $@ $<

test_write_arbitration: test_write_arbitration.c opentag3d_arbitrate.o
	$(CC) $(CFLAGS) -I../../lib/opentag3d -o $@ test_write_arbitration.c opentag3d_arbitrate.o

test_write_outcome_ring: test_write_outcome_ring.cpp ../../src/WriteOutcomeRing.h
	$(CXX) $(CXXFLAGS) -o $@ test_write_outcome_ring.cpp

test: test_printer_manager test_opt_ndef_bounds test_diagnostics test_tft_landscape_layout test_tigertag_parser test_bambutag_parser test_write_uid_guard test_type2_write_guard test_cc_sizing test_spool_cache test_opentag3d_runner test_opentag3d_patch test_opentag3d_layout test_write_arbitration test_write_outcome_ring test_write_result_gate.js
	@echo ""
	@echo "=== Running PrinterManager tests ==="
	@./test_printer_manager
	@echo ""
	@echo "=== Running openprinttag NDEF bounds tests ==="
	@./test_opt_ndef_bounds
	@echo ""
	@echo "=== Running Diagnostics tests ==="
	@./test_diagnostics
	@echo ""
	@echo "=== Running Landscape layout tests ==="
	@./test_tft_landscape_layout
	@echo ""
	@echo "=== Running TigerTag parser tests ==="
	@./test_tigertag_parser
	@echo ""
	@echo "=== Running Bambu parser tests ==="
	@./test_bambutag_parser
	@echo ""
	@echo "=== Running write UID guard tests ==="
	@./test_write_uid_guard
	@echo ""
	@echo "=== Running type 2 write guard tests ==="
	@./test_type2_write_guard
	@echo ""
	@echo "=== Running CC sizing tests ==="
	@./test_cc_sizing
	@echo ""
	@echo "=== Running spool cache JSON tests ==="
	@./test_spool_cache
	@echo ""
	@echo "=== Running OpenTag3D parser tests ==="
	@./test_opentag3d_runner
	@echo ""
	@echo "=== Running OpenTag3D patch tests ==="
	@./test_opentag3d_patch
	@echo ""
	@echo "=== Running OpenTag3D layout tests ==="
	@./test_opentag3d_layout
	@echo ""
	@echo "=== Running OpenTag3D write arbitration tests ==="
	@./test_write_arbitration
	@echo ""
	@echo "=== Running write outcome ring tests ==="
	@./test_write_outcome_ring
	@echo ""
	@echo "=== Running write-result gate tests (Node) ==="
	@if command -v node >/dev/null 2>&1; then \
		node test_write_result_gate.js; \
	else \
		echo "SKIP: node not found, write-result gate test not run"; \
	fi
	@echo ""

clean:
	rm -f test_printer_manager test_opt_ndef_bounds test_diagnostics \
	      test_tft_landscape_layout test_tigertag_parser test_bambutag_parser \
	      test_write_uid_guard test_type2_write_guard test_cc_sizing test_spool_cache test_opentag3d_runner \
	      test_opentag3d_patch test_opentag3d_layout test_write_arbitration \
	      test_write_outcome_ring *.o
