# Improving Indirect-Call Analysis in LLVM with Type and Data-Flow Co-Analysis

This project includes a prototype implementation of TFA. The foundation behind TFA lies in the complementary nature of type-based analysis and data-flow analysis when it comes to resolving indirect-call targets. By combining the strengths of both analyses, TFA incorporates a co-analysis system that iteratively refines the global call graph. This iterative refinement process enables TFA to achieve an optimal indirect call analysis. 

## Supported features 
* Multi-layer type analysis for indirect call resolving in C and C++ programs.
* Data-flow based indirect call resolving in C and C++ programs.
* Class hierarchy analysis based virtual call analysis in C++ programs (supporting single and multiple inheritance). Note: RTTI info is required to enable this analysis.

## How to use TFA

### Build LLVM 
* Download the source of llvm-project (https://github.com/llvm/llvm-project).
* Checkout suitable llvm versions (this project is compiled and tested with llvm-15-init).
* Compile the llvm-project and add the project build path to the Makefile.
  You could use the following commands to build the llvm project:
```sh 
    $ mkdir build
    $ cd build
    $ cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE="Release" -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld;lldb;openmp" -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" ../llvm
	$ make 
```

### Build the TFA analyzer 
* Before proceeding with the build process of the analyzer, it is essential to tailor the configuration to match your testing environment and the targets you wish to analyze. Please make the following adjustments:
    * **LLVM Build Path**: Update the Makefile with the correct path to your LLVM build by setting the appropriate variable to point to the LLVM installation directory.
    * **OpenMP Path**: Locate the src/CMakeLists.txt file and specify both the include path and the library path for OpenMP. This ensures that the analyzer can locate and link against OpenMP during compilation.
    * **MySQL Path**:  Within the same src/CMakeLists.txt file, please provide the include and library paths for MySQL. These settings are used for the analyzer to communicate with the MYSQL database, which are recommended.
    * **Linux Source Path**: Set the LINUX_SOURCE macro in src/lib/utils/Common.cc for kernel analysis, which activate specific kernel indirect call analysis optimizations provided by TFA.
    * **MySQL Database Configuration**: Finally, set up the local MySQL database configuration in src/lib/utils/DBTools.cc. This file should be edited to include the correct database connection details such as the host, username, password, and the database name to be used by the analyzer.
* Building command:
```sh 
	# Build the analysis pass of TFA 
	$ make 
	# Now, you can find the executable, `analyzer`, in `build/lib/`
```
 
### Prepare LLVM bitcode files of target programs

To facilitate effective indirect call analysis, please compile target programs with specific options that preserve maximum information:
* **Recommended Compilation Options**: Compile your target programs with the -O0 optimization level to disable optimizations; this preserves the original structure of the code. Use the -g option to include debugging information in the IRs.
* **Note on TFA Analysis**: While TFA is capable of analyzing code optimized at the -O2 level, please be aware that some information may be totally lost during optimization. This can lead to a small number of false negatives in the analysis results.
* **Bitcode Generation Steps**:
    * LLVM Bitcode Dump: The LLVM bitcode of the target programs can be generated using the LLVM API function WriteBitcodeToFile().
    * Register LLVM Module Pass: Integrate the bitcode dumping API into the LLVM pass manager by registering an LLVM module pass. This will enable the automatic generation of bitcode when compiling your target programs with Clang.
    * Optimization Level Bitcode: To capture bitcode at the -O0 optimization level, register the IR generation pass at the beginning of the optimization pipeline using the **registerPipelineStartEPCallback** API.

By following these guidelines, you will ensure that LLVM bitcode files are suitably prepared for thorough indirect call analysis by the TFA. Additionally, TFA is compatible with LLVM bitcode files produced by other tools, such as [deadline](https://github.com/sslab-gatech/deadline).

### Run the TFA analyzer
```sh
	# To analyze a single bitcode file, say "test.bc", run:
	$ ./build/lib/analyzer test.bc
	# To analyze a list of bitcode files, put the absolute paths of the bitcode files in a file, say "bc.list", then run:
	$ ./build/lib/analyzer @bc.list
```

### Use the analysis results
* The indirect call analysis results are recorded in the **ICallees** map, which is defined in **src/utils/Analyzer.h**. In this map, each key corresponds to an indirect call site, while the associated values are the potential target functions determined by the analysis
* For a comprehensive set of results, data is recorded into a local MYSQL database. The database contains the following tables:
    * **icall_target_table**: This table captures the analysis outcomes for each indirect call. Each record within the table is an indirect call along with its target functions.
    * **caller_table**: To retrieve a list of target functions for a specific indirect call, you can look up the ***func_set_hash*** in this table. The ***func_set_hash*** serves as a unique identifier correlating to a set of target functions affiliated with an indirect call.
    * **func_table**: This table records the alias analysis results for each address-taken function.


### Indirect Call Analysis Configuration

TFA provides flexible analysis configurations through macros defined in`utils/Config.h`. These macros allow users to customize the analysis behavior for different scenarios, balancing between precision, performance, and analysis scope.

#### Key Configuration Groups
1. **Core Analysis Modes**  
   Control fundamental analysis strategies like type and data-flow co-analysis (`ENABLE_DATA_FLOW_ANALYSIS`) and conservative fallback mechanisms (`ENABLE_CONSERVATIVE_ESCAPE_HANDLER`).

2. **Type System Handling**  
   Configure how type casting is interpreted, including function type casting (`ENABLE_FUNCTYPE_CAST`), bidirectional type casting (`ENABLE_BIDIRECTIONAL_TYPE_CASTING`), and void pointer handling (`ENABLE_CAST_ESCAPE`).

3. **Advanced Features**  
   Enable specialized analyses for C++ virtual calls (`ENABLE_VIRTUAL_CALL_ANALYSIS`), variadic functions (`ENABLE_VARIABLE_PARAMETER_ANALYSIS`), and enhanced LLVM IR pattern recognition (`ENABLE_ENHANCED_GEP_ANALYSIS`).

#### Usage Guidelines
- Modify `Config.h` by uncommenting `#define` statements to enable features
- Rebuild the tool after configuration changes
- Combine flags strategically - some features may have interdependencies

Detailed features are marked with comments in the configuration file.


### Other issues and solutions
* When compiling the analysis target with LLVM version 15 or higher, please disable the **opaque pointer** feature to ensure compatibility with the TFA. To do this, append the ***-no-opaque-pointers*** flag to your compiler options. Failure to specify this flag will result in TFA being unable to carry out pointer-to-type analysis. 
* If you meet the following warning in building TFA on MacOS Sequoia, please install lld first (e.g., brew install lld) and add -fuse-ld=lld to the CXX FLAGS in building TFA.
```sh
symbol '__ZnwmSt19__type_descriptor_t' missing from root that overrides /usr/lib/libc++abi.dylib. Use of that symbol in ... is being set to 0xBAD4007.
```

## More details

* [The TFA paper](https://www.usenix.org/system/files/usenixsecurity24-liu-dinghao-improving.pdf)

```sh
@inproceedings{liu2024improving,
  title={Improving $\{$Indirect-Call$\}$ Analysis in $\{$LLVM$\}$ with Type and $\{$Data-Flow$\}$$\{$Co-Analysis$\}$},
  author={Liu, Dinghao and Ji, Shouling and Lu, Kangjie and He, Qinming},
  booktitle={33rd USENIX Security Symposium (USENIX Security 24)},
  pages={5895--5912},
  year={2024}
}
```
