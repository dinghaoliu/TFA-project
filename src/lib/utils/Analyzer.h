#ifndef _ANALYZER_GLOBAL_H
#define _ANALYZER_GLOBAL_H

#include <llvm/IR/DebugInfo.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Instructions.h>
#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Analysis/AliasAnalysis.h>
#include "llvm/Support/CommandLine.h"
#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include "Common.h"

// 
// typedefs
//
typedef std::vector< std::pair<llvm::Module*, llvm::StringRef> > ModuleList;
// Mapping module to its file name.
typedef std::unordered_map<llvm::Module*, llvm::StringRef> ModuleNameMap;
// The set of all functions.
typedef llvm::SmallPtrSet<llvm::Function*, 8> FuncSet;
// Mapping from function name to function.
typedef std::unordered_map<std::string, set<size_t>> NameFuncMap;
typedef llvm::SmallPtrSet<llvm::CallInst*, 8> CallInstSet;
typedef llvm::DenseMap<llvm::Function*, CallInstSet> CallerMap;
typedef llvm::DenseMap<llvm::CallInst *, FuncSet> CalleeMap;
// Pointer analysis types.
typedef std::map<llvm::Value *, std::set<llvm::Value *>> PointerAnalysisMap;
typedef std::map<llvm::Function *, PointerAnalysisMap> FuncPointerAnalysisMap;
typedef std::map<llvm::Function *, AAResults *> FuncAAResultsMap;
typedef std::map<llvm::Function *, PointerAnalysisMap> FuncStructAnalysisMap;
// init-fini analysis    
typedef std::map<ConstantStruct*, std::set<Function*>> StructInfoMap;
typedef std::map<Module*, StructInfoMap> ModuleInfoMap;

enum PairType{
	UNKNOWN = 0,
    MODULE_FUNC = 1,
	MODULE_FUNC_WRAPPER = 2,
};

enum TypeAnalyzeResult{
	TypeEscape = 0,
    OneLayer = 1,
	TwoLayer = 2,
	ThreeLayer = 3,
	NoTwoLayerInfo = 4,
	MissingBaseType = 5,
	MixedLayer = 6,
	VTable = 7,
};

enum AliasFailureReasons{
	none = 0,
	F_has_no_global_definition = 1,
    binary_operation = 2,
	llvm_used = 3,
	exceed_max = 4,
	success = 5,
	ignore_analysis = 6,
	inline_asm = 7,
	init_in_asm = 8,
	strip_invariant_group = 9,
};

typedef struct PairInfo {
	Function* initfunc;
	Function* finifunc;
	int type;

	PairInfo(){
		initfunc = NULL;
		finifunc = NULL;
		type = UNKNOWN;
	}

	PairInfo(Function* initf, Function* finif, int T){
		initfunc = initf;
		finifunc = finif;
		type = T;
	}

	friend bool operator< (const PairInfo& PI1, const PairInfo& PI2){
		if (PI1.initfunc != PI2.initfunc)
        	return (PI1.initfunc < PI2.initfunc);
		else
			return (PI1.finifunc < PI2.finifunc);
    }

} PairInfo;


struct GlobalContext {

	GlobalContext() {
		
		// Initialize statistucs.
		NumFunctions = 0;

		Modules.clear();
		ModuleMaps.clear();
		Loopfuncs.clear();
		Longfuncs.clear();

		Global_Pair_Set.clear();
		Global_Func_Pair_Set.clear();
		Global_Keywords_Map.clear();
	}

	unsigned NumFunctions;

	set<string> SafeMacros;

	vector<string> IcallIgnoreFileLoc;
	vector<string> IcallIgnoreLineNum;

	// Map global function name to function defination.
	std::unordered_map<std::string, Function*> GlobalFuncs_old;
  	NameFuncMap GlobalFuncs;
	map<size_t, Function*> Global_Unique_Func_Map;
	map<size_t, set<GlobalValue*>> Global_Unique_GV_Map;
	set<Function*> Global_AddressTaken_Func_Set;

	// Functions whose addresses are taken.
	FuncSet AddressTakenFuncs;

	// Map a callsite to all potential callee functions.
	// CalleeMap Type: llvm::DenseMap<llvm::CallInst *, FuncSet>;
	// Given a call inst, find its definition function (1 is the best)
	CalleeMap Callees;

	// Map a function to all potential caller instructions.
	// CallerMap Type: llvm::DenseMap<llvm::Function*, CallInstSet>
	// Given a function pointer F, collect all of its call site
	CallerMap Callers;

	//Only record indirect call
	CallerMap ICallers;
	CalleeMap ICallees;

	// Indirect call instructions.
	std::vector<CallInst *>IndirectCallInsts;

	// Modules.
	ModuleList Modules;
	ModuleNameMap ModuleMaps;
	std::set<std::string> InvolvedModules;

	std::map<std::string, uint8_t> MemWriteFuncs;
	std::set<std::string> CriticalFuncs;

	// Pinter analysis results.
	FuncPointerAnalysisMap FuncPAResults;
	FuncAAResultsMap FuncAAResults;
	FuncStructAnalysisMap FuncStructResults;

	// Unified functions -- no redundant inline functions
	DenseMap<size_t, Function *>UnifiedFuncMap;
	set<Function *>UnifiedFuncSet;

	// Map function signature to functions
	DenseMap<size_t, FuncSet>sigFuncsMap;
	DenseMap<size_t, FuncSet>oneLayerFuncsMap;

	// Functions handling errors
	map<string, tuple<int8_t, int8_t, int8_t>> CopyFuncs;

	/******Path pair analysis methods******/
	set<Function *> Loopfuncs;
	set<Function *> Longfuncs;
	set<string> HeapAllocFuncs;
	set<string> DebugFuncs;
	set<string> BinaryOperandInsts;

	/******Init-fini analysis methods******/
	unsigned num_1_pairs = 0;
	unsigned num_2_pairs = 0;
	unsigned num_3_pairs = 0;
	unsigned num_4_pairs = 0;
	unsigned num_5_pairs = 0;
	unsigned num_more_pairs = 0;

	/******Type Builder methods******/
	DenseMap<size_t, string> Global_Literal_Struct_Map; //map literal struct ('s hash) and its name
	map<string, StructType*> Global_Identified_Struct_Map;
	DenseMap <size_t, FuncSet> Global_EmptyTy_Funcs;
	set<size_t> Globa_Union_Set;
	map<Function*, set<size_t>> Global_Arg_Cast_Func_Map;
	map<Type*, Module*> Global_Ty_Module_Map;

	/******Virtual func handling methods******/
	//  class name, method name, vf func     
	map<string, map<string, set<Value*>>> Global_VTable_Map;
	map<string, map<int, set<string>>> Global_Class_Method_Index_Map;
	map<string, set<string>> GlobalClassHierarchyMap; // Key: parent lcass name; value: derived class name

	/******icall methods******/
	unsigned long long icallTargets = 0;
	unsigned long long icallTargets_OneLayer = 0;
	unsigned long long icallNumber = 0;
	unsigned long long valied_icallNumber = 0;
	unsigned long long valied_icallTargets = 0;

	unsigned long long num_haveLayerStructName = 0;
	unsigned long long num_emptyNameWithDebuginfo = 0;
	unsigned long long num_emptyNameWithoutDebuginfo = 0;
	unsigned long long num_local_info_name = 0;
	unsigned long long num_array_prelayer = 0;
	unsigned long long num_escape_store = 0;

	unsigned long long num_typebuilder_haveStructName = 0;
	unsigned long long num_typebuilder_haveNoStructName = 0;

