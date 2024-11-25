#include "CallGraph.h"

//Resolve type escape
void CallGraphPass::escapeHandler(){

    OP<<"\nstart escapeHandler\n";

    static set<size_t> succeed_type_set;
    
	size_t hash_num = escapedTypesInTypeAnalysisSet.size() - succeed_type_set.size();
	size_t SI_num = 0;
	size_t succeed_inst_num = 0;
	size_t succeed_type_num = 0;
	
	for(size_t hash : escapedTypesInTypeAnalysisSet){
		bool all_resolved_tag = true;
        if(succeed_type_set.count(hash))
            continue;

		for(StoreInst* SI : escapedStoreMap[hash]){
            
			SI_num++;
			if(escapeChecker(SI, hash)){
			    Function* F = SI->getFunction();
				succeed_inst_num++;
				continue;
			}

			all_resolved_tag = false;
			Function* F = SI->getFunction();

			Type *escapeTy;
			int escapeIdx = -1;
			if(hashIDTypeMap.count(hash)){
				escapeTy = hashIDTypeMap[hash].first;
				escapeIdx = hashIDTypeMap[hash].second;
			}
			else{
				escapeTy = hashTypeMap[hash];
			}

            break;
		}

		if(all_resolved_tag){
			succeed_type_num++;
			if(typeEscapeSet.count(hash)){
                typeEscapeSet.erase(hash);
			}
            succeed_type_set.insert(hash);

            if(hashIDTypeMap.count(hash)){
                Type *escapeTy;
			    int escapeIdx = -1;

				escapeTy = hashIDTypeMap[hash].first;
				escapeIdx = hashIDTypeMap[hash].second;
			}
		}
	}

	OP<<"\nrelated types: "<<hash_num<<"\n";
	OP<<"succeed_type_num: "<<succeed_type_num<<"\n";
	OP<<"related SI_num: "<<SI_num<<"\n";
	OP<<"succeed_inst_num: "<<succeed_inst_num<<"\n";

	for(auto it = Ctx->Global_MLTA_Reualt_Map.begin(); it != Ctx->Global_MLTA_Reualt_Map.end(); it++){
		TypeAnalyzeResult type_result = it->second;

        if(type_result == TypeEscape){
			CallInst* CI = it->first;

            //Alias analysis has succeed for this call
            if(Ctx->Global_Alias_Results_Map.count(CI))
                if(Ctx->Global_Alias_Results_Map[CI] == 1)
                    continue;

			FuncSet FS;
			FS.clear();

			findCalleesWithMLTA(CI, FS);
			Ctx->Callees[CI] = FS;
			Ctx->ICallees[CI] = FS;
		}
	}

	//Update analysis results
	Ctx->icallTargets = 0;
	for(auto i = Ctx->ICallees.begin(); i!= Ctx->ICallees.end(); i++){
		FuncSet fset = i->second;
		Ctx->icallTargets+=fset.size();
	}
}

bool CallGraphPass::escapeChecker(StoreInst* SI, size_t escapeHash){
    
    if(!SI)
        return false;
    
    Function* SI_caller = SI->getFunction();

    Module* M = SI_caller->getParent();
    DL = &(M->getDataLayout());

    Type *escapeTy = hashIDTypeMap[escapeHash].first;
    int escapeIdx = hashIDTypeMap[escapeHash].second;

    Type *LayerTy = NULL;
	int FieldIdx = -1;

    //store VO to PO
	Value *PO = SI->getPointerOperand();
	Value *VO = SI->getValueOperand();

    //Check the source of VO
    std::list<Value *> EV; //BFS record list
    std::set<Value *> PV; //Global value set to avoid loop
    EV.clear();
    PV.clear();
    EV.push_back(VO);

    while (!EV.empty()) {
        Value *TV = EV.front(); //Current checking value
		EV.pop_front();
            
        if (PV.find(TV) != PV.end()) //Avoid loop
			continue;
        PV.insert(TV);

        if(ConstantData *Ct = dyn_cast<ConstantData>(TV)){
            continue;
        }

        if(GEPOperator *GEP = dyn_cast<GEPOperator>(TV)){

            list<CompositeType> TyList;
            if (getGEPLayerTypes(GEP, TyList)){
                for(auto CT : TyList){

                    string Escape_Type_name = "";
                    if(escapeTy->isStructTy()){
                        StructType* escapeSTy = dyn_cast<StructType>(escapeTy);
                        if(escapeSTy->isLiteral()){
                            Escape_Type_name = Ctx->Global_Literal_Struct_Map[typeHash(escapeTy)];
                        }
                        else{
                            StringRef Ty_name = escapeTy->getStructName();
                            Escape_Type_name = parseIdentifiedStructName(Ty_name);
                        }
                    }

                    string CT_Type_name = "";
                    if(CT.first->isStructTy()){
                        StructType* CTSTy = dyn_cast<StructType>(CT.first);
                        if(CTSTy->isLiteral()){
                            CT_Type_name = Ctx->Global_Literal_Struct_Map[typeHash(CTSTy)];
                        }
                        else{
                            StringRef Ty_name = CTSTy->getStructName();
                            CT_Type_name = parseIdentifiedStructName(Ty_name);
                        }
                    }

                    typeConfineMap[typeNameIdxHash(Escape_Type_name, escapeIdx)].insert(typeNameIdxHash(CT_Type_name, CT.second));
                    hashIDTypeMap[typeNameIdxHash(Escape_Type_name, escapeIdx)] = make_pair(escapeTy, escapeIdx);
                    hashIDTypeMap[typeNameIdxHash(CT_Type_name, CT.second)] = make_pair(CT.first, CT.second);
                }
                continue;
            }
            else{
                //OP<<"invalid GEP\n";
                //OP<<"GEP: "<<*GEP<<"\n";
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

            for(auto it = FSet.begin(); it != FSet.end(); it++){
				
				F = *it;

				typeFuncsMap[typeIdxHash(escapeTy, escapeIdx)].insert(F);
				Ctx->sigFuncsMap[funcHash(F, false)].insert(F);

				if(Ctx->Global_Arg_Cast_Func_Map.count(F)){
					set<size_t> hashset = Ctx->Global_Arg_Cast_Func_Map[F];
					for(auto i = hashset.begin(); i!= hashset.end(); i++){
						Ctx->sigFuncsMap[*i].insert(F);
					}
				}

				//Use the new type to store
				size_t typehash = typeHash(escapeTy);
				size_t typeidhash = typeIdxHash(escapeTy, escapeIdx);
				hashTypeMap[typehash] = escapeTy;
				hashIDTypeMap[typeidhash] = make_pair(escapeTy,escapeIdx);
				updateStructInfo(F, escapeTy, escapeIdx);
			}

            continue;
        }

        llvm::Argument *Arg = dyn_cast<llvm::Argument>(TV);
        if(Arg){

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
            
            //A special case handler
            //We do not need to consider the refcount influence here
            if(FName == "__symbol_get"){
                string symbol_str = symbol_get_hander(CAI);
                if(symbol_str.size() == 0)
                    return false;

                if(Ctx->GlobalFuncs.count(symbol_str)){
                    for(size_t hash : Ctx->GlobalFuncs[symbol_str]){
                        EV.push_back(Ctx->Global_Unique_Func_Map[hash]);
                    }
                }else{
                    continue;
                }
            }

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
                continue;

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


    return true;
}