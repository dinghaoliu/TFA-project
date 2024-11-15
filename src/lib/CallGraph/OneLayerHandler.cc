#include "CallGraph.h"

//Further trace if one layer icall has two layer info
void CallGraphPass::oneLayerHandler_callgraph(){

    size_t success_num = 0;
    for(auto it = Ctx->Global_MLTA_Reualt_Map.begin(); it != Ctx->Global_MLTA_Reualt_Map.end(); it++){
		TypeAnalyzeResult type_result = it->second;
		if(type_result == OneLayer){
			CallInst* CI = it->first;
            FuncSet FS;
			if(oneLayerChecker(CI, FS)){
                success_num++;
            }
		}
	}

    OP<<"success one layer num: "<<success_num<<"\n";

    //Update analysis results
	Ctx->icallTargets = 0;
	for(auto i = Ctx->ICallees.begin(); i!= Ctx->ICallees.end(); i++){
		FuncSet fset = i->second;
		Ctx->icallTargets+=fset.size();
	}
}

bool CallGraphPass::oneLayerChecker(CallInst* CI, FuncSet &FS){

	if(!CI)
		return false;

    Module* M = CI->getFunction()->getParent();
    DL = &(M->getDataLayout());
    
    Type *LayerTy = NULL;
	int FieldIdx = -1;

    set<TypeAnalyzeResult> TAResultSet;
    FuncSet FS1;
	getOneLayerResult(CI, FS1);
	
	//Check the source of VO
    std::list<Value *> EV;
    std::set<Value *> PV;
    EV.clear();
    PV.clear();
    EV.push_back(CI->getCalledOperand());

    while (!EV.empty()) {
        Value *TV = EV.front();
		EV.pop_front();
            
        if (PV.find(TV) != PV.end())
			continue;
        PV.insert(TV);
        

		if(ConstantData *Ct = dyn_cast<ConstantData>(TV))
            continue;

        if(GEPOperator *GEP = dyn_cast<GEPOperator>(TV)){

            list<CompositeType> TyList;
            if (getGEPLayerTypes(GEP, TyList)){
                for(auto CT : TyList){

                    LayerTy = CT.first;
                    FieldIdx = CT.second;
                    int LayerNo = 1;
	                int escapeTag = 0;
                    
                    FuncSet FST;
                    
                    findCalleesWithTwoLayerTA(CI, FS1, LayerTy, FieldIdx, FST, LayerNo, escapeTag);
                    if(!FST.empty()){

                        TAResultSet.insert(TwoLayer);
                        FS.insert(FST.begin(), FST.end());
                    }
                    else{
                        //not escape but still empty, regard this as no target rather than 
                        //use the result of a lower layer type analysis
 
                        if(escapeTag==0){
                            TAResultSet.insert(NoTwoLayerInfo);
                        }
                        else{
                            //Tupe escape, result is one layer
                            TAResultSet.insert(TypeEscape);
                        }
                    }
                }
                
                continue;
            }
            else{
                return false;
            }
        }

        if (Function *F = getBaseFunction(TV->stripPointerCasts())) {

            if (F->isIntrinsic())
			    continue;

            FuncSet FSet;
            FSet.clear();

            if(F->isDeclaration()){
                getGlobalFuncs(F,FSet);
            }
            else{
                FSet.insert(F);
            }

            if(FSet.empty())
                return false;

            for(auto it = FSet.begin(); it != FSet.end(); it++){
				F = *it;
                FS.insert(F);
			}

            TAResultSet.insert(OneLayer);

            continue;
        }

        llvm::Argument *Arg = dyn_cast<llvm::Argument>(TV);
        if(Arg){
            //OP<<"is arg\n";
            Function* caller = Arg->getParent();
            unsigned arg_index = Arg->getArgNo();
            
            CallInstSet callset = Ctx->Callers[caller];

            for(auto it = callset.begin(); it != callset.end(); it++){
                CallInst* cai = *it;

                unsigned cai_arg_num = cai->arg_size();
                if(cai_arg_num <= arg_index){
                    //OP<<"caller arg number is invalid\n";
                    return false;
                }
                
                Value* cai_arg = cai->getArgOperand(arg_index);
                EV.push_back(cai_arg);   
            }

            continue;
        }

        CallInst *CAI = dyn_cast<CallInst>(TV);
        if(CAI){
            StringRef FName = getCalledFuncName(CAI);
            if(Ctx->HeapAllocFuncs.count(FName.str()))
                continue;

            if(!Ctx->Callees.count(CAI)){
                //OP<<"call inst is not appear in Callees\n";
                return false;
            }

            if(Ctx->Callees[CAI].empty()){
                //OP<<"empty callee target\n";
                return false;
            }
            
            for(Function *f : Ctx->Callees[CAI]){
                for (inst_iterator i = inst_begin(f), ei = inst_end(f); i != ei; ++i) {
                    ReturnInst *rInst = dyn_cast<ReturnInst>(&*i);
                    if(rInst){
                        EV.push_back(rInst);
                    }
                }
            }

            continue;
        }

        LoadInst* LI = dyn_cast<LoadInst>(TV);
		if(LI){
			Value *LPO = LI->getPointerOperand();
            EV.push_back(LPO);

            //If TV comes from global, we will check it later
            auto globalvar = dyn_cast<GlobalVariable>(LPO);
            if(globalvar)
                return false;

			//Get all stored values
            for(User *U : LPO->users()){
                StoreInst *STI = dyn_cast<StoreInst>(U);
                if(STI){
                    
                    Value* vop = STI->getValueOperand(); // store vop to pop
                    Value* pop = STI->getPointerOperand();
                    EV.push_back(vop);
                }
            }
			continue;
		}

		PHINode *PHI = dyn_cast<PHINode>(TV);
        if(PHI){
            for (unsigned i = 0, e = PHI->getNumIncomingValues(); i != e; ++i){
                Value *IV = PHI->getIncomingValue(i);
                if(ConstantData *Ct = dyn_cast<ConstantData>(IV))
                    continue;
                EV.push_back(IV);
            }
            continue;
        }

        BitCastOperator *BCI = dyn_cast<BitCastOperator>(TV);
        if(BCI){
            
            //Check if TV comes from union
            Type *ToTy = BCI->getDestTy(), *FromTy = BCI->getSrcTy();
            if(FromTy->isPointerTy()){
                FromTy = FromTy->getPointerElementType();
            }
            if(FromTy->isStructTy()){
                if(FromTy->getStructName().contains(".union")){
                    return false;
                }
                if(FromTy->getStructName().contains(".anon")){
                    return false;
                }
            }

            EV.push_back(BCI->getOperand(0));
            continue;
        }

        IntToPtrInst *ITPI = dyn_cast<IntToPtrInst>(TV);
        if(ITPI){
            EV.push_back(ITPI->getOperand(0));
            continue;
        }

        PtrToIntInst *PTII = dyn_cast<PtrToIntInst>(TV);
        if(PTII){
            EV.push_back(PTII->getOperand(0));
            continue;
        }

        ZExtInst *ZEXI = dyn_cast<ZExtInst>(TV);
        if(ZEXI){
            EV.push_back(ZEXI->getOperand(0));
            continue;
        }

        SExtInst *SEXI = dyn_cast<SExtInst>(TV);
        if(SEXI){
            EV.push_back(SEXI->getOperand(0));
            continue;
        }

        TruncInst *TRUI = dyn_cast<TruncInst>(TV);
        if(TRUI){
            EV.push_back(TRUI->getOperand(0));
            continue;
        }

        SelectInst *SLI = dyn_cast<SelectInst>(TV);
        if(SLI){
            auto TrueV = SLI->getTrueValue();
            auto FalseV = SLI->getFalseValue();
            EV.push_back(TrueV);
            EV.push_back(FalseV);
            continue;
        }

        AllocaInst* ALI = dyn_cast<AllocaInst>(TV);
        if(ALI){
            continue;
        }

		ReturnInst *RI = dyn_cast<ReturnInst>(TV);
        if(RI){
            if(RI->getReturnValue() == NULL)
                continue;
            EV.push_back(RI->getReturnValue());
            continue;
        }

        // Many add inst comes from the return value of ioremap_prot()
        // For such case, we currently have no solution

        //Unsupported inst:
        //OP<<"Unsupported inst: "<<*TV<<"\n";
        return false;
	}

    //Resolve TAResult
    if(TAResultSet.size() == 1){
        TypeAnalyzeResult TAResult = *(TAResultSet.begin());
        Ctx->Global_MLTA_Reualt_Map[CI] = TAResult;
        if(TAResult == TwoLayer || TAResult == OneLayer){
        }
        else if(TAResult == NoTwoLayerInfo){
            FS.clear();
        }
        else{
            return false;
        }
    }
    else if(TAResultSet.count(TypeEscape)){
        Ctx->Global_MLTA_Reualt_Map[CI] = TypeEscape;
        return false;
    }
    else{
        Ctx->Global_MLTA_Reualt_Map[CI] = MixedLayer;
    }

    Ctx->Callees[CI] = FS;
    Ctx->ICallees[CI] = FS;

	return true;
}

