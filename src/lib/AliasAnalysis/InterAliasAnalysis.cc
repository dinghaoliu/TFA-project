#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LegacyPassManager.h>

#include "AliasAnalysis.h"

void analyzeFunction(Function* F, AliasContext *aliasCtx, GlobalContext *Ctx){

    if(!F)
        return;

    if(aliasCtx->AnalyzedFuncSet.count(F))
        return;

    for (inst_iterator i = inst_begin(F), ei = inst_end(F); i != ei; ++i) {
        Instruction *iInst = dyn_cast<Instruction>(&*i);
        HandleInst(iInst, aliasCtx, Ctx);
    }
    aliasCtx->AnalyzedFuncSet.insert(F);

    //After we've analyzed F, we need to find related globals, calls and args to
    //extend our analysis if necessary

}

void getClusterNodes(AliasNode* startNode, set<AliasNode*> &nodeSet, AliasContext *aliasCtx){

	if(startNode == NULL)
		return;
	
	nodeSet.insert(startNode);

	list<AliasNode *>LN;
	LN.push_back(startNode);
	set<AliasNode *> PN;
	PN.clear();

	while (!LN.empty()) {
		AliasNode *CN = LN.front();
		LN.pop_front();

		if (PN.find(CN) != PN.end()){
			continue;
		}
		PN.insert(CN);

		if(aliasCtx->ToNodeMap.count(CN)){
			LN.push_back(aliasCtx->ToNodeMap[CN]);
			nodeSet.insert(aliasCtx->ToNodeMap[CN]);
		}

		if(aliasCtx->FromNodeMap.count(CN)){
			LN.push_back(aliasCtx->FromNodeMap[CN]);
			nodeSet.insert(aliasCtx->FromNodeMap[CN]);
		}
	}
}

void getClusterValues(Value* v, set<Value*> &valueSet, AliasContext *aliasCtx){

    if(v == NULL)
        return;

    AliasNode *n = getNode(v, aliasCtx);
	if(!n)
		return;

	//Get the cluster of values to enable inter-procedural analysis
	set<AliasNode*> targetNodeSet;
	targetNodeSet.clear();
	getClusterNodes(n, targetNodeSet, aliasCtx);
	
	valueSet.clear();
	for(auto it = targetNodeSet.begin(); it != targetNodeSet.end(); it++){
		AliasNode *n = *it;
		valueSetMerge(valueSet, n->aliasclass);
	}
}


void interCaseHandler(Value* aliased_v, list<Value *>&LV, 
    set<Value *>Analyzed_Set, AliasContext *aliasCtx, GlobalContext *Ctx){


    if(aliased_v == NULL)
        return;

    if (isa<ConstantData>(aliased_v)){
        return;
    }

    //OP<<"aliased_v: "<<*aliased_v<<"\n";
    
    //The aliased value is a function
    //Filter this case from checking GlobalValue
    Function *F = dyn_cast<Function>(aliased_v);
    if(F){

        if(!F->isDeclaration()){
            return;
        }

        //F is a function declare
        StringRef FName = F->getName();

        //F has definition
        if(Ctx->GlobalFuncs.count(FName.str())){
            return;
        }
        fail_tag:
        //Here F has no global definition,
        //F only has a declarition, we conservatively think
        //there exists assembly code to init the icall

        //New: This tag does not influence the soudness, just stop further analysis
        return;
    }

    Instruction* I = dyn_cast<Instruction>(aliased_v);
    if(I){
        auto opcodeName = I->getOpcodeName();
        if(Ctx->BinaryOperandInsts.count(opcodeName)){
            aliasCtx->Is_Analyze_Success = false;
            aliasCtx->failreason = binary_operation;
            return;
        }
    }
    

    //Check if the aliased value is the argument of some callee functions
    list<User *>LU;
    set<User *> GU;
    for(User *aliased_U : aliased_v->users()){
        LU.push_back(aliased_U);
    }

    while (!LU.empty()) {
        User *aliased_U = LU.front();
        LU.pop_front();

        if (GU.find(aliased_U) != GU.end()){
            continue;
        }
        GU.insert(aliased_U);

        Instruction* I = dyn_cast<Instruction>(aliased_U);
        if(I){
            auto opcodeName = I->getOpcodeName();
            if(Ctx->BinaryOperandInsts.count(opcodeName)){
                aliasCtx->Is_Analyze_Success = false;
                aliasCtx->failreason = binary_operation;
                return;
            }
        }

    //for(User *aliased_U : aliased_v->users()){

        if(aliased_U == aliased_v)
            continue;

        GEPOperator *GEPO = dyn_cast<GEPOperator>(aliased_U);
        BitCastOperator *CastO = dyn_cast<BitCastOperator>(aliased_U);
        PtrToIntOperator *PTIO = dyn_cast<PtrToIntOperator>(aliased_U);
        ConstantAggregate *CA = dyn_cast<ConstantAggregate>(aliased_U);
        
        if(GEPO || CastO || PTIO || CA ){
            for(User *u : aliased_U->users()){
                LU.push_back(u);
            }
            continue;
        }

        CallInst *cai = dyn_cast<CallInst>(aliased_U);
        if(!cai)
            continue;
        
        //OP<<"User check2\n";
        //OP<<"User: "<<*aliased_U<<"\n";
        //OP<<"used in cai: "<<*cai<<"\n";
        if(cai->isInlineAsm()){
            //OP<<"is inline\n";
            aliasCtx->Is_Analyze_Success = false;
            aliasCtx->failreason = inline_asm;
            return;
        }


        unsigned argnum = cai->arg_size();

        for(Function *f : Ctx->Callees[cai]){

            HandleReturn(f, cai, aliasCtx);

            //OP<<"Used in func call: "<<f->getName()<<"\n";

            analyzeFunction(f, aliasCtx, Ctx);

            //f's caller is cai, so we do not need to analysis its args in the futhure
            vector<Value *>f_arg_vec;
            f_arg_vec.clear();
            for(auto it = f->arg_begin(); it != f->arg_end(); it++){
                f_arg_vec.push_back(it);
                Analyzed_Set.insert(it);
            }

            auto f_arg_size = f->arg_size();
            unsigned min_num = getMin(f_arg_size, argnum);
            for(unsigned j = 0; j < min_num; j++){
                Value* cai_arg = cai->getArgOperand(j);
                HandleMove(cai_arg, f_arg_vec[j], aliasCtx);
                //OP<<"move handled\n";
            }

            //Now the graph has been extened, update analysis targets
            set<Value*> Covered_value_Set;
            Covered_value_Set.clear();
            valueSetMerge(Covered_value_Set, Analyzed_Set);
            for(auto it = LV.begin(); it != LV.end(); it++){
                Covered_value_Set.insert(*it);
            }

            set<Value*> targetValueSet;
	        getClusterValues(aliased_v, targetValueSet, aliasCtx);
            for(auto it = targetValueSet.begin(); it != targetValueSet.end(); it++){
                Value* target_v = *it;
                if(Covered_value_Set.count(target_v))
                    continue;
                
                LV.push_back(target_v);
            }

            if(LV.size() > MAX_ANALYSIS_NUM){
                aliasCtx->Is_Analyze_Success = false;
                //OP<<"Failed: exceed MAX_ANALYSIS_NUM\n";
                aliasCtx->failreason = exceed_max;
                break;
            }

        }
        Analyzed_Set.insert(aliased_U);
    }

    //The aliased value is not a function. but is a global variable
    GlobalVariable* GV = dyn_cast<GlobalVariable>(aliased_v);
    if(GV){

        if(GV->getName().contains("__UNIQUE_ID___addressable")){

            //Find the macro
            string source = getGlobalMacroSource(GV);
            if(source.size() != 0){
                for(string safe_macro : Ctx->SafeMacros){
                    if(checkStringContainSubString(source, safe_macro)){
                        //Aliased with a safe macro, stop further analysis
                        return;
                    }
                }
            }
            else{
                //OP<<"empty source\n";
            }
        }

        if(GV->getName() == "llvm.used" || GV->getName() == "llvm.compiler.used" 
            || GV->getName().contains("__start_set")){
            aliasCtx->Is_Analyze_Success = false;
            aliasCtx->failreason = llvm_used;
            return;
        }

        //Check potential init in asm
        //In this case, we only have a external GV without any init info
        auto lkTy = GV->getLinkage();
        if(lkTy == llvm::GlobalValue::ExternalLinkage){
            auto GID = GV->getGUID();
            if(Ctx->Global_Unique_GV_Map[GID].size() == 1){
                aliasCtx->Is_Analyze_Success = false;
                aliasCtx->failreason = init_in_asm;
                return;
            }
        }

        //Get all global uses of GV
        auto GID = GV->getGUID();
        for(auto it = Ctx->Global_Unique_GV_Map[GID].begin(); it != Ctx->Global_Unique_GV_Map[GID].end(); it++){
            GlobalValue* Global_Value = *it;
            GlobalVariable* Global_GV = dyn_cast<GlobalVariable>(Global_Value);
            if(!Global_GV)
                continue;

            //Globals in different modules are aliased with each other
            if(Global_GV != GV){
                HandleMove(Global_GV, GV, aliasCtx);
            }

            //First check the Initializer of the GV
            if(Global_GV->hasInitializer()){
                //TODO: resolve initializer
                analyzeGlobalInitializer(Global_GV, LV, aliasCtx);
            }

            //Use BFS to execute use-chain check
            list<User *>LU;
            set<User *> GU;
            for(User *U : Global_GV->users()){
                LU.push_back(U);
            }

            while (!LU.empty()) {
                User *U = LU.front();
                LU.pop_front();

                if (GU.find(U) != GU.end()){
                    continue;
                }
                GU.insert(U);

                Instruction *iInst = dyn_cast<Instruction>(U);
                if(iInst){

                    Function* icaller = iInst->getFunction();
                    analyzeFunction(icaller, aliasCtx, Ctx);

                    //Now the graph has been extended, update the analysis targets
                    set<Value*> Covered_value_Set;
                    Covered_value_Set.clear();
                    valueSetMerge(Covered_value_Set, Analyzed_Set);
                    for(auto it = LV.begin(); it != LV.end(); it++){
                        Covered_value_Set.insert(*it);
                    }

                    set<Value*> targetValueSet;
                    getClusterValues(aliased_v, targetValueSet, aliasCtx);
                    for(auto it = targetValueSet.begin(); it != targetValueSet.end(); it++){
                        Value* target_v = *it;
                        if(Covered_value_Set.count(target_v))
                            continue;

                        LV.push_back(target_v);
                    }

                    if(LV.size() > MAX_ANALYSIS_NUM){
                        aliasCtx->Is_Analyze_Success = false;
                        aliasCtx->failreason = exceed_max;
                        break;
                    }

                    continue;
                }

                GEPOperator *GEPO = dyn_cast<GEPOperator>(U);
                BitCastOperator *CastO = dyn_cast<BitCastOperator>(U);
                PtrToIntOperator *PTIO = dyn_cast<PtrToIntOperator>(U);
                
                if(GEPO || CastO || PTIO ){
                    for(User *u : U->users()){
                        LU.push_back(u);
                    }
                    continue;
                }

                GlobalVariable* Gv = dyn_cast<GlobalVariable>(U);
                if(Gv){
                    LV.push_back(U);
                    continue;
                }

                ConstantAggregate *CA = dyn_cast<ConstantAggregate>(U);
                if(CA){
                    for(User *u : U->users()){
                        LU.push_back(u);
                    }
                    continue;
                }
            }

            //Now the graph has been extened, update analysis targets
            set<Value*> Covered_value_Set;
            Covered_value_Set.clear();
            valueSetMerge(Covered_value_Set, Analyzed_Set);
            for(auto it = LV.begin(); it != LV.end(); it++){
                Covered_value_Set.insert(*it);
            }

            set<Value*> targetValueSet;
	        getClusterValues(aliased_v, targetValueSet, aliasCtx);
            for(auto it = targetValueSet.begin(); it != targetValueSet.end(); it++){
                Value* target_v = *it;
                if(Covered_value_Set.count(target_v))
                    continue;
                
                LV.push_back(target_v);
            }

            if(LV.size() > MAX_ANALYSIS_NUM){
                aliasCtx->Is_Analyze_Success = false;
                aliasCtx->failreason = exceed_max;
                break;
            }

            Analyzed_Set.insert(Global_GV);
        }
        return;
    }

    //The aliased value is a return value of a call
    CallInst *CAI = dyn_cast<CallInst>(aliased_v);
    if(CAI){
        
        if(CAI->isInlineAsm()){
            aliasCtx->Is_Analyze_Success = false;
            aliasCtx->failreason = inline_asm;
            return;
        }

        //Ignore the return value of alloc functions
        StringRef FName = getCalledFuncName(CAI);
        if(Ctx->HeapAllocFuncs.count(FName.str()))
            return;

        //TODO: handle the llvm.strip.invariant.group
        if(FName.contains("llvm.strip.invariant.group")){
            //HandleMove(CAI->getArgOperand(0), CAI, aliasCtx);
            aliasCtx->Is_Analyze_Success = false;
            aliasCtx->failreason = strip_invariant_group;
            return;
        }

        if(!Ctx->Callees.count(CAI))
            return;
        
        unsigned argnum = CAI->arg_size();

        for(Function *f : Ctx->Callees[CAI]){
            HandleReturn(f, CAI, aliasCtx);

            analyzeFunction(f, aliasCtx, Ctx);

            //f's caller is CAI, so we do not need to analysis its args in the future
            vector<Value *>f_arg_vec;
            f_arg_vec.clear();
            for(auto it = f->arg_begin(); it != f->arg_end(); it++){
                f_arg_vec.push_back(it);
                Analyzed_Set.insert(it);
            }

            auto f_arg_size = f->arg_size();
            unsigned min_num = getMin(f_arg_size, argnum);
            for(unsigned j = 0; j < min_num; j++){
                Value* CAI_arg = CAI->getArgOperand(j);
                HandleMove(CAI_arg, f_arg_vec[j], aliasCtx);
            }

            //Now the graph has been extened, update analysis targets
            set<Value*> Covered_value_Set;
            Covered_value_Set.clear();
            valueSetMerge(Covered_value_Set, Analyzed_Set);
            for(auto it = LV.begin(); it != LV.end(); it++){
                Covered_value_Set.insert(*it);
            }

            set<Value*> targetValueSet;
	        getClusterValues(aliased_v, targetValueSet, aliasCtx);
            for(auto it = targetValueSet.begin(); it != targetValueSet.end(); it++){
                Value* target_v = *it;
                if(Covered_value_Set.count(target_v))
                    continue;
                
                LV.push_back(target_v);
            }

            if(LV.size() > MAX_ANALYSIS_NUM){
                aliasCtx->Is_Analyze_Success = false;
                aliasCtx->failreason = exceed_max;
                break;
            }
        }
        return;
    }

    //The aliased value is the argument of some caller functions
    Argument *arg = dyn_cast<Argument>(aliased_v);
    if(arg){
        Function* caller = arg->getParent();
        if(!caller){
            return;
        }
        
        auto caller_arg_size = caller->arg_size();
        if(!Ctx->Callers.count(caller))
            return;

        unsigned arg_index = getArgIndex(caller, arg);

        vector<Value *>caller_arg_vec;
        caller_arg_vec.clear();
        for(auto it = caller->arg_begin(); it != caller->arg_end(); it++){
            caller_arg_vec.push_back(it);
        }

        CallInstSet callset = Ctx->Callers[caller];

        for(auto it = callset.begin(); it != callset.end(); it++){
            CallInst* cai = *it;
            unsigned argnum = cai->arg_size();
            unsigned min_num = getMin(caller_arg_size, argnum);
            for(unsigned j = 0; j < min_num; j++){
            
                Value* cai_arg = cai->getArgOperand(j);
                HandleMove(cai_arg, caller_arg_vec[j], aliasCtx);
            }

            Function* cai_parent = cai->getFunction();
            analyzeFunction(cai_parent, aliasCtx, Ctx);

            //Now the graph has been extened, update analysis targets
            set<Value*> Covered_value_Set;
            Covered_value_Set.clear();
            valueSetMerge(Covered_value_Set, Analyzed_Set);
            for(auto it = LV.begin(); it != LV.end(); it++){
                Covered_value_Set.insert(*it);
            }

            set<Value*> targetValueSet;
	        getClusterValues(aliased_v, targetValueSet, aliasCtx);
            for(auto it = targetValueSet.begin(); it != targetValueSet.end(); it++){
                Value* target_v = *it;
                if(Covered_value_Set.count(target_v))
                    continue;
                
                LV.push_back(target_v);
            }

            if(LV.size() > MAX_ANALYSIS_NUM){
                aliasCtx->Is_Analyze_Success = false;
                aliasCtx->failreason = exceed_max;
                break;
            }
        }

        return;
    }

    ReturnInst *RI = dyn_cast<ReturnInst>(aliased_v);
    if(RI){

        Function* caller = RI->getFunction();
        if(!caller)
            return;
        
        Value* return_v = RI->getReturnValue();
        
        CallInstSet callset = Ctx->Callers[caller];
        for(auto it = callset.begin(); it != callset.end(); it++){
            CallInst* cai = *it;

            HandleMove(return_v, cai, aliasCtx);
            analyzeFunction(cai->getFunction(), aliasCtx, Ctx);

            //Now the graph has been extened, update analysis targets
            set<Value*> Covered_value_Set;
            Covered_value_Set.clear();
            valueSetMerge(Covered_value_Set, Analyzed_Set);
            for(auto it = LV.begin(); it != LV.end(); it++){
                Covered_value_Set.insert(*it);
            }

            set<Value*> targetValueSet;
	        getClusterValues(aliased_v, targetValueSet, aliasCtx);
            for(auto it = targetValueSet.begin(); it != targetValueSet.end(); it++){
                Value* target_v = *it;
                if(Covered_value_Set.count(target_v))
                    continue;
                
                LV.push_back(target_v);
            }

            if(LV.size() > MAX_ANALYSIS_NUM){
                aliasCtx->Is_Analyze_Success = false;
                aliasCtx->failreason = exceed_max;
                break;
            }
            
        }

    }

}

