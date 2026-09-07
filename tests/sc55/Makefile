CXX ?= c++
CORE = ../../X68000 Shared/SC55/core
SAN ?= -fsanitize=address,undefined
.PHONY: all clean
all:
	$(CXX) -std=c++14 -O1 -g $(SAN) test_bridge.cpp \
		"$(CORE)/mcu.cpp" "$(CORE)/mcu_interrupt.cpp" \
		"$(CORE)/mcu_opcodes.cpp" "$(CORE)/mcu_timer.cpp" \
		"$(CORE)/pcm.cpp" "$(CORE)/submcu.cpp" -o /tmp/mpx68k-test-sc55
	/tmp/mpx68k-test-sc55
clean:
	rm -f /tmp/mpx68k-test-sc55

# Optional: make -C tests/sc55 rom ROM_DIR='/path/to/your/SC-55 ROM folder'
.PHONY: rom
rom:
	$(CXX) -std=c++14 -O2 test_rom.cpp \
		"$(CORE)/mcu.cpp" "$(CORE)/mcu_interrupt.cpp" \
		"$(CORE)/mcu_opcodes.cpp" "$(CORE)/mcu_timer.cpp" \
		"$(CORE)/pcm.cpp" "$(CORE)/submcu.cpp" -o /tmp/mpx68k-test-sc55-rom
	/tmp/mpx68k-test-sc55-rom "$(ROM_DIR)"
