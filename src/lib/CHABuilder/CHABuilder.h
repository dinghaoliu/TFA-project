#ifndef _CHA_BUILDER_H
#define _CHA_BUILDER_H

#include <llvm/IR/DebugInfo.h>
#include <llvm/Pass.h>
#include <llvm/IR/Instructions.h>
#include "llvm/IR/Instruction.h"
#include <llvm/Support/Debug.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Analysis/CallGraph.h>
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"  
#include "llvm/IR/InstrTypes.h" 
#include "llvm/IR/BasicBlock.h" 
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include <llvm/IR/LegacyPassManager.h>
#include "llvm/IR/CFG.h" 
#include "llvm/Transforms/Utils/BasicBlockUtils.h" 
#include "llvm/IR/IRBuilder.h"
#include "llvm/Demangle/Demangle.h"

#include "../utils/Analyzer.h"
#include "../utils/Tools.h"
#include "../utils/Config.h"
#include "../utils/Common.h"

#include <stdlib.h>
#include <set>
#include <map>
#include <vector> 
#include <deque>

class CHABuilderPass : public IterativeModulePass {

	private:

        static map<GlobalVariable*, vector<vector<Value*>>> GlobalVTableMap;
        static map<string, GlobalVariable*> GlobalNameGVMap; // Key: GV name; value: GV llvm value
        static map<string, vector<string>> ParentClassMap; // Key: child class; value: parent class
		static map<string, vector<string>> ChildClassMap;  // Key: parent class; value: child class
        static map<GlobalVariable*, string> ClassNameMap; //Key: class name; va;ue: VTable

        void CPPClassHierarchyRelationCollector(GlobalVariable* GV);
        void CPPVTableBuilder(GlobalVariable* GV);
        void CPPVTableMapping(GlobalVariable* GV);
        string getMethodNameOfPureVirtualCall(GlobalVariable* GV, int idx);
		
	public:
		CHABuilderPass(GlobalContext *Ctx_)
			: IterativeModulePass(Ctx_, "CHABuilder") { }
		virtual bool doInitialization(llvm::Module *);
		virtual bool doFinalization(llvm::Module *);
		virtual bool doModulePass(llvm::Module *);
};

#endif
