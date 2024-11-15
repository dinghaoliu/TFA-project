#ifndef _CALL_GRAPH_H
#define _CALL_GRAPH_H

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
#include <vector> 
#include <deque>

class CallGraphPass : public IterativeModulePass {

	private:
		const DataLayout *DL;
		// char * or void *
		Type *Int8PtrTy;
		// long interger type
		Type *IntPtrTy;

		static DenseMap<size_t, FuncSet>typeFuncsMap;
		static unordered_map<size_t, set<size_t>>typeConfineMap;
		static unordered_map<size_t, set<size_t>>typeTransitMap;
		static map<string, set<string>>typeNameTransitMap;
		//static unordered_map<size_t, set<size_t>>reverse_typeTransitMap;
		static map<Value*, Type*> TypeHandlerMap;

		//Record VTable info
		static map<string, vector<Value*>> GlobalVTableMap;

		//Key Toty, element: fromTy
		static unordered_map<size_t, set<size_t>>funcTypeCastMap;
		

		static set<size_t>typeEscapeSet;
		static set<string>castEscapeSet;
		static set<size_t>funcTypeCastToVoidSet;
		static set<size_t>funcTypeCastFromVoidSet;
		static FuncSet escapeFuncSet;
		
		//Used in checking return a composite type
		typedef std::pair<Type*, int> CompositeType;
		static DenseMap<size_t, set<CompositeType>> FuncTypesMap; //hash(func_name) with type

		//filter redundant function analysis (use funcname + line number to locate unique icall site)
		static map<string, set<unsigned long long>>globalFuncNameMap;

		//used in func analysis
		static map<Value*, map<Function*, set<size_t>>> Func_Init_Map;

		//Resolve type casting for one-layer results
		static map<string, set<string>> typeStrCastMap;
		static map<int, map<size_t, FunctionType*>> funcTypeMap;

		enum SourceFlag {
		// error returning, mask:0xF
			Internal_Global = 1,
			External_Global = 2,
			Argument = 3,
			Local = 4,
			Return = 5,
			InnerFunction = 6,
			Invalid = 7,
		};

		enum LayerFlag {
			Precise_Mode = 1,
			Recall_Mode = 2,
		};

		static map<string, set<Function*>> globalFuncInitMap;
		static set<string> globalFuncEscapeSet;
		static DenseMap<size_t, FuncSet> argStoreFuncSet;
		static unordered_map<size_t, set<size_t>>argStoreFuncTransitMap;


		/**************  new type method ********************/
		static map<Function*, set<CallInst*>> LLVMDebugCallMap;

		//map type hash to the type pointer
		static map<size_t, Type*> hashTypeMap;
		static map<size_t, pair<Type*, int>> hashIDTypeMap;

		//cluster equal Types, each type maintains a corresponding equal type set
		//Once we parse a new type, check all recorded type and update this map
		static unordered_map<unsigned, set<size_t>>subMemberNumTypeMap; //This faild on union type
	
		//TODO: a new idea: base element type hash

		/************** type escape method ********************/
		static set<size_t> escapedTypesInTypeAnalysisSet; //A subset of escaped type-id set
		static map<size_t, set<StoreInst*>> escapedStoreMap;

		//Record composite type info (a composite type set)
		//This map merge the info of the old typeTransitMap
		//static unordered_map<size_t, map<unsigned, FuncSet>> newtypeFuncsMap;
		//static unordered_map<size_t, set<size_t>>newtypeTransitMap;
		//static DenseMap<unsigned, set<size_t>> newtypeEscapeMap;
		

		// Use type-based analysis to find targets of indirect calls
		// Multi-layer type analysis supported
		void findCalleesByType(llvm::CallInst*, FuncSet&);

		void unrollLoops(Function *F);

		bool checkLoop(Function *F);

		bool topSort(Function *F);

		//bool isCompositeType(Type *Ty);
		bool typeConfineInInitializer(GlobalVariable* GV);
		void CPPVirtualTableHandler(GlobalVariable* GV);
		bool typeConfineInStore(StoreInst *SI);
		void typeConfineInStore_new(StoreInst *SI);
		bool typeConfineInCast(CastInst *CastI);
		bool typeConfineInCast(Type *FromTy, Type *ToTy);
		void escapeType(StoreInst* SI, Type *Ty, int Idx = -1);
		void handleCastEscapeType(Type *ToTy, Type *FromTy);
		void transitType(Type *ToTy, Type *FromTy,
						int ToIdx = -1, int FromIdx = -1);
		
		
		/************** layer analysis method ********************/
		Value *nextLayerBaseType(Value *V, Type * &BTy, int &Idx);
		bool nextLayerBaseType_new(Value *V, list<CompositeType> &TyList, 
			set<Value*> &VisitedSet, LayerFlag Mode = Precise_Mode); // A new implementation
		bool getGEPLayerTypes(GEPOperator *GEP, list<CompositeType> &TyList);
		bool getGEPIndex(Type* baseTy, int offset, Type * &resultTy, int &Idx);
		Type *getBaseType(Value *V, set<Value *> &Visited);
		Type *getPhiBaseType(PHINode *PN, set<Value *> &Visited);
		Type *getFuncPtrType(Value *V);
		Function *getBaseFunction(Value *V);
		Value *recoverBaseType(Value *V);
		void propagateType(Value *ToV, Type *FromTy, int FromIdx, StoreInst* SI);
		bool trackFuncPointer(Value* VO, Value* PO, StoreInst* SI);

		//Support C++ features
		bool getCPPVirtualFunc(Value* V, int &Idx, string &struct_name);
		void resolveVariableParameters(CallInst *CI, FuncSet &FS);
  
		
		void funcSetIntersection(FuncSet &FS1, FuncSet &FS2,
								FuncSet &FS); 
		void funcSetMerge(FuncSet &FS1, FuncSet &FS2);
		bool findCalleesWithMLTA(CallInst *CI, FuncSet &FS);
		void getOneLayerResult(CallInst *CI, FuncSet &FS);
		

		//New added method:
		void checkTypeStoreFunc(Value *V, set<CompositeType> &CTSet);
		void findCalleesWithTwoLayerTA(CallInst *CI, FuncSet PreLayerResult, Type *LayerTy, 
			int FieldIdx, FuncSet &FS, int &LayerNo, int &escape);
		Type *getLayerTwoType(Type* baseTy, vector<int> ids);

		//Tools
		bool isEscape(Type *LayerTy, int FieldIdx, CallInst *CI);
		bool isEscape(string LayerTyName, int FieldIdx, CallInst *CI);
		void updateStructInfo(Function *F, Type* Ty, int idx, Value* context = NULL);
		string parseIdentifiedStructName(StringRef str_name);
		size_t callInfoHash(CallInst* CI, Function *Caller, int index);
		bool checkValidStructName(Type *Ty);
		bool checkValidIcalls(CallInst *CI);

		//Given a func declarition, find its global definition
		void getGlobalFuncs(Function *F, FuncSet &FSet);

		//Debug info handler
		BasicBlock* getParentBlock(Value* V);
		Type* getRealType(Value* V);
		void getDebugCall(Function* F);

	public:
		CallGraphPass(GlobalContext *Ctx_)
			: IterativeModulePass(Ctx_, "CallGraph") { }
		virtual bool doInitialization(llvm::Module *);
		virtual bool doFinalization(llvm::Module *);
		virtual bool doModulePass(llvm::Module *);

		bool escapeChecker(StoreInst* SI, size_t escapeHash);
		void escapeHandler();
		bool oneLayerChecker(CallInst* CI, FuncSet &FS);
		void oneLayerHandler_callgraph();
		bool FuncSetHandler(FuncSet FSet, CallInstSet &callset);
		bool FindNextLayer(Function* F, set<size_t> &HashSet, CallInstSet &callset);
};

#endif