void analyzeGlobalInitializer(GlobalVariable* GV, list<Value *>&Future_analysis_list,
    AliasContext *aliasCtx){

    Constant *Ini = GV->getInitializer();
    HandleMove(Ini, GV, aliasCtx);
    
    list<User *>LU;
	LU.push_back(Ini);
	set<User *> PB;
	PB.clear();

	//We should consider global struct array
	while (!LU.empty()) {
		User *U = LU.front();
		LU.pop_front();

		if (PB.find(U) != PB.end()){
			continue;
		}
		PB.insert(U);

        for (auto oi = U->op_begin(), oe = U->op_end(); oi != oe; ++oi) {
			Value *O = *oi;
            HandleStore(O, U, aliasCtx);

            GlobalVariable* inner_GV = dyn_cast<GlobalVariable>(O);
            if(inner_GV){
                Future_analysis_list.push_back(inner_GV);
            }

            GEPOperator *GEPO = dyn_cast<GEPOperator>(O);
            BitCastOperator *CastO = dyn_cast<BitCastOperator>(O);
            PtrToIntOperator *PTIO = dyn_cast<PtrToIntOperator>(O);
            
            if(GEPO || CastO || PTIO ){
                HandleOperator(O, aliasCtx);
            }

            Type *OTy = O->getType();
            if(isCompositeType(OTy)){
                User *OU = dyn_cast<User>(O);
				LU.push_back(OU);
            }
        }
    }
}