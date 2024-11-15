#include "CallGraph.h"

DenseMap<size_t, set<CallGraphPass::CompositeType>> CallGraphPass::FuncTypesMap;

//Use this on failure of nextLayerBaseType
void CallGraphPass::checkTypeStoreFunc(Value *V, set<CompositeType> &CTSet){

	std::set<Value *> PV; //Global value set to avoid loop
	std::list<Value *> EV; //BFS record list

	PV.clear();
	EV.clear();
	EV.push_back(V);

	while (!EV.empty()) {

		Value *TV = EV.front(); //Current checking value
		EV.pop_front();

		if (PV.find(TV) != PV.end())
			continue;
		PV.insert(TV);

		LoadInst* LI = dyn_cast<LoadInst>(TV);
		if(LI){
			Value *LPO = LI->getPointerOperand();
            EV.push_back(LPO);

			//Get all stored values
            for(User *U : LPO->users()){
                StoreInst *STI = dyn_cast<StoreInst>(U);
                if(STI){
                    
                    Value* vop = STI->getValueOperand();
                    Value* pop = STI->getPointerOperand();

					//Store constant is not considered
					if(isConstant(vop))
						continue;

					//There must be a path from the store to the load
                    if(pop == LPO && checkBlockPairConnectivity(STI->getParent(), LI->getParent()))
                        EV.push_back(vop);
                }
            }
			continue;
		}

		UnaryInstruction *UI = dyn_cast<UnaryInstruction>(TV);
		if(UI){
			EV.push_back(UI->getOperand(0));
			continue;
		}

		CallInst *CAI = dyn_cast<CallInst>(TV);
		if(CAI){

			Function *CF = CAI->getCalledFunction();
			if (!CF) {
				continue;
			}

			size_t funchash = funcHash(CF,true);
			
			if(FuncTypesMap.count(funchash)){
				CTSet = FuncTypesMap[funchash];
				return;
			}
			
			continue;
		}

		GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(TV);
        if(GEP){
			Value *PO = GEP->getPointerOperand();
			Type *PTy = PO->getType();
			Type *Ty = PTy->getPointerElementType();
			Type *sTy = GEP->getSourceElementType();

			//Expect the PointerOperand is a struct
			if (isCompositeType(Ty)	&& GEP->hasAllConstantIndices()) {
				User::op_iterator ie = GEP->idx_end();
				ConstantInt *ConstI = dyn_cast<ConstantInt>((--ie)->get());
				int Idx = ConstI->getSExtValue();
				CTSet.insert(std::make_pair(Ty, Idx));
				return;
			}
			continue;
		}
	}
}