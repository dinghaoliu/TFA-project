#ifndef _TYPE_BUILDER_H
#define _TYPE_BUILDER_H

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

#include "../utils/Analyzer.h"
#include "../utils/Tools.h"
#include "../utils/Config.h"
#include "../utils/Common.h"

#include <stdlib.h>
#include <set>
#include <map>

class TypeBuilderPass : public IterativeModulePass {

	typedef struct CompondTy {
		Type* Ty;
		Function* finifunc;
		int type;
	} CompondTy;

	private:
		static map<size_t, DIType*> structDebugInfoMap;
		static map<string, StructType*> identifiedStructType;

		static set<GlobalVariable*> targetGVSet;
		static map<size_t, string> ArrayBaseDebugTypeMap;

		// Use type-based analysis to find targets of indirect calls
		// Multi-layer type analysis supported

		void checkGlobalDebugInfo(GlobalVariable *GV, size_t Tyhash);
		void checkLayeredDebugInfo(GlobalVariable *GV);
		void getLayeredDebugTypeName(DIType *DTy, int idx, size_t Tyhash);

		void matchStructTypes(Type *identifiedTy, User *U);

		//Tools
		string parseIdentifiedStructName(StringRef str_name);
		void updateTypeQueue(Type* Ty, deque<Type*> &Ty_queue);
		bool updateUserQueue(User* U, deque<User*> &U_queue);
		void updateQueues(Type* Ty1, Type* Ty2, deque<Type*> &Ty_queue);
		bool checkValidGV(GlobalVariable* GV);

		//Check if function arg cast to another type (for function pointer args)
		bool checkArgCast(Function *F);
		void combinate(int start, map<unsigned, set<Type*>> CastMap, vector<Type*> &cur_results);

		//Collect global literal structures
		void collectLiteralStruct(Module* M);

	public:

		// General pointer types like char * and void *
		map<Module *, Type *>Int8PtrTy;

		TypeBuilderPass(GlobalContext *Ctx_)
			: IterativeModulePass(Ctx_, "TypeBuilder") { }
		virtual bool doInitialization(llvm::Module *);
		virtual bool doFinalization(llvm::Module *);
		virtual bool doModulePass(llvm::Module *);
};

#endif