	set <StructInfoMap> Global_Pair_Set;
	set <PairInfo> Global_Func_Pair_Set;
	set <string> Global_Debug_Message_Set;
	map <string,int> Global_Keywords_Map;

	CalleeMap largeTargetsICalls;

	/****** ICall data-flow-analysis related******/
	//TODO: support func return value analysis
	map <string, FuncSet> Global_GV_Func_Map;  //map global variable and func pointer
	map <size_t, FuncSet> Global_Arg_Func_Map; //map func arg (hash of  callinst + argid) and input func pointer
	map <string, set<string>> Global_Trans_Global_To_Global_Map; //map global variable comes from other global variable
	map <string, set<size_t>> Global_Trans_Global_To_Arg_Map; //map global variable comes from func args
	map <string, set<string>> Global_Trans_Global_To_Retuen_Map; //map  global variable comes from func return values
	map <size_t, set<size_t>> Global_Trans_Arg_To_Arg_Map; //map func arg to another func arg
	map <size_t, set<string>> Global_Trans_Arg_To_Global_Map; //map func arg to a global variable
	map <size_t, set<string>> Global_Trans_Arg_To_Return_Map; //map func arg to a global variable

	//We do not analysis the whole kernel, only focus some intereting targets
	//These two set could be enlarged during analysis
	set <string> Global_Target_GV_Set;  //Use name to distinguish global variable
	
	//FIXME: a func may have multiple func pointer args
	set <size_t> Global_Target_Call_Set; // Hasd of call func name and the arg id
	set <size_t> Global_Analyzed_Call_Set; // One call analyze once
	set <size_t> Global_Analyzed_Global_Set; // One global analyze once

	//If global variable or func arg comes from the return value of func, abandon them
	set <string> Global_Escape_GV_Set;
	set <size_t> Global_Escape_Call_Set;

	bool analysis_Target_Update_Tag = false;

	map<llvm::CallInst*, int> Global_Alias_Results_Map;

	/****** ICall Statistic Result ******/
	map <CallInst*, TypeAnalyzeResult> Global_MLTA_Reualt_Map;
	

	/****** Alias Statistic Result ******/
	unsigned long long icall_support_dataflow_Number = 0;
	unsigned long long func_support_dataflow_Number = 0;
	map <AliasFailureReasons, size_t> failure_reasons;
	map <CallInst*, AliasFailureReasons> Global_ICall_Alias_Result_Map;
	map <Function*, AliasFailureReasons> Global_Func_Alias_Result_Map;
	map <Function*, set<CallInst*>> Global_Func_Alias_Frequency_Map;
	set<string> Global_symbol_get_funcSet;

	//Record funcs that failed in FuncAliasAnalysis
	set<size_t> Global_failed_Func_Set;

	//Debug info
	map<string, Type*> Global_Literal_Struct_Name_Map;
	map<Function *, map<Value *, Value *>>AliasStructPtrMap;
	unsigned long long Global_missing_type_def_struct_num = 0;
	int Global_pre_anon_icall_num = 0;

	//Time analysis
	double Load_time = 0;
	double Typebuilder_time = 0;
	double MLTA_time = 0;
	double Icall_alias_time = 0;
	double Func_alias_time = 0;
	double Escape_alias_time = 0;
	double Onelayer_alias_time = 0;
	double Database_time = 0;

};

class IterativeModulePass {
	protected:
		GlobalContext *Ctx;
		const char * ID;
	public:
		IterativeModulePass(GlobalContext *Ctx_, const char *ID_)
			: Ctx(Ctx_), ID(ID_) { }

		// Run on each module before iterative pass.
		virtual bool doInitialization(llvm::Module *M)
		{ return true; }

		// Run on each module after iterative pass.
		virtual bool doFinalization(llvm::Module *M)
		{ return true; }

		// Iterative pass.
		virtual bool doModulePass(llvm::Module *M)
		{ return false; }

		virtual void run(ModuleList &modules);
};

#endif
