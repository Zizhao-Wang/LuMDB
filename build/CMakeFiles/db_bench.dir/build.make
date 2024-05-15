# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.22

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:

#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:

# Disable VCS-based implicit rules.
% : %,v

# Disable VCS-based implicit rules.
% : RCS/%

# Disable VCS-based implicit rules.
% : RCS/%,v

# Disable VCS-based implicit rules.
% : SCCS/s.%

# Disable VCS-based implicit rules.
% : s.%

.SUFFIXES: .hpux_make_needs_suffix_list

# Command-line flag to silence nested $(MAKE).
$(VERBOSE)MAKESILENT = -s

#Suppress display of executed commands.
$(VERBOSE).SILENT:

# A target that is always out of date.
cmake_force:
.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E rm -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/jeff-wang/WorkloadAnalysis/SynLSM

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/jeff-wang/WorkloadAnalysis/build

# Include any dependencies generated for this target.
include CMakeFiles/db_bench.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/db_bench.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/db_bench.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/db_bench.dir/flags.make

CMakeFiles/db_bench.dir/util/histogram.cc.o: CMakeFiles/db_bench.dir/flags.make
CMakeFiles/db_bench.dir/util/histogram.cc.o: /home/jeff-wang/WorkloadAnalysis/SynLSM/util/histogram.cc
CMakeFiles/db_bench.dir/util/histogram.cc.o: CMakeFiles/db_bench.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/jeff-wang/WorkloadAnalysis/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/db_bench.dir/util/histogram.cc.o"
	/usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/db_bench.dir/util/histogram.cc.o -MF CMakeFiles/db_bench.dir/util/histogram.cc.o.d -o CMakeFiles/db_bench.dir/util/histogram.cc.o -c /home/jeff-wang/WorkloadAnalysis/SynLSM/util/histogram.cc

CMakeFiles/db_bench.dir/util/histogram.cc.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/db_bench.dir/util/histogram.cc.i"
	/usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/jeff-wang/WorkloadAnalysis/SynLSM/util/histogram.cc > CMakeFiles/db_bench.dir/util/histogram.cc.i

CMakeFiles/db_bench.dir/util/histogram.cc.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/db_bench.dir/util/histogram.cc.s"
	/usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/jeff-wang/WorkloadAnalysis/SynLSM/util/histogram.cc -o CMakeFiles/db_bench.dir/util/histogram.cc.s

CMakeFiles/db_bench.dir/util/testutil.cc.o: CMakeFiles/db_bench.dir/flags.make
CMakeFiles/db_bench.dir/util/testutil.cc.o: /home/jeff-wang/WorkloadAnalysis/SynLSM/util/testutil.cc
CMakeFiles/db_bench.dir/util/testutil.cc.o: CMakeFiles/db_bench.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/jeff-wang/WorkloadAnalysis/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Building CXX object CMakeFiles/db_bench.dir/util/testutil.cc.o"
	/usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/db_bench.dir/util/testutil.cc.o -MF CMakeFiles/db_bench.dir/util/testutil.cc.o.d -o CMakeFiles/db_bench.dir/util/testutil.cc.o -c /home/jeff-wang/WorkloadAnalysis/SynLSM/util/testutil.cc

CMakeFiles/db_bench.dir/util/testutil.cc.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/db_bench.dir/util/testutil.cc.i"
	/usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/jeff-wang/WorkloadAnalysis/SynLSM/util/testutil.cc > CMakeFiles/db_bench.dir/util/testutil.cc.i

CMakeFiles/db_bench.dir/util/testutil.cc.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/db_bench.dir/util/testutil.cc.s"
	/usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/jeff-wang/WorkloadAnalysis/SynLSM/util/testutil.cc -o CMakeFiles/db_bench.dir/util/testutil.cc.s

CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.o: CMakeFiles/db_bench.dir/flags.make
CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.o: /home/jeff-wang/WorkloadAnalysis/SynLSM/benchmarks/db_bench.cc
CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.o: CMakeFiles/db_bench.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/jeff-wang/WorkloadAnalysis/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_3) "Building CXX object CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.o"
	/usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.o -MF CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.o.d -o CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.o -c /home/jeff-wang/WorkloadAnalysis/SynLSM/benchmarks/db_bench.cc

CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.i"
	/usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/jeff-wang/WorkloadAnalysis/SynLSM/benchmarks/db_bench.cc > CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.i

CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.s"
	/usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/jeff-wang/WorkloadAnalysis/SynLSM/benchmarks/db_bench.cc -o CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.s

# Object files for target db_bench
db_bench_OBJECTS = \
"CMakeFiles/db_bench.dir/util/histogram.cc.o" \
"CMakeFiles/db_bench.dir/util/testutil.cc.o" \
"CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.o"

# External object files for target db_bench
db_bench_EXTERNAL_OBJECTS =

db_bench: CMakeFiles/db_bench.dir/util/histogram.cc.o
db_bench: CMakeFiles/db_bench.dir/util/testutil.cc.o
db_bench: CMakeFiles/db_bench.dir/benchmarks/db_bench.cc.o
db_bench: CMakeFiles/db_bench.dir/build.make
db_bench: libleveldb.a
db_bench: lib/libgmockd.a
db_bench: lib/libgtestd.a
db_bench: third_party/benchmark/src/libbenchmark.a
db_bench: CMakeFiles/db_bench.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/jeff-wang/WorkloadAnalysis/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_4) "Linking CXX executable db_bench"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/db_bench.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/db_bench.dir/build: db_bench
.PHONY : CMakeFiles/db_bench.dir/build

CMakeFiles/db_bench.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/db_bench.dir/cmake_clean.cmake
.PHONY : CMakeFiles/db_bench.dir/clean

CMakeFiles/db_bench.dir/depend:
	cd /home/jeff-wang/WorkloadAnalysis/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/jeff-wang/WorkloadAnalysis/SynLSM /home/jeff-wang/WorkloadAnalysis/SynLSM /home/jeff-wang/WorkloadAnalysis/build /home/jeff-wang/WorkloadAnalysis/build /home/jeff-wang/WorkloadAnalysis/build/CMakeFiles/db_bench.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/db_bench.dir/depend

