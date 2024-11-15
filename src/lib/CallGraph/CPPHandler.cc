#include "CallGraph.h"

StringRef getClassName(GlobalVariable* GV){
    StringRef className = "";
	for(User* u : GV->users()){
		if(GEPOperator *GEPO = dyn_cast<GEPOperator>(u)){
			for(User* u2 : GEPO->users()){
				if(BitCastOperator *CastO = dyn_cast<BitCastOperator>(u2)){
					for(User* u3 : u2->users()){
						if(StoreInst *STI = dyn_cast<StoreInst>(u3)){
							
							Value* vop = STI->getValueOperand();
                    		Value* pop = STI->getPointerOperand();
							if(vop != u2)
								continue;
							
							BitCastOperator *CastO = dyn_cast<BitCastOperator>(pop);

							if(CastO){
								Value *ToV = CastO, *FromV = CastO->getOperand(0);
								Type *ToTy = ToV->getType(), *FromTy = FromV->getType();

								if(FromTy->isPointerTy()){
									Type *ToeleType = FromTy->getPointerElementType();
									if(ToeleType->isStructTy()){
										className = ToeleType->getStructName();
									}
								}
							}

							GEPOperator *GEPO_outer = dyn_cast<GEPOperator>(pop);
							if(GEPO_outer){
								auto sourceTy = GEPO_outer->getSourceElementType();
								if(sourceTy->isStructTy()){
									className = sourceTy->getStructName();
								}
							}
						}
					}
				}
			}
		}
	}
    return className;
}

//Handle C++ virtual tables
//In most cases, VTable is a struct with one array element
//Currently we only support normal class
void CallGraphPass::CPPVirtualTableHandler(GlobalVariable* GV){
	
	if(!GV->getName().contains("_ZTV"))
		return;

	//TODO: handle this
	if (!GV->hasInitializer()){
		return;
	}

	Constant *Ini = GV->getInitializer();
	if (!isa<ConstantAggregate>(Ini)){
		return;
	}
	
	if(!Ini->getType()->isStructTy()){
		return;
	}

	unsigned num_operand = Ini->getNumOperands();
	if(num_operand != 1){
		return;
	}
	
	auto arr = Ini->getOperand(0);
	ConstantArray *CA = dyn_cast<ConstantArray>(arr);
	if(!CA){
		return;
	}

	//Get the class name
	string className = getClassName(GV).str();
	if(className == "")
		return;

	int i = 0;
	for (auto oi = CA->op_begin(), oe = CA->op_end(); oi != oe; ++oi) {
		Value *ele = *oi;
		if(i <= 1){
			i++;
			continue;
		}

		BitCastOperator *CastO = dyn_cast<BitCastOperator>(ele);
		if(!CastO)
			continue;
		
		Value *FromV = CastO->getOperand(0);
		GlobalVTableMap[className].push_back(FromV);

		i++;
	}
}

bool CallGraphPass::getCPPVirtualFunc(Value* V, int &Idx, string &struct_name){

    set<Value *>Visited;

	// Case 1: GetElementPtrInst / GEPOperator
	if(GEPOperator *GEP = dyn_cast<GEPOperator>(V)){
		Type *PTy = GEP->getPointerOperand()->getType();
		Type *Ty = PTy->getPointerElementType();

        if(!Ty->isPointerTy()){
            return false;
        }

        Type *innertTy = Ty->getPointerElementType();

		//Expect the PointerOperand is a struct
		if (innertTy->isFunctionTy() && GEP->hasAllConstantIndices()) {

			User::op_iterator ie = GEP->idx_end();
			ConstantInt *ConstI = dyn_cast<ConstantInt>((--ie)->get());
			Idx = ConstI->getSExtValue();
			if(Idx < 0)
				return false;
            
            unsigned indice_num = GEP->getNumIndices();
            if(indice_num != 1)
                return false;
            
            return getCPPVirtualFunc(GEP->getPointerOperand(), Idx, struct_name);
		}
		else
			return false;
	}

	// Case 2: LoadInst
	// Maybe we should also consider the store inst here
	else if (LoadInst *LI = dyn_cast<LoadInst>(V)) {
		return getCPPVirtualFunc(LI->getOperand(0), Idx, struct_name);
	}

    //Find the special bitcast inst
    else if (BitCastInst *BCI = dyn_cast<BitCastInst>(V)){
        
        Type * srcty = BCI->getSrcTy();
		Type * destty = BCI->getDestTy();

        if(srcty->isPointerTy() && destty->isPointerTy()){
            Type *srctoTy = srcty->getPointerElementType();
            Type *desttoTy = destty->getPointerElementType();

            if(srctoTy->isStructTy()){
                string tyname = srctoTy->getStructName().str();
                Type *desttotoTy = desttoTy->getPointerElementType();
                if(desttotoTy->isPointerTy()){

                    Type *desttototoTy = desttotoTy->getPointerElementType();
                    if(desttototoTy->isFunctionTy()){
                        struct_name = tyname;
                        if(Idx >=0)
                            return true;
                        else 
                            return false;
                    }
                }
            }
        }
        return getCPPVirtualFunc(BCI->getOperand(0), Idx, struct_name);
    }


#if 1
	// Other instructions such as CastInst
	// FIXME: may introduce false positives
	//UnaryInstruction includes castinst, load, etc, resolve this in the last step
	else if (UnaryInstruction *UI = dyn_cast<UnaryInstruction>(V)) {
		return getCPPVirtualFunc(UI->getOperand(0), Idx, struct_name);
	}
#endif
	
	else {
		//OP<<"unknown inst: "<<*V<<"\n";
		return false;
	}

    return false;
}

bool endsWith(const string& str, const string& suffix) {
    if (str.length() < suffix.length()) {
        return false;
    }

    return str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
}

//Resolve function types with variable parameters
void CallGraphPass::resolveVariableParameters(CallInst *CI, FuncSet &FS){

	Type* funcTy = CI->getFunctionType();
	string funcTy_str = getTypeStr(funcTy);

	if(endsWith(funcTy_str, ", ...)")){

		//Erase ', ...)'
		funcTy_str.erase(funcTy_str.length() - 6);

		for(auto i = Ctx->sigFuncsMap.begin(); i != Ctx->sigFuncsMap.end(); i++){
			auto fs = i->second;
			if(fs.empty())
				continue;
			Function* f = *fs.begin();
			Type* fty = f->getFunctionType();
			string fty_str = getTypeStr(fty);
			if(checkStringContainSubString(fty_str, funcTy_str)){
				for(auto ele : fs){
					Ctx->sigFuncsMap[callHash(CI)].insert(ele);
				}
			}
		}
	}
	FS = Ctx->sigFuncsMap[callHash(CI)];
}