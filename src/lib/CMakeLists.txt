file (GLOB TypeBuilder TypeBuilder/*.h TypeBuilder/*.cc)
file (GLOB CallGraph CallGraph/*.h CallGraph/*.cc)
file (GLOB CHABuilder CHABuilder/*.h CHABuilder/*.cc)
file (GLOB AliasAnalysis AliasAnalysis/*.h AliasAnalysis/*.cc)
file (GLOB utils utils/*.hpp utils/*.h utils/*.cc)

set (AnalyzerSourceCodes
  ${TypeBuilder}
  ${CHABuilder}
	${CallGraph}
  ${AliasAnalysis}
  ${utils}
)

set(AnalyzerMain Analyzer.cc)

# Build executable analyzer
add_executable(analyzer ${AnalyzerMain} ${AnalyzerSourceCodes})

llvm_map_components_to_libnames(llvm_libs core support analysis irreader)

target_link_libraries(analyzer
  PUBLIC
  ${llvm_libs}
  mysqlclient
)