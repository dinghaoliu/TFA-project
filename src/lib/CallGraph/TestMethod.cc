//===-- TestMethod.cc - Old implementation of type analysis-----===//
// 
// This file contains the old implementation of two-layer type analysis.
//
//===-----------------------------------------------------------===//

#include "CallGraph.h"

// Find targets of indirect calls based on type analysis: as long as
// the number and type of parameters of a function matches with the
// ones of the callsite, we say the function is a possible target of
// this call.
void CallGraphPass::findCalleesByType(CallInst *CI, FuncSet &S) {

	if (CI->isInlineAsm())
		return;

	//CallSite CS(CI);
	for (Function *F : Ctx->AddressTakenFuncs) {

		// VarArg
		if (F->getFunctionType()->isVarArg()) {
			// Compare only known args in VarArg.
			//OP<<"target F: "<<F->getName()<<"\n";
		}
		// otherwise, the numbers of args should be equal.
		else if (F->arg_size() != CI->arg_size()) {
			continue;
		}

		if (F->isIntrinsic()) {
			continue;
		}

		// Type matching on args.
		bool Matched = true;
		auto AI = CI->arg_begin();
		for (Function::arg_iterator FI = F->arg_begin(), 
				FE = F->arg_end();
				FI != FE; ++FI, ++AI) {
			// Check type mis-matches.
			// Get defined type on callee side.
			Type *DefinedTy = FI->getType();
			// Get actual type on caller side.
			Type *ActualTy = (*AI)->getType();

			if (DefinedTy == ActualTy)
				continue;

			// FIXME: this is a tricky solution for disjoint
			// types in different modules. A more reliable
			// solution is required to evaluate the equality
			// of two types from two different modules.
			// Since each module has its own type table, same
			// types are duplicated in different modules. This
			// makes the equality evaluation of two types from
			// two modules very hard, which is actually done
			// at link time by the linker.
			while (DefinedTy->isPointerTy() && ActualTy->isPointerTy()) {
				DefinedTy = DefinedTy->getPointerElementType();
				ActualTy = ActualTy->getPointerElementType();
			}
			if (DefinedTy->isStructTy() && ActualTy->isStructTy() &&
				(DefinedTy->getStructName().equals(ActualTy->getStructName())))
				continue;
			if (DefinedTy->isIntegerTy() && ActualTy->isIntegerTy() &&
				DefinedTy->getIntegerBitWidth() == ActualTy->getIntegerBitWidth())
				continue;
			// TODO: more types to be supported.

			// Make the type analysis conservative: assume universal
			// pointers, i.e., "void *" and "char *", are equivalent to 
			// any pointer type and integer type.
			if (
				(DefinedTy == Int8PtrTy &&
					(ActualTy->isPointerTy() || ActualTy == IntPtrTy)) 
				||
				(ActualTy == Int8PtrTy &&
					(DefinedTy->isPointerTy() || DefinedTy == IntPtrTy))
				)
				continue;
			else {
				#if 0 // for debugging
				if (F->getName().compare("nfs_fs_mount") == 0) {
				OP << "DefinedTy @ " << DefinedTy << ": " << *DefinedTy << "\n";
				OP << "ActualTy @ " << ActualTy << ": " << *ActualTy << "\n";
				}
				#endif
						Matched = false;
						break;
			}
		}

		if (Matched)
			S.insert(F);
	}
}

//Current implementation will miss some load/store based layer store
//because of nextLayerBaseType does not consider store
bool CallGraphPass::typeConfineInStore(StoreInst *SI) {

	//store VO to PO
	Value *PO = SI->getPointerOperand();
	Value *VO = SI->getValueOperand();

	recheck:

	//if (isa<Constant>(VO)) this will introduce large FN
	if (isa<ConstantData>(VO))
		return false;

	//OP<<"\n \033[34m" << "in typeConfineInStore" <<"\033[0m" <<"\n";
	//OP<<"\nstore inst: "<<*SI<<"\n";

	// Case 1: The value operand is a function (a function is stored into sth)
	if (Function *F = dyn_cast<Function>(VO)) {
	
		if (F->isIntrinsic())
			return false;
		
		Type *STy;
		int Idx;

		FuncSet FSet;
		FSet.clear();

		if(F->isDeclaration()){
			getGlobalFuncs(F,FSet);
		}
		else{
			FSet.insert(F);
		}

		//set<Value*> VisitedSet;
		//list<CompositeType> TyList;

		if (nextLayerBaseType(PO, STy, Idx)) {

				for(auto it = FSet.begin(); it != FSet.end(); it++){
					
					F = *it;

					typeFuncsMap[typeIdxHash(STy, Idx)].insert(F);

					Ctx->sigFuncsMap[funcHash(F, false)].insert(F);
					//OP<<"func type hash: "<<funcHash(F, false)<<"\n";
					if(Ctx->Global_Arg_Cast_Func_Map.count(F)){
						set<size_t> hashset = Ctx->Global_Arg_Cast_Func_Map[F];
						for(auto i = hashset.begin(); i!= hashset.end(); i++){
							Ctx->sigFuncsMap[*i].insert(F);
						}
					}

					//Use the new type to store
					size_t typehash = typeHash(STy);
					size_t typeidhash = typeIdxHash(STy,Idx);
					hashTypeMap[typehash] = STy;
					hashIDTypeMap[typeidhash] = make_pair(STy,Idx);

					updateStructInfo(F, STy, Idx);
				}
			//}

			return true;
		}
		else {

			//A function is stored to an unknown one-layer value, we cannot track it.
			//So we mark this func as escape (record in Global_EmptyTy_Funcs).
			for(auto it = FSet.begin(); it != FSet.end(); it++){	
				F = *it;
				Ctx->sigFuncsMap[funcHash(F, false)].insert(F);
				Ctx->Global_EmptyTy_Funcs[funcHash(F, false)].insert(F);
			}

			//OP<<"----fail get nextLayerBaseType result\n";
			return false;
		}
	}

	//This case could catch more stored func
	if(BitCastOperator *Cast = dyn_cast<BitCastOperator>(VO)) {
		Value* op = Cast->getOperand(0);
		VO = op;
		goto recheck;
	}

	//This check is almost useless
	if(BitCastOperator *Cast = dyn_cast<BitCastOperator>(PO)) {
		Value* op = Cast->getOperand(0);
		PO = op;
		goto recheck;
	}

	// Cast 2: value-based store
	// A composite-type object (VO) is stored
	// This check is totally useless, more interesting cases are in reference-based store
	Type *VTy = VO->getType(); 
	
	// Case 3: reference (i.e., pointer)-based store
	// FIXME: Get the correct types
	PointerType *VOPointerTy = dyn_cast<PointerType>(VO->getType());
	if (!VOPointerTy){
		return false;
	}

	// Store something to a field of a struct
	// Note: for only type analysis, we do not need to consider sth is stored 
	// to a non-composite type object,
	// but we may need to consider it in data flow analysis
	Type *PO_STy;
	int PO_Idx;

	set<Value*> VisitedSet;
	list<CompositeType> TyList;

	if (nextLayerBaseType(PO, PO_STy, PO_Idx)) {

		//Union prelayer and auto-generated prelayer is not considered,
		//All funcs stored in them are degenerated to one-layer 
		if(PO_STy->isStructTy() && PO_STy->getStructName().size() != 0){
			auto TyName = PO_STy->getStructName().str();
			if(checkStringContainSubString(TyName, ".union")){
				return false;
			}

			if(checkStringContainSubString(TyName, ".anon")){
				return false;
			}
		}

		// Here we need to track if "something" is CompositeType value
		// Which means a non-composite value with layered type info is stored
		Type *VO_STy;
		int VO_Idx;
		if(nextLayerBaseType(VO, VO_STy, VO_Idx)){
			if(VO_STy == PO_STy && PO_Idx == VO_Idx){
				return true;
			}
			else {
				typeConfineMap[typeIdxHash(PO_STy, PO_Idx)].insert(typeIdxHash(VO_STy, VO_Idx));
				hashIDTypeMap[typeIdxHash(PO_STy,PO_Idx)] = make_pair(PO_STy,PO_Idx);
				hashIDTypeMap[typeIdxHash(VO_STy,VO_Idx)] = make_pair(VO_STy,VO_Idx);
				return true;
			}
		}
		else {

			// TODO: The type is escaping?
			escapeType(SI, PO_STy, PO_Idx);
			Ctx->num_escape_store++;

			if(PO_Idx == -1){
				//This case will never happen in current layer analysis
				escapedStoreMap[typeHash(PO_STy)].insert(SI);
			}
			else{
				escapedStoreMap[typeIdxHash(PO_STy, PO_Idx)].insert(SI);
			}

			return false;
		}
	}

	return false;
}