CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic

quorum: quorum.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

# Same seed and faults, both policies, with random upsets throughout:
# replica 1 gets stuck at tick 4000, replica 0 dies at tick 9000.
FAULTS = --ticks 20000 --seed 1 --upset-rate 0.001 \
         --fault stuck:1@4000 --fault kill:0@9000

demo: quorum
	./quorum --policy tmr $(FAULTS)
	@echo
	./quorum --policy shrink $(FAULTS)

clean:
	rm -f quorum

.PHONY: demo clean
