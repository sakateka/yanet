# Simple Makefile for hashtable_benchmark

OPT ?= -O2
CXX = g++
CXXFLAGS = -std=c++17 $(OPT) -march=corei7 -Wall -Wextra -pthread

# Include paths
INCLUDES = -I. -I./common -I./dataplane \
           -I./build_unittest/subprojects/dpdk \
           -I./subprojects/dpdk/config \
           -I./subprojects/dpdk/lib/eal/include \
           -I./subprojects/dpdk/lib/eal/linux/include \
           -I./subprojects/dpdk/lib/eal/x86/include \
           -I./subprojects/dpdk/lib/ethdev \
           -I./subprojects/dpdk/lib/mbuf \
           -I./subprojects/dpdk/lib/mempool \
           -I./subprojects/dpdk/lib/ring \
           -I./subprojects/dpdk/lib/hash \
           -I./subprojects/dpdk/lib/log \
           -I./subprojects/dpdk/lib/net \
           -I./subprojects/dpdk/lib/rcu \
           -I./subprojects/dpdk/lib/timer \
           -I./subprojects/json/include

# Library paths and libraries
LIBS = -L./build_unittest/subprojects/dpdk/lib \
       -lrte_eal -lrte_mbuf -lrte_mempool -lrte_ring -lrte_hash \
       -lrte_log -lrte_timer -lrte_ethdev -lrte_net -lrte_rcu \
       -lnuma -ldl -lm -lpthread

configure:
	meson setup -Dtarget=unittest build_unittest --reconfigure
	meson compile -C build_unittest

hashtable_benchmark: hashtable_benchmark.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) hashtable_benchmark.cpp -o hashtable_benchmark $(LIBS)

clean:
	rm -f hashtable_benchmark

run: hashtable_benchmark
	LD_LIBRARY_PATH=./build_unittest/subprojects/dpdk/lib:$$LD_LIBRARY_PATH ./hashtable_benchmark

debug: hashtable_benchmark
	LD_LIBRARY_PATH=./build_unittest/subprojects/dpdk/lib:$$LD_LIBRARY_PATH timeout -s SIGINT 5 gdb -batch -ex "set confirm off" -ex run -ex bt -ex quit --args ./hashtable_benchmark
