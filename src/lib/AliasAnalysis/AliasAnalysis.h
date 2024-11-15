#ifndef ALIAS_ANALYSIS_H
#define ALIAS_ANALYSIS_H

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
#include "llvm/IR/IRBuilder.h"

#include <omp.h>
#include "../utils/Analyzer.h"
#include "../utils/Tools.h"

#define MAX_ANALYSIS_NUM 500

typedef struct AliasNode {

    set<Value*> aliasclass;

    AliasNode(){
        aliasclass.clear();
    }

    int count(Value* V){
        return aliasclass.count(V);
    }

    void insert(Value* V){
        aliasclass.insert(V);
    }

    bool empty(){
        return aliasclass.empty();
    }

    void erase(Value* V){
        if(aliasclass.count(V) == 0)
            return;
        
        aliasclass.erase(V);
    }

    void print_set(){
        for(auto it = aliasclass.begin(); it != aliasclass.end(); it++){
            Value *v = *it;
            if(Function *F = dyn_cast<Function>(v)){
                OP<<"func: "<<F->getName()<<"\n";
                continue;
            }
            OP<<*v<<"\n";
        }
    }

} AliasNode;


typedef struct AliasEdge {
    
    AliasNode *fromNode;
    AliasNode *toNode;
    int type; 

    AliasEdge(){
        fromNode = NULL;
        toNode = NULL;
        type = -1;
    }
    
} AliasEdge;

typedef struct AliasContext {

    map<Value*, AliasNode*> NodeMap;
    map<AliasNode*, AliasNode*> ToNodeMap;
    map<AliasNode*, AliasNode*> FromNodeMap;
    bool Is_Analyze_Success;
    AliasFailureReasons failreason;

    set<Function*> AnalyzedFuncSet;
    set<string> Global_symbol_get_funcSet;

    AliasContext(){
        NodeMap.clear();
        ToNodeMap.clear();
        FromNodeMap.clear();
        Is_Analyze_Success = true;
        failreason = none;
        AnalyzedFuncSet.clear();
    }

    ~AliasContext(){
        set<AliasNode*> nodeSet;
        for(auto it = NodeMap.begin(); it != NodeMap.end(); it++){
            nodeSet.insert(it->second);
        }

        for(AliasNode* n : nodeSet){
            delete n;
        }
    }

} AliasContext;

//Functionality tests
void FunctionAliasAnalysis(Function* F, AliasContext *aliasCtx, GlobalContext *Ctx);
void ModuleAliasAnalysis(GlobalContext *Ctx);


//Start point
void ICallAliasAnalysis(GlobalContext *Ctx);
void FuncAliasAnalysis(GlobalContext *Ctx);
void FuncTypeAnalysis(GlobalContext *Ctx);
void HandleICall(CallInst* icall, AliasContext *aliasCtx, 
    GlobalContext *Ctx, FuncSet &fset, omp_lock_t *lock);
void HandleFunc(Function* F, AliasContext *aliasCtx, 
    GlobalContext *Ctx, CallInstSet &callset, omp_lock_t *lock);
void HandleFuncTypes(Function* F, 
    GlobalContext *Ctx, CallInstSet &callset, omp_lock_t *lock);

//When precise alias analysis failed, we use the following type-info-aimed data-flow analysis
void oneLayerHandler(GlobalContext *Ctx);
void FuncDFHandler(GlobalContext *Ctx);

//InstHandler
void HandleInst(Instruction* I, AliasContext *aliasCtx, GlobalContext *Ctx);
void HandleLoad(LoadInst* LI, AliasContext *aliasCtx);
void HandleStore(StoreInst* STI, AliasContext *aliasCtx);
void HandleStore(Value* vop, Value* pop, AliasContext *aliasCtx);
void HandleGEP(GetElementPtrInst* GEP, AliasContext *aliasCtx);
void HandleGEP(GEPOperator* GEP, AliasContext *aliasCtx);
void HandleAlloc(AllocaInst* ALI, AliasContext *aliasCtx);
void HandleCai(CallInst* CAI, AliasContext *aliasCtx, GlobalContext *Ctx);
void HandleMove(Value* v1, Value* v2, AliasContext *aliasCtx);
void HandleReturn(Function* F, CallInst* cai, AliasContext *aliasCtx);

void HandleOperator(Value* v, AliasContext *aliasCtx);

//Interprocedural analysis
void getClusterNodes(AliasNode* startNode, set<AliasNode*> &nodeSet, AliasContext *aliasCtx);
void getClusterValues(Value* v, set<Value*> &valueSet, AliasContext *aliasCtx);
void interCaseHandler(Value* aliased_v, list<Value *> &LV, set<Value *>Analyzed_Set, 
     AliasContext *aliasCtx, GlobalContext *Ctx);
void analyzeFunction(Function* F, AliasContext *aliasCtx, GlobalContext *Ctx);
void analyzeGlobalInitializer(GlobalVariable* GV, list<Value *>&Future_analysis_list,  AliasContext *aliasCtx);

//Tools
AliasNode* getNode(Value *V, AliasContext *aliasCtx);
bool isUselessInst(Instruction* I, GlobalContext *Ctx);
void mergeNode(AliasNode* n1, AliasNode* n2, AliasContext *aliasCtx);
void valueSetMerge(set<Value*> &S1, set<Value*> &S2);
void funcSetMerge(FuncSet &S1, FuncSet &S2);
unsigned getArgIndex(Function* F, Argument *Arg);
unsigned getMin(unsigned n1, unsigned n2);
void getGlobalFuncs(Function *F, FuncSet &FSet, GlobalContext *Ctx);
string getGlobalMacroSource(GlobalVariable* GV);
bool checkNodeConnectivity(AliasNode* node1, AliasNode* node2, AliasContext *aliasCtx);

#endif
