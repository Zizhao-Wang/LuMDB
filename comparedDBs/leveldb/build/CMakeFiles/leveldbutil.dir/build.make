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
CMAKE_SOURCE_DIR = /home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/build

# Include any dependencies generated for this target.
include CMakeFiles/leveldbutil.dir/depend.make
# Include any dependencies generated by the compiler for this target.
include CMakeFiles/leveldbutil.dir/compiler_depend.make

# Include the progress variables for this target.
include CMakeFiles/leveldbutil.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/leveldbutil.dir/flags.make

CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.o: CMakeFiles/leveldbutil.dir/flags.make
CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.o: ../db/leveldbutil.cc
CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.o: CMakeFiles/leveldbutil.dir/compiler_depend.ts
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.o"
	/usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -MD -MT CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.o -MF CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.o.d -o CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.o -c /home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/db/leveldbutil.cc

CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.i"
	/usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/db/leveldbutil.cc > CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.i

CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.s"
	/usr/bin/g++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/db/leveldbutil.cc -o CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.s

# Object files for target leveldbutil
leveldbutil_OBJECTS = \
"CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.o"

# External object files for target leveldbutil
leveldbutil_EXTERNAL_OBJECTS =

leveldbutil: CMakeFiles/leveldbutil.dir/db/leveldbutil.cc.o
leveldbutil: CMakeFiles/leveldbutil.dir/build.make
leveldbutil: libleveldb.a
leveldbutil: CMakeFiles/leveldbutil.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/build/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX executable leveldbutil"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/leveldbutil.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/leveldbutil.dir/build: leveldbutil
.PHONY : CMakeFiles/leveldbutil.dir/build

CMakeFiles/leveldbutil.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/leveldbutil.dir/cmake_clean.cmake
.PHONY : CMakeFiles/leveldbutil.dir/clean

CMakeFiles/leveldbutil.dir/depend:
	cd /home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/build && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb /home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb /home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/build /home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/build /home/jeff-wang/WorkloadAnalysis/comparedDBs/leveldb/build/CMakeFiles/leveldbutil.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/leveldbutil.dir/depend

