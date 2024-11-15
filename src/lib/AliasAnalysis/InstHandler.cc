#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LegacyPassManager.h>

#include "AliasAnalysis.h"

void HandleOperator(Value* v, AliasContext *aliasCtx){

    GEPOperator *GEPO = dyn_cast<GEPOperator>(v);
    if(GEPO){
        HandleGEP(GEPO, aliasCtx);
        HandleOperator(GEPO->getOperand(0), aliasCtx);
    }

    BitCastOperator *CastO = dyn_cast<BitCastOperator>(v);
    if(CastO){
        HandleMove(CastO, CastO->getOperand(0), aliasCtx);
        HandleOperator(CastO->getOperand(0), aliasCtx);
    }

    PtrToIntOperator *PTIO = dyn_cast<PtrToIntOperator>(v);
    if(PTIO){
        HandleMove(PTIO, PTIO->getOperand(0), aliasCtx);
        HandleOperator(PTIO->getOperand(0), aliasCtx);
    }
}

void HandleInst(Instruction* I, AliasContext *aliasCtx, GlobalContext *Ctx){

    //First filter instructions that do not need to consider
    if(isUselessInst(I, Ctx))
        return;

    // Handle GEP and Cast operator
    // Arguments of a call are also caught here
    int opnum = I->getNumOperands();
    for(int i = 0; i < I->getNumOperands(); i++){
        Value* op = I->getOperand(i);
        HandleOperator(op, aliasCtx);
    }

    //Handle target instruction
    AllocaInst* ALI = dyn_cast<AllocaInst>(I);
    if(ALI){
        HandleAlloc(ALI, aliasCtx);
        return;
    }

    StoreInst *STI = dyn_cast<StoreInst>(I);
    if(STI){
        HandleStore(STI, aliasCtx);
        return;
    }

    LoadInst* LI = dyn_cast<LoadInst>(I);
    if(LI){
        HandleLoad(LI, aliasCtx);
        return;
    }

    GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(I);
    if(GEP){
        HandleGEP(GEP, aliasCtx);
        return;
    }

    BitCastInst *BCI = dyn_cast<BitCastInst>(I);
    ZExtInst *ZEXI = dyn_cast<ZExtInst>(I);
    SExtInst *SEXI = dyn_cast<SExtInst>(I);
    TruncInst *TRUI = dyn_cast<TruncInst>(I);
    IntToPtrInst *ITPI = dyn_cast<IntToPtrInst>(I);
    PtrToIntInst *PTII = dyn_cast<PtrToIntInst>(I);
    if(BCI || ZEXI || SEXI || TRUI || ITPI || PTII){
        auto op = I->getOperand(0);
        HandleMove(I, op, aliasCtx);
        return;
    }

    PHINode *PHI = dyn_cast<PHINode>(I);
    if(PHI){
        for (unsigned i = 0, e = PHI->getNumIncomingValues(); i != e; ++i){
            Value *IV = PHI->getIncomingValue(i);
            HandleMove(I, IV, aliasCtx);
        }
        return;
    }

    SelectInst *SLI = dyn_cast<SelectInst>(I);
    if(SLI){
        auto TV = SLI->getTrueValue();
        auto FV = SLI->getFalseValue();
        HandleMove(I, TV, aliasCtx);
        HandleMove(I, FV, aliasCtx);
        return;
    }

    CallInst *CAI = dyn_cast<CallInst>(I);
    if(CAI){
        HandleCai(CAI, aliasCtx, Ctx);
        return;
    }

    ReturnInst *RI = dyn_cast<ReturnInst>(I);
    if(RI){
        Value* return_v = RI->getReturnValue();
        if(return_v == NULL)
            return;
        
        if(isa<ConstantData>(return_v))
            return;
            
        HandleMove(I, return_v, aliasCtx);
    }

}

void HandleAlloc(AllocaInst* ALI, AliasContext *aliasCtx){
    
    if(getNode(ALI, aliasCtx) == NULL){
        AliasNode* node = new AliasNode();
        node->insert(ALI);
        aliasCtx->NodeMap[ALI] = node;
    }
}

// v1 = *v2
void HandleLoad(LoadInst* LI, AliasContext *aliasCtx){
    
    AliasNode* node1 = getNode(LI, aliasCtx);
    if(node1 == NULL){
        node1 = new AliasNode();
        node1->insert(LI);
        aliasCtx->NodeMap[LI] = node1;
    }

    Value* op = LI->getOperand(0);
    AliasNode* node2 = getNode(op, aliasCtx);
    if(node2 == NULL){
        node2 = new AliasNode();
        node2->insert(op);
        aliasCtx->NodeMap[op] = node2;
    }

    //node2 has pointed to some nodes
    if(aliasCtx->ToNodeMap.count(node2)){
        AliasNode* node2_toNode = aliasCtx->ToNodeMap[node2];
        mergeNode(node1 ,node2_toNode, aliasCtx);
    }
    //For load, this usually does not happen
    else if(aliasCtx->FromNodeMap.count(node1)){
        AliasNode* node1_fromNode = aliasCtx->FromNodeMap[node1];
        mergeNode(node1_fromNode, node2, aliasCtx);
    }
    else{
        aliasCtx->ToNodeMap[node2] = node1;
        aliasCtx->FromNodeMap[node1] = node2;
    }
}

// *v2 = v1
void HandleStore(StoreInst* STI, AliasContext *aliasCtx){
    
    //store vop to pop
    Value* vop = STI->getValueOperand(); //v1
    Value* pop = STI->getPointerOperand(); //v2

    if(isa<ConstantData>(vop))
        return;

    AliasNode* node1 = getNode(vop, aliasCtx);
    if(node1 == NULL){
        node1 = new AliasNode();
        node1->insert(vop);
        aliasCtx->NodeMap[vop] = node1;
    }

    AliasNode* node2 = getNode(pop, aliasCtx);
    if(node2 == NULL){
        node2 = new AliasNode();
        node2->insert(pop);
        aliasCtx->NodeMap[pop] = node2;
    }

    //Under test
    if(checkNodeConnectivity(node1, node2, aliasCtx)){
        return;
    }

    //node2 has pointed to some nodes
    if(aliasCtx->ToNodeMap.count(node2)){
        AliasNode* node2_toNode = aliasCtx->ToNodeMap[node2];
        mergeNode(node1 ,node2_toNode, aliasCtx);
    }
    else if(aliasCtx->FromNodeMap.count(node1)){
        AliasNode* node1_fromNode = aliasCtx->FromNodeMap[node1];
        mergeNode(node1_fromNode, node2, aliasCtx);
    }
    else{
        aliasCtx->ToNodeMap[node2] = node1;
        aliasCtx->FromNodeMap[node1] = node2;
    }
}

//store vop to pop
void HandleStore(Value* vop, Value* pop, AliasContext *aliasCtx){

    if(isa<ConstantData>(vop))
        return;

    AliasNode* node1 = getNode(vop, aliasCtx);
    if(node1 == NULL){
        node1 = new AliasNode();
        node1->insert(vop);
        aliasCtx->NodeMap[vop] = node1;
    }

    AliasNode* node2 = getNode(pop, aliasCtx);
    if(node2 == NULL){
        node2 = new AliasNode();
        node2->insert(pop);
        aliasCtx->NodeMap[pop] = node2;
    }

    //Under test
    if(checkNodeConnectivity(node1, node2, aliasCtx)){
        return;
    }

    //node2 has pointed to some nodes
    if(aliasCtx->ToNodeMap.count(node2)){
        AliasNode* node2_toNode = aliasCtx->ToNodeMap[node2];
        mergeNode(node1 ,node2_toNode, aliasCtx);
    }
    else if(aliasCtx->FromNodeMap.count(node1)){
        AliasNode* node1_fromNode = aliasCtx->FromNodeMap[node1];
        mergeNode(node1_fromNode, node2, aliasCtx);
    }
    else{
        aliasCtx->ToNodeMap[node2] = node1;
        aliasCtx->FromNodeMap[node1] = node2;
    }
}

// v1 = &v2->f
void HandleGEP(GetElementPtrInst* GEP, AliasContext *aliasCtx){

    HandleMove(GEP, GEP->getPointerOperand(), aliasCtx);
}

// v1 = &v2->f
void HandleGEP(GEPOperator* GEP, AliasContext *aliasCtx){
    HandleMove(GEP, GEP->getPointerOperand(), aliasCtx);
}

// v1 = v2
void HandleMove(Value* v1, Value* v2, AliasContext *aliasCtx){

    AliasNode* node2 = getNode(v2, aliasCtx);
    if(node2 == NULL){
        node2 = new AliasNode();
        node2->insert(v2);
        aliasCtx->NodeMap[v2] = node2;
    }


    AliasNode* node1 = getNode(v1, aliasCtx);
    if(node1 == NULL){
        node2->insert(v1);
        aliasCtx->NodeMap[v1] = node2;
        return;
    }

    if(node1 == node2)
        return;
    
    if(checkNodeConnectivity(node1, node2, aliasCtx)){
        return;
    }

    mergeNode(node1, node2, aliasCtx);

}

void HandleCai(CallInst* CAI, AliasContext *aliasCtx, GlobalContext *Ctx){
    
    if(getNode(CAI, aliasCtx) == NULL){
        AliasNode* node = new AliasNode();
        node->insert(CAI);
        aliasCtx->NodeMap[CAI] = node;
    }

    for (User::op_iterator OI = CAI->op_begin(), OE = CAI->op_end(); OI != OE; ++OI) {
        if(getNode(*OI, aliasCtx) == NULL){
        AliasNode* node = new AliasNode();
        node->insert(*OI);
        aliasCtx->NodeMap[*OI] = node;
    }
    }

    // Resolve mem copy functions
    // Usually a copy func is like: copy_func(dst, src, len)
    StringRef FName = getCalledFuncName(CAI);
    if(Ctx->CopyFuncs.count(FName.str())){
        HandleMove(CAI->getArgOperand(0), CAI->getArgOperand(1), aliasCtx);
        return;
    }

    if(FName == "__symbol_get"){
        string symbol_str = symbol_get_hander(CAI);
        if(symbol_str.size() == 0)
            return;

        if(Ctx->GlobalFuncs.count(symbol_str)){
            for(size_t hash : Ctx->GlobalFuncs[symbol_str]){
                Function* f = Ctx->Global_Unique_Func_Map[hash];
                HandleMove(CAI, f, aliasCtx);
                aliasCtx->Global_symbol_get_funcSet.insert(symbol_str);
            }
        }
        else{
            //OP<<"Ctx->GlobalFuncs empty\n";
        }
    }

}

void HandleReturn(Function* F, CallInst* cai, AliasContext *aliasCtx){

    for (inst_iterator i = inst_begin(F), ei = inst_end(F); i != ei; ++i) {
        ReturnInst *RI = dyn_cast<ReturnInst>(&*i);
        if(RI){
            Value* return_v = RI->getReturnValue();
            if(return_v){
                HandleMove(return_v, cai, aliasCtx);
            }
        }
    }

}