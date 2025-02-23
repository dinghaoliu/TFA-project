#include "CallGraph.h"

using namespace llvm;

//#define UNROLL_LOOP_ONCE 1

#define MLTA_FOR_INDIRECT_CALL 1

//#define ONE_LAYER_MLTA 1

//#define TEST_ONE_FUNC ""
//#define TEST_ONE_INIT_GLOBAL ""
//#define TEST_ONE_INIT_STORE ""
//#define ENABLE_CAST_ESCAPE
#define ENABLE_BIDIRECTIONAL_TYPE_CASTING

#define IGNORE_LINUX_STATIC_CALL 1

//key: the parent struct's type and stored index, value: stored functions
//(type-function map in paper)
DenseMap<size_t, FuncSet> CallGraphPass::typeFuncsMap;

//key: parrent struct's type and stored index, value: current types stored
unordered_map<size_t, set<size_t>> CallGraphPass::typeConfineMap; 

//record type cast info
//key: to type, value: from types
unordered_map<size_t, set<size_t>> CallGraphPass::typeTransitMap;
map<string, set<string>> CallGraphPass::typeNameTransitMap;
unordered_map<size_t, set<size_t>> CallGraphPass::funcTypeCastMap;
set<size_t> CallGraphPass::funcTypeCastToVoidSet;
set<size_t> CallGraphPass::funcTypeCastFromVoidSet;
FuncSet CallGraphPass::escapeFuncSet;  

map<string, set<string>> CallGraphPass::typeStrCastMap;
map<int, map<size_t, FunctionType*>> CallGraphPass::funcTypeMap;

set<size_t> CallGraphPass::typeEscapeSet;
set<string> CallGraphPass::castEscapeSet;
const DataLayout *CurrentLayout;

map<string, set<unsigned long long>> CallGraphPass::globalFuncNameMap;
map<string, set<Function*>> CallGraphPass::globalFuncInitMap;
set<string> CallGraphPass::globalFuncEscapeSet;
DenseMap<size_t, FuncSet> CallGraphPass::argStoreFuncSet;
unordered_map<size_t, set<size_t>> CallGraphPass::argStoreFuncTransitMap;

//New storage structure
map<size_t, Type*> CallGraphPass::hashTypeMap;
map<size_t, pair<Type*, int>> CallGraphPass::hashIDTypeMap;
unordered_map<unsigned, set<size_t>> CallGraphPass::subMemberNumTypeMap; 

set<size_t> CallGraphPass::escapedTypesInTypeAnalysisSet;
map<size_t, set<StoreInst*>> CallGraphPass::escapedStoreMap;

map<Value*, map<Function*, set<size_t>>> CallGraphPass::Func_Init_Map;
map<Value*, Type*> CallGraphPass::TypeHandlerMap;
map<Function*, set<CallInst*>> CallGraphPass::LLVMDebugCallMap;

void CallGraphPass::typeConfineInStore_new(StoreInst *SI) {

	Value *PO = SI->getPointerOperand();
	Value *VO = SI->getValueOperand();

	if (isa<ConstantData>(VO))
		return;

	//A structure with 2-layer info is stored to PO
	set<Value*> VisitedSet;
	list<CompositeType> TyList;
	bool next_result = nextLayerBaseType_new(VO, TyList, VisitedSet, Recall_Mode);
	if (next_result) {
		for (CompositeType CT : TyList) {
			propagateType(PO, CT.first, CT.second, SI);
		}
		return;
	}
	else if (TyList.size() > 0) {

		for (CompositeType CT : TyList) {
			propagateType(PO, CT.first, CT.second, SI);
		}
		VisitedSet.clear();
		TyList.clear();

		nextLayerBaseType_new(PO, TyList, VisitedSet, Recall_Mode);
		for (CompositeType CT : TyList) {
			escapeType(SI, CT.first, CT.second);
			Ctx->num_escape_store++;
		}
		return;
	}

	//A structure/array/vector without 2-layer info is stored to PO
	//This is a common operation, ignore this case
	set<Value *>Visited;
	Type *VO_BTy = getBaseType(VO, Visited);
	if (VO_BTy) {

		//A special case: store a func pointer vec to a structure field
		if(VO_BTy->isVectorTy() && VO_BTy->getScalarType()->isPointerTy()){
			VectorType* FVT = dyn_cast<VectorType>(VO_BTy);
			if(!FVT){
				return;
			}

			ConstantVector* CV = dyn_cast<ConstantVector>(VO);
			if(CV){

				unsigned elenum = CV->getNumOperands();

				for (unsigned i = 0; i < elenum; i++) {

					Value* O = CV->getOperand(i);
					Function *F = getBaseFunction(O->stripPointerCasts());
					if(F){
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

						set<Value*> VisitedSet;
						list<CompositeType> TyList;
						if (nextLayerBaseType_new(PO, TyList, VisitedSet, Precise_Mode)) {

							for(CompositeType CT : TyList){

								Type* STy = CT.first;
								int Idx = CT.second + i;
								size_t typehash = typeHash(STy);
								size_t typeidhash = typeIdxHash(STy,Idx);
								hashTypeMap[typehash] = STy;
								hashIDTypeMap[typeidhash] = make_pair(STy,Idx);

								for(auto it = FSet.begin(); it != FSet.end(); it++){					
									F = *it;

									typeFuncsMap[typeIdxHash(STy, Idx)].insert(F);
									Ctx->sigFuncsMap[funcHash(F, false)].insert(F);

									if(Ctx->Global_Arg_Cast_Func_Map.count(F)){
										set<size_t> hashset = Ctx->Global_Arg_Cast_Func_Map[F];
										for(auto i = hashset.begin(); i!= hashset.end(); i++){
											Ctx->sigFuncsMap[*i].insert(F);
										}
									}
									
									//If STy is an invalid struct (e.g., union), F will be marked escape
									updateStructInfo(F, STy, Idx, SI);
								}
							}
						}
						else {

							//A function is stored to an unknown one-layer value, 
							//we cannot track this case through type analysis.
							//So we mark this func as escape (record in Global_EmptyTy_Funcs),
							//and analyze them in data-flow analysis
							for(auto it = FSet.begin(); it != FSet.end(); it++){	
								auto F = *it;
								Ctx->sigFuncsMap[funcHash(F, false)].insert(F);
								Ctx->Global_EmptyTy_Funcs[funcHash(F, false)].insert(F);
							}
						}
					}
				}
			}
		}
		return;
	}

	//A function is stored into sth
	Function *F = getBaseFunction(VO->stripPointerCasts());
	if(F){

		if (F->isIntrinsic())
			return;

		FuncSet FSet;
		FSet.clear();

		if(F->isDeclaration()){
			getGlobalFuncs(F,FSet);
		}
		else{
			FSet.insert(F);
		}

		set<Value*> VisitedSet;
		list<CompositeType> TyList;
		if (nextLayerBaseType_new(PO, TyList, VisitedSet, Precise_Mode)) {

			for(CompositeType CT : TyList){

				Type* STy = CT.first;
				int Idx = CT.second;

				size_t typehash = typeHash(STy);
				size_t typeidhash = typeIdxHash(STy,Idx);
				hashTypeMap[typehash] = STy;
				hashIDTypeMap[typeidhash] = make_pair(STy,Idx);

				for(auto it = FSet.begin(); it != FSet.end(); it++){					
					F = *it;

					typeFuncsMap[typeIdxHash(STy, Idx)].insert(F);
					Ctx->sigFuncsMap[funcHash(F, false)].insert(F);

					if(Ctx->Global_Arg_Cast_Func_Map.count(F)){
						set<size_t> hashset = Ctx->Global_Arg_Cast_Func_Map[F];
						for(auto i = hashset.begin(); i!= hashset.end(); i++){
							Ctx->sigFuncsMap[*i].insert(F);
						}
					}
					
					//If STy is an invalid struct (e.g., union), F will be marked escape
					updateStructInfo(F, STy, Idx, SI);
				}
			}
		}
		else {

			//A function is stored to an unknown one-layer value, 
			//we cannot track this case through type analysis.
			//So we mark this func as escape (record in Global_EmptyTy_Funcs),
			//and analyze them in data-flow analysis
			for(auto it = FSet.begin(); it != FSet.end(); it++){	
				F = *it;
				Ctx->sigFuncsMap[funcHash(F, false)].insert(F);
				Ctx->Global_EmptyTy_Funcs[funcHash(F, false)].insert(F);
			}
		}

		return;
	}

	if(!VO->getType()->isPointerTy())
		return;

	//A general-pointer without a known source is stored to PO
	//TODO: further check whether VO is unknown
	if(trackFuncPointer(VO, PO, SI)){
		return;
	}

	TyList.clear();
	VisitedSet.clear();
	nextLayerBaseType_new(PO, TyList, VisitedSet, Recall_Mode);
	for (CompositeType CT : TyList) {
		escapeType(SI, CT.first, CT.second);
		Ctx->num_escape_store++;
	}

	return;
}

/*Sometimes there is no direct cast from one type to another, like the example in tensorflow:
from type: i32 (%struct.TfLiteContext*, %struct.TfLiteNode*, i32, %struct.TfLiteTensor**)*
to tyoe: i32 (%struct.TfLiteContext.5225*, %struct.TfLiteNode.5220*, i32, %struct.TfLiteTensor.5219**)*
in this case, we may need to regard a cast happens between %struct.TfLiteContext and %struct.TfLiteContext.5225
*/
void CallGraphPass::handleIndirectCast(Type *FromTy, Type *ToTy){
	
	//Handle the cast inside function pointers
	if(ToTy->isPointerTy() && FromTy->isPointerTy()){
		Type *ToeleType = ToTy->getPointerElementType();
		Type *FromeleType = FromTy->getPointerElementType();
		
		if(ToeleType->isFunctionTy() && FromeleType->isFunctionTy()){

			/*string to_ty_str = getTypeStr(ToeleType);
			string from_ty_str = getTypeStr(FromeleType);
			if(checkStringContainSubString(to_ty_str, "TfLiteContext.1233")){
				OP<<"from_ty_str: "<<from_ty_str<<"\n";
				OP<<"to_ty_str: "<<to_ty_str<<"\n";
				
			}*/

			//OP<<"\nToeleType: "<<*ToeleType<<"\n";
			//OP<<"FromeleType: "<<*FromeleType<<"\n";
			FunctionType* FromFuncTy = dyn_cast<FunctionType>(FromeleType);
			FunctionType* ToFuncTy = dyn_cast<FunctionType>(ToeleType);

			//First handle return type
			Type* FromReturnTy = FromFuncTy->getReturnType();
			Type* ToReturnTy = ToFuncTy->getReturnType();
			if(FromReturnTy != ToReturnTy){
				if(FromReturnTy->isStructTy() && ToReturnTy->isStructTy()){
					transitType(ToReturnTy, FromReturnTy);
				}
			}

			//Next handle arg type
			if(FromFuncTy->getNumParams() != ToFuncTy->getNumParams()){
				//OP<<"invalid arg number!\n";
				return;
			}

			for(int i = 0; i < FromFuncTy->getNumParams(); i++){
				Type* FromArgTy = FromFuncTy->getParamType(i);
				Type* ToArgTy = ToFuncTy->getParamType(i);
				//OP<<"FromArgTy: "<<*FromArgTy<<"\n";
				//OP<<"ToArgTy: "<<*ToArgTy<<"\n";
				if(ToArgTy->isPointerTy() && FromArgTy->isPointerTy()){
					//OP<<"both ptr\n";
					ToArgTy = ToArgTy->getPointerElementType();
					FromArgTy = FromArgTy->getPointerElementType();
				}

				if(FromArgTy != ToArgTy){
					if(FromArgTy->isStructTy() && ToArgTy->isStructTy()){
						transitType(ToArgTy, FromArgTy);
						/*string to_ty_str = getTypeStr(ToArgTy);
						string from_ty_str = getTypeStr(FromArgTy);
						if(checkStringContainSubString(to_ty_str, "TfLiteContext.1233")){
							//if(FromTy->isStructTy())
							OP<<"\nfrom_ty_str: "<<from_ty_str<<"\n";
							OP<<"to_ty_str: "<<to_ty_str<<"\n";
						}*/
					}
				}
			}
		}
	}
}

bool CallGraphPass::typeConfineInCast(CastInst *CastI) {

	// If a function address ever cast to another type and is stored
	// to a composite type, the escaping analysis will capture the
	// composite type and discard it
	Value *ToV = CastI, *FromV = CastI->getOperand(0);
	Type *ToTy = ToV->getType(), *FromTy = FromV->getType();

	return typeConfineInCast(FromTy, ToTy);
}

bool CallGraphPass::typeConfineInCast(Type *FromTy, Type *ToTy){
	
	if (isCompositeType(FromTy)) {
		transitType(ToTy, FromTy);
		return true;
	}

	if (!FromTy->isPointerTy() || !ToTy->isPointerTy()){
		//OP<<"unsupported case type\n";
		return false;
	}

	typeStrCastMap[getTypeStr(ToTy)].insert(getTypeStr(FromTy));

	Type *EToTy = ToTy->getPointerElementType();
	Type *EFromTy = FromTy->getPointerElementType();
	if (isCompositeType(EToTy) && isCompositeType(EFromTy)) {

		transitType(EToTy, EFromTy);
#ifdef ENABLE_CAST_ESCAPE
		if(EToTy->isStructTy() && EFromTy->isStructTy()){
			StructType* EToSTy = dyn_cast<StructType>(EToTy);
			StructType* EFromSTy = dyn_cast<StructType>(EFromTy);
			if(!EToSTy->isLiteral() && !EFromSTy->isLiteral()){
				StringRef To_Ty_name = EToTy->getStructName();
				StringRef From_Ty_name = EFromTy->getStructName();
				string parsed_To_Ty_name = parseIdentifiedStructName(To_Ty_name);
				string parsed_From_Ty_name = parseIdentifiedStructName(From_Ty_name);
				typeNameTransitMap[parsed_To_Ty_name].insert(parsed_From_Ty_name);
			}
		}
#endif

		return true;
	}

	return false;
}

//Record the casting between pointers and structures
void CallGraphPass::handleCastEscapeType(Type *ToTy, Type *FromTy){
	
	int Idx = -1;
	if(ToTy->isPointerTy() && FromTy->isPointerTy()){
		string to_ty_str = getTypeStr(ToTy);
		string from_ty_str = getTypeStr(FromTy);
		if(to_ty_str == "i8*"){
			Type *Ty = FromTy->getPointerElementType();
			if(Ty->isStructTy()){
				StructType* STy = dyn_cast<StructType>(Ty);
				if(STy->isLiteral()){
					string Ty_name = Ctx->Global_Literal_Struct_Map[typeHash(Ty)];
					castEscapeSet.insert(Ty_name);
					typeNameTransitMap[Ty_name].insert("i8");
					typeNameTransitMap["i8"].insert(Ty_name);
				}
				else{
					StringRef Ty_name = Ty->getStructName();
					string parsed_Ty_name = parseIdentifiedStructName(Ty_name);
					castEscapeSet.insert(parsed_Ty_name);
					typeNameTransitMap[parsed_Ty_name].insert("i8");
					typeNameTransitMap["i8"].insert(parsed_Ty_name);

				}
			}
		}
		else if(from_ty_str == "i8*"){
			Type *Ty = ToTy->getPointerElementType();
			if(Ty->isStructTy()){
				StructType* STy = dyn_cast<StructType>(Ty);
				if(STy->isLiteral()){
					string Ty_name = Ctx->Global_Literal_Struct_Map[typeHash(Ty)];
					castEscapeSet.insert(Ty_name);
					typeNameTransitMap[Ty_name].insert("i8");
					typeNameTransitMap["i8"].insert(Ty_name);
				}
				else{
					StringRef Ty_name = Ty->getStructName();
					string parsed_Ty_name = parseIdentifiedStructName(Ty_name);
					castEscapeSet.insert(parsed_Ty_name);
					typeNameTransitMap[parsed_Ty_name].insert("i8");
					typeNameTransitMap["i8"].insert(parsed_Ty_name);
				}
			}
		}
	}
}

void CallGraphPass::escapeType(StoreInst* SI, Type *Ty, int Idx) {

	if(Ty->isStructTy()){
		StructType* STy = dyn_cast<StructType>(Ty);
		if(STy->isLiteral()){
			string Ty_name = Ctx->Global_Literal_Struct_Map[typeHash(Ty)];
			typeEscapeSet.insert(typeNameIdxHash(Ty_name, Idx));
			hashIDTypeMap[typeNameIdxHash(Ty_name, Idx)] = make_pair(Ty,Idx);
			escapedStoreMap[typeNameIdxHash(Ty_name, Idx)].insert(SI);
		}
		else{
			StringRef Ty_name = Ty->getStructName();
			string parsed_Ty_name = parseIdentifiedStructName(Ty_name);
			typeEscapeSet.insert(typeNameIdxHash(parsed_Ty_name, Idx));
			hashIDTypeMap[typeNameIdxHash(parsed_Ty_name, Idx)] = make_pair(Ty,Idx);
			escapedStoreMap[typeNameIdxHash(parsed_Ty_name, Idx)].insert(SI);

		}
	}

}

void CallGraphPass::transitType(Type *ToTy, Type *FromTy,
		int ToIdx, int FromIdx) {
	
	if (ToIdx != -1 && FromIdx != -1){
		//This part is under testing
		typeTransitMap[typeIdxHash(ToTy, ToIdx)].insert(typeIdxHash(FromTy, FromIdx));

#ifdef ENABLE_BIDIRECTIONAL_TYPE_CASTING
		typeTransitMap[typeIdxHash(FromTy, FromIdx)].insert(typeIdxHash(ToTy, ToIdx));
#endif

		hashIDTypeMap[typeIdxHash(ToTy,ToIdx)] = make_pair(ToTy,ToIdx);
		hashIDTypeMap[typeIdxHash(FromTy,FromIdx)] = make_pair(FromTy,FromIdx);
	}
	else{
		//may should iteratively update struct member info
		typeTransitMap[typeHash(ToTy)].insert(typeHash(FromTy));

#ifdef ENABLE_BIDIRECTIONAL_TYPE_CASTING
		//Type casting should be bidirectional
		typeTransitMap[typeHash(FromTy)].insert(typeHash(ToTy));
#endif
	}

	hashTypeMap[typeHash(ToTy)] = ToTy;
	hashTypeMap[typeHash(FromTy)] = FromTy;
}



bool CallGraphPass::doInitialization(Module *M) {

	DL = &(M->getDataLayout());
	CurrentLayout = DL;
	Int8PtrTy = Type::getInt8PtrTy(M->getContext());
	IntPtrTy = DL->getIntPtrType(M->getContext());

	static int iter_num = 0;
	OP<< iter_num++ <<": Module: "<<M->getName()<<"\n";

	//
	// Iterate and process globals
	//
	for (Module::global_iterator gi = M->global_begin(); 
			gi != M->global_end(); ++gi) {
		GlobalVariable* GV = &*gi;

		if (!GV->hasInitializer())
			continue;

		if(GV->getName().contains(".str."))
        	continue;

		//Filter C++ VTables
		if(GV->getName().startswith("_ZTV"))
			continue;

		if(GV->getName().startswith("_ZN"))
			continue;

		 if(GV->getName().startswith("_ZTI"))
		 	continue;
		
#ifdef TEST_ONE_INIT_GLOBAL
		if(GV->getName() != TEST_ONE_INIT_GLOBAL)
			continue;
#endif

		typeConfineInInitializer(GV);
	}

	// Iterate functions and instructions
	for (Function &F : *M) {

		if (F.isDeclaration()){
			
			if (F.hasAddressTaken()) {
				FuncSet FSet;
				FSet.clear();
				getGlobalFuncs(&F,FSet);
				funcSetMerge(Ctx->sigFuncsMap[funcHash(&F, false)], FSet);
				Ctx->AddressTakenFuncs.insert(&F);
			}
			
			continue;
		}

#ifdef TEST_ONE_INIT_STORE
		if (F.getName() != TEST_ONE_INIT_STORE)
			continue;
#endif

		// Todo: collect operations that store a parameter to a global

		set<BitCastOperator *>CastSet;
		for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
			Instruction *I = &*i;

			if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
				typeConfineInStore_new(SI);
			}

			//Only this cast is not enough, some cast occurs directly in function arguments!!
			else if (CastInst *CastI = dyn_cast<CastInst>(I)) {
				typeConfineInCast(CastI);
				Type *ToTy = CastI->getDestTy(), *FromTy = CastI->getSrcTy();
				handleIndirectCast(FromTy, ToTy);
#ifdef ENABLE_CAST_ESCAPE
            handleCastEscapeType(CastI->getDestTy(), CastI->getSrcTy());
#endif
			}

			// Operands of instructions can be BitCastOperator
			for (User::op_iterator OI = I->op_begin(), OE = I->op_end(); OI != OE; ++OI) {
				if (BitCastOperator *CO = dyn_cast<BitCastOperator>(*OI)) {
					CastSet.insert(CO);
				}
			}
		}

		for (auto Cast : CastSet) {
			Type *ToTy = Cast->getDestTy(), *FromTy = Cast->getSrcTy();

			if(ToTy->isPointerTy() && FromTy->isPointerTy()){
				Type *ToeleType = ToTy->getPointerElementType();
				Type *FromeleType = FromTy->getPointerElementType();
				if(ToeleType->isFunctionTy() && FromeleType->isFunctionTy()){
					funcTypeCastMap[typeHash(ToeleType)].insert(typeHash(FromeleType));
					hashTypeMap[typeHash(ToeleType)] = ToeleType;
					hashTypeMap[typeHash(FromeleType)] = FromeleType;
				}

				//Sometimes a function pointer type is casted to i8*
				string to_ty_str = getTypeStr(ToTy);
				string from_ty_str = getTypeStr(FromTy);
				if(FromeleType->isFunctionTy() && to_ty_str == "i8*"){

					Value* op = Cast->getOperand(0);
					if(Function *F = dyn_cast<Function>(op)){
						FuncSet FSet;
						FSet.clear();

						if(F->isDeclaration()){
							getGlobalFuncs(F,FSet);
						}
						else{
							FSet.insert(F);
						}
						funcSetMerge(escapeFuncSet, FSet);
					}
				}

				handleIndirectCast(FromTy, ToTy);
			}

#ifdef ENABLE_CAST_ESCAPE
            handleCastEscapeType(ToTy, FromTy);
#endif

			typeConfineInCast(FromTy,ToTy);
		}

		// Collect address-taken functions.
		if (F.hasAddressTaken()) {
			Ctx->AddressTakenFuncs.insert(&F); //not used in mlta
			Ctx->sigFuncsMap[funcHash(&F, false)].insert(&F); //hash without name, only type info
		}

		// Keep a single copy for same functions (inline functions)
		size_t fh = funcHash(&F);
		if (Ctx->UnifiedFuncMap.find(fh) == Ctx->UnifiedFuncMap.end()) {
			Ctx->UnifiedFuncMap[fh] = &F;
			Ctx->UnifiedFuncSet.insert(&F);

			if (F.hasAddressTaken()) {
				Ctx->sigFuncsMap[funcHash(&F, false)].insert(&F);
			}
		}
	}

	return false;
}

static bool analyze_once = true;

bool CallGraphPass::doFinalization(Module *M) {

	return false;
}

bool CallGraphPass::doModulePass(Module *M) {

	if(analyze_once == true){

		for(auto it = Ctx->sigFuncsMap.begin(); it != Ctx->sigFuncsMap.end(); it++){
			FuncSet fset = it->second;
			if(fset.empty())
				continue;
			
			Function *f = *fset.begin();

			int ArgNo = f->arg_size();

			funcTypeMap[ArgNo].insert(make_pair(it->first, f->getFunctionType()));
		}

		analyze_once = false;
	}

	DL = &(M->getDataLayout());
	CurrentLayout = DL;

	// Use type-analysis to concervatively find possible targets of 
	// indirect calls.
	for (Module::iterator f = M->begin(), fe = M->end(); 
			f != fe; ++f) {

		Function *F = &*f;
		//OP<<"F: "<<F->getName()<<"\n";

#ifdef TEST_ONE_FUNC
		if(F->getName()!=TEST_ONE_FUNC){
            continue;
        }
#endif

		//Unroll loops
#ifdef UNROLL_LOOP_ONCE
		unrollLoops(F);

		//Check the loop unroll result
		if(checkLoop(F)){
			OP << "Loop unroll failed!!!\n";
			Ctx->Loopfuncs.insert(F);
			continue;
		}
		else if(!topSort(F)){
			OP << "topSort not pass!!!\n";
			Ctx->Loopfuncs.insert(F);
			continue;
		}
#endif
		int icall_id = 0;
		for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
			// Map callsite to possible callees.
			if (CallInst *CI = dyn_cast<CallInst>(&*i)) {

				bool icalltag = false;
				FuncSet FS;
				FS.clear();
				Function *CF = CI->getCalledFunction(); //This API should be replaced in LLVM 15

				//Ignore llvm debug funcs while constructing global call graph
				if(CF){
					auto icall_name = CF->getName();
					if(icall_name.contains("llvm.dbg"))
						continue;
				}

				// Indirect call
				if (CI->isIndirectCall()) {

#ifdef IGNORE_LINUX_STATIC_CALL
					if(checkValidIcalls(CI) == false)
						continue;
#endif
					icall_id++;

					if(globalFuncNameMap.count(F->getName().str())){
						size_t hash = callInfoHash(CI, F, icall_id);
						if(globalFuncNameMap[F->getName().str()].count(hash)){			
							continue;
						}
					}

					icalltag = true;
#ifdef MLTA_FOR_INDIRECT_CALL
					// Find the actual called function of CI
					findCalleesWithMLTA(CI, FS);
#elif SOUND_MODE
					findCalleesByType(CI, FS);
#endif

					for (Function *CalleeFunc : FS) {
						Ctx->Callers[CalleeFunc].insert(CI);
						Ctx->ICallers[CalleeFunc].insert(CI);
					}

					Ctx->IndirectCallInsts.push_back(CI);
					globalFuncNameMap[F->getName().str()].insert(callInfoHash(CI, F, icall_id));

				}
				// Direct call
				else {
					// not InlineAsm
					if (CF) {
						FuncSet FSet;
						FSet.clear();

						if(CF->empty()){
							getGlobalFuncs(CF,FSet);
						}
						else{
							FSet.insert(CF);
						}

						for(auto it = FSet.begin(); it != FSet.end(); it++){
							CF = *it;
							// Use unified function
							size_t fh = funcHash(CF);
							CF = Ctx->UnifiedFuncMap[fh];
							if (CF) {
								FS.insert(CF);
								Ctx->Callers[CF].insert(CI);
							}
						}
					}
					// InlineAsm
					else {
						
					}
				}

				Ctx->Callees[CI] = FS;
				if(icalltag){
					Ctx->icallTargets+=FS.size();
					Ctx->ICallees[CI] = FS;
				}
			}
		}
	}//end for loop

  return false;
}