#include "CallGraph.h"
#include <cstddef>

//#define PRINT_LAYER_SET
//#define ENHANCED_ONE_LAYER_COLLECTION
//#define ENABLE_FUNCTYPE_CAST
//#define ENABLE_FUNCTYPE_ESCAPE
//#define ENABLE_CONSERVATIVE_ESCAPE_HANDLER

CallInst * current_CI;

bool CallGraphPass::isEscape(string LayerTyName, int FieldIdx, CallInst *CI){

	if(typeEscapeSet.find(typeNameIdxHash(LayerTyName, FieldIdx)) != typeEscapeSet.end()){
		escapedTypesInTypeAnalysisSet.insert(typeNameIdxHash(LayerTyName, FieldIdx));
		return true;
	}

	return false;
}

bool CallGraphPass::isEscape(Type *LayerTy, int FieldIdx, CallInst *CI){

	//If we use type name, we cannot resolve anon struct
	if(LayerTy->isStructTy()){
		StructType* LayerSTy = dyn_cast<StructType>(LayerTy);
		if(LayerSTy->isLiteral()){
			string Ty_name = Ctx->Global_Literal_Struct_Map[typeHash(LayerTy)];

			if(typeEscapeSet.find(typeNameIdxHash(Ty_name, FieldIdx)) != typeEscapeSet.end()){
				escapedTypesInTypeAnalysisSet.insert(typeNameIdxHash(Ty_name, FieldIdx));
				return true;
			}
		}
		else{
			StringRef Ty_name = LayerTy->getStructName();
			string parsed_Ty_name = parseIdentifiedStructName(Ty_name);
			if(typeEscapeSet.find(typeNameIdxHash(parsed_Ty_name, FieldIdx)) != typeEscapeSet.end()){
				escapedTypesInTypeAnalysisSet.insert(typeNameIdxHash(parsed_Ty_name, FieldIdx));
				return true;
			}
		}
	}

	return false;
}

//Given the previous layer result of type analysis, current type info,
//Output the next layer result of type analysis into FS
//TODO: stop early return, collect all escape conditions.
void CallGraphPass::findCalleesWithTwoLayerTA(CallInst *CI, FuncSet PreLayerResult, Type *LayerTy, 
	int FieldIdx, FuncSet &FS, int &LayerNo, int &escape){

	FS.clear();
	escape = 0;

	current_CI = CI;

	// Step 1: ensure the type hasn't escaped
	if(isEscape(LayerTy, FieldIdx, CI)){
		escape = 1;
		return;
	}

	LayerNo++;
	
	FuncSet merge_set, nextLayerResult, FST;
	merge_set.clear();
	
	// Step 2: get the funcset based on current layer and merge
	nextLayerResult.clear();
	nextLayerResult = typeFuncsMap[typeIdxHash(LayerTy, FieldIdx)]; //direct result

	string LayerTy_name = "";

	if(LayerTy->isStructTy()){
		StructType* LayerSTy = dyn_cast<StructType>(LayerTy);
		if(!LayerSTy->isLiteral()){
			auto Ty_name = LayerTy->getStructName();
			LayerTy_name = parseIdentifiedStructName(Ty_name);
		}
		else{
			LayerTy_name = Ctx->Global_Literal_Struct_Map[typeHash(LayerTy)];
		}
	}
	else{
		//Other cases will be resolved in merging Global_EmptyTy_Funcs
	}

	funcSetMerge(nextLayerResult, typeFuncsMap[typeNameIdxHash(LayerTy_name, FieldIdx)]);
	funcSetMerge(merge_set, nextLayerResult);
	
	// Step 3: get transited funcsets and merge
	// NOTE: this nested loop can be slow
	size_t TH = typeHash(LayerTy);
	list<size_t> LT;
	set<size_t> PT;

	LT.push_back(TH);
	while (!LT.empty()) {
		size_t CT = LT.front();
		LT.pop_front();

		if (PT.find(CT) != PT.end())
			continue;
        PT.insert(CT);

		Type* Cty = hashTypeMap[CT];
		for (auto H : typeTransitMap[CT]) {

			Type* Hty = hashTypeMap[H];

			//Make sure the transitted types haven't escape
			if(isEscape(Hty, FieldIdx, CI)){
				escape = 1;
				return;
			}

			nextLayerResult = typeFuncsMap[hashIdxHash(H, FieldIdx)];
			
			if(Hty->isStructTy()){
				StructType* SHty = dyn_cast<StructType>(Hty);
				if(SHty->isLiteral()){
					string Ty_name = Ctx->Global_Literal_Struct_Map[typeHash(Hty)];
					funcSetMerge(nextLayerResult, typeFuncsMap[typeNameIdxHash(Ty_name, FieldIdx)]);
				}
				else{
					StringRef Ty_name = Hty->getStructName();
					string parsed_Ty_name = parseIdentifiedStructName(Ty_name);
					funcSetMerge(nextLayerResult, typeFuncsMap[typeNameIdxHash(parsed_Ty_name, FieldIdx)]);
				}
			}

			funcSetMerge(merge_set, nextLayerResult);
			LT.push_back(H);
		}

		//Setp 5: if a struct pointer could cast to i8*, we assume they could cast to each other

	}
	PT.clear();
	LT.clear();

	size_t TDH = typeIdxHash(LayerTy,FieldIdx);
	LT.push_back(TDH);
	while (!LT.empty()) {
		size_t CT = LT.front();
		LT.pop_front();

		if (PT.find(CT) != PT.end())
			continue;
        PT.insert(CT);

		for (auto H : typeTransitMap[CT]) {

			auto Hidty = hashIDTypeMap[H];
			Type* Hty = Hidty.first;
			
			//Make sure the transitted types haven't escape
			if(isEscape(Hty, FieldIdx, CI)){
				escape = 1;
				return;
			}

			nextLayerResult = typeFuncsMap[H];
			
			if(Hty->isStructTy()){
				StructType* SHty = dyn_cast<StructType>(Hty);
				if(SHty->isLiteral()){
					string Ty_name = Ctx->Global_Literal_Struct_Map[typeHash(Hty)];
					funcSetMerge(nextLayerResult, typeFuncsMap[typeNameIdxHash(Ty_name, FieldIdx)]);
				}
				else{
					StringRef Ty_name = Hty->getStructName();
					string parsed_Ty_name = parseIdentifiedStructName(Ty_name);
					funcSetMerge(nextLayerResult, typeFuncsMap[typeNameIdxHash(parsed_Ty_name, FieldIdx)]);
				}
			}

			funcSetMerge(merge_set, nextLayerResult);

			LT.push_back(H);
		}
	}
	PT.clear();
	LT.clear();
	
	
	// Step 4: get confined funcsets and merge

	TDH = typeNameIdxHash(LayerTy_name,FieldIdx);

	LT.push_back(TDH);
	while (!LT.empty()) {
		size_t CT = LT.front();
		LT.pop_front();

		if (PT.find(CT) != PT.end())
			continue;
        PT.insert(CT);
	
		for (auto H : typeConfineMap[CT]) {
			auto Hidty = hashIDTypeMap[H];
			Type* Hty = Hidty.first;
			int Hid = Hidty.second;

			//Make sure the transitted types haven't escape
			if(isEscape(Hty, Hid, CI)){
				escape = 1;
				return;
			}

			nextLayerResult = typeFuncsMap[H];
			
			if(Hty->isStructTy()){
				StructType* SHty = dyn_cast<StructType>(Hty);
				if(SHty->isLiteral()){
					string Ty_name = Ctx->Global_Literal_Struct_Map[typeHash(Hty)];
					funcSetMerge(nextLayerResult, typeFuncsMap[typeNameIdxHash(Ty_name, Hid)]);
				}
				else{
					StringRef Ty_name = Hty->getStructName();
					string parsed_Ty_name = parseIdentifiedStructName(Ty_name);
					funcSetMerge(nextLayerResult, typeFuncsMap[typeNameIdxHash(parsed_Ty_name, Hid)]);
				}
			}

			funcSetMerge(merge_set, nextLayerResult);
			LT.push_back(H);
		}
	
	}

	FST.clear();

#ifdef PRINT_LAYER_SET
	OP<<"PreLayerResult: \n";
	for(auto it = PreLayerResult.begin(); it!=PreLayerResult.end();it++){
		Function* f= *it;
		OP<<"f: "<<f->getName()<<"\n";
	}

	OP<<"merge_set: \n";
	for(auto it = merge_set.begin(); it!=merge_set.end();it++){
		Function* f= *it;
		OP<<"f: "<<f->getName()<<"\n";
	}
#endif

	funcSetIntersection(PreLayerResult, merge_set, FST);

	//Add the lost type func
	funcSetMerge(FST, Ctx->Global_EmptyTy_Funcs[callHash(CI)]);

	if(!FST.empty()){
		FS = FST;
	}
}



// Initial set: first-layer results (only function type match)
// Here we consider the arg type casting
void CallGraphPass::getOneLayerResult(CallInst *CI, FuncSet &FS){
	
	FS = Ctx->sigFuncsMap[callHash(CI)];

	// Handle function types with variable parameters
	if(FS.empty()){
		resolveVariableParameters(CI, FS);
	}

#ifdef ENABLE_FUNCTYPE_CAST
    for(size_t casthash : funcTypeCastMap[callHash(CI)]){
    	funcSetMerge(FS, Ctx->sigFuncsMap[casthash]);
	}
#endif

	if(Ctx->oneLayerFuncsMap.count(callHash(CI))){
		FS = Ctx->oneLayerFuncsMap[callHash(CI)];
		return;
	}
	
#ifdef ENHANCED_ONE_LAYER_COLLECTION

	unsigned argnum = CI->arg_size();
	if(argnum == 0)
		return;
	
	if(funcTypeMap.count(argnum) == 0)
		return;

	for(auto it = funcTypeMap[argnum].begin(); it != funcTypeMap[argnum].end(); it++){
		
		size_t funcsethash = it->first;
		FunctionType* fty = it->second;

		if(funcsethash == callHash(CI))
			continue;

		Type* c_return_ty = CI->getFunctionType()->getReturnType();
		Type* f_return_ty = fty->getReturnType();
		string c_return_ty_str = getTypeStr(c_return_ty);
		string f_return_ty_str = getTypeStr(f_return_ty);

		if(c_return_ty_str != f_return_ty_str){
			if((c_return_ty_str != "i8*") && (c_return_ty_str != "i8**")){
				continue;
			}

			if(typeStrCastMap.count(c_return_ty_str)){
				if(typeStrCastMap[c_return_ty_str].count(f_return_ty_str) == 0)
					continue;
			}
		}
		
		bool pass_check = true;
		for (unsigned i = 0; i < argnum; i++) {
			Value* cai_arg = CI->getArgOperand(i);
        	Type* cai_argTy = cai_arg->getType();
			Type* func_argTy = fty->getParamType(i);

			string cai_argTyStr = getTypeStr(cai_argTy);
			string func_argTyStr = getTypeStr(func_argTy);

			if(cai_argTyStr == func_argTyStr)
				continue;
			
			if((cai_argTyStr != "i8*") && (cai_argTyStr != "i8**")){
				pass_check = false;
				break;
			}

			if(typeStrCastMap.count(cai_argTyStr)){
				if(typeStrCastMap[cai_argTyStr].count(func_argTyStr))
					continue;
			}
			
			pass_check = false;
			break;
		}

		if(pass_check){
			funcSetMerge(FS, Ctx->sigFuncsMap[funcsethash]);
		}
	}

	Ctx->oneLayerFuncsMap[callHash(CI)] = FS;

#endif

}

//It seems that the function parameter info is not considered?
bool CallGraphPass::findCalleesWithMLTA(CallInst *CI, FuncSet &FS) {

	// Initial set: first-layer results (only function type match)
	FuncSet FS1;
	getOneLayerResult(CI, FS1);

#ifdef ENABLE_FUNCTYPE_ESCAPE

	funcSetMerge(FS1, escapeFuncSet);
	
#endif

	Ctx->icallTargets_OneLayer += FS1.size();

	if (FS1.size() == 0) {
        Ctx->Global_MLTA_Reualt_Map[CI] = MissingBaseType;
		return false;
	}


	FuncSet FS2, FST;

	Type *LayerTy = NULL;
	int FieldIdx = -1;
	Value *CV = CI->getCalledOperand();

	set<Value*> VisitedSet;
	list<CompositeType> TyList;
	set<TypeAnalyzeResult> TAResultSet;

	bool layer_result = nextLayerBaseType_new(CV, TyList, VisitedSet, Precise_Mode);

	int LayerNo = 1;
	int escapeTag = 0;
	int VTable_idx = -1;
	Type* virtial_sty = NULL;

	if(layer_result){
		//Once we step in here, CI must have all 2-layer info, one-layer case is imposible
		for(CompositeType CT : TyList){
			
			LayerTy = CT.first;
			FieldIdx = CT.second;

			if(LayerTy->getStructName().contains(".anon")){
				Ctx->Global_pre_anon_icall_num++;
			}
			
			findCalleesWithTwoLayerTA(CI, FS1, LayerTy, FieldIdx, FST, LayerNo, escapeTag);
			if(!FST.empty()){
				TAResultSet.insert(TwoLayer);
				FS2.insert(FST.begin(), FST.end());
			}
			else{

				if(escapeTag==0){
					TAResultSet.insert(NoTwoLayerInfo);
#ifdef ENABLE_CONSERVATIVE_ESCAPE_HANDLER
					//FS1.clear();
#else				
					FS1.clear();
#endif
				}
				else{
					TAResultSet.insert(TypeEscape);
				}
			}
		}

		//Resolve TAResult
		if(TAResultSet.size() == 1){
			TypeAnalyzeResult TAResult = *(TAResultSet.begin());
			Ctx->Global_MLTA_Reualt_Map[CI] = TAResult;
			if(TAResult == TwoLayer){
				FS1 = FS2;
			}
			else if(TAResult == NoTwoLayerInfo){
				//FS1.clear();
			}
			else{
				//do nothing, the final result is one-layer
			}
		}
		else if(TAResultSet.count(TypeEscape)){
			Ctx->Global_MLTA_Reualt_Map[CI] = TypeEscape;
		}
		else{
			Ctx->Global_MLTA_Reualt_Map[CI] = MixedLayer;
			FS1 = FS2;
		}

	}

	//Handle CPP virtual function
	//TODO: need more testcases to improve this func
	else if(getCPPVirtualFunc(CV, VTable_idx, virtial_sty)){

		//Note: the class type could cast to other type
		string class_name = virtial_sty->getStructName().str();
		size_t typehash = typeHash(virtial_sty);

		list<string> worklist;
		worklist.push_back(class_name);
		for (auto H : typeTransitMap[typehash]){
			Type* Hty = hashTypeMap[H];
			string Hty_name = Hty->getStructName().str();
			worklist.push_back(Hty_name);
		}
		bool found_tag = false;
		for (auto type_name : worklist){
			if(GlobalVTableMap.count(type_name)){
				if(GlobalVTableMap[type_name].size() > VTable_idx){
					Value *vfunc = GlobalVTableMap[type_name][VTable_idx];
					if (Function *F = dyn_cast<Function>(vfunc)) {
						if(!found_tag){
							FS1.clear();
							Ctx->Global_MLTA_Reualt_Map[CI] = VTable;
							found_tag = true;
						}
						FS1.insert(F);
					}
				}
			}
			else {
				Ctx->Global_MLTA_Reualt_Map[CI] = OneLayer;
			}
		}
	}
	else{

		CV = CI->getCalledOperand();
		set<CompositeType> CTSet;
		CTSet.clear();
		//checkTypeStoreFunc(CV,CTSet);
		if(!CTSet.empty()){
			//CV comes from a return value of a function call
			FuncSet mergeFS;
			mergeFS.clear();
            int isTransEscape = 0;
			for(auto it = CTSet.begin(); it != CTSet.end(); it++){
				Type *ty = it->first;
				int fieldIdx = it->second;
				findCalleesWithTwoLayerTA(CI, FS1, ty, fieldIdx, FST, LayerNo, escapeTag);
                if(escapeTag == 0)
				    funcSetMerge(mergeFS,FST);
                else{
                    isTransEscape = 1;
                    break;
                }
			}

			if(!mergeFS.empty()){
                Ctx->Global_MLTA_Reualt_Map[CI] = TwoLayer;
				FS1 = mergeFS;
			}
			else{
                if(isTransEscape == 0){
                    Ctx->Global_MLTA_Reualt_Map[CI] = NoTwoLayerInfo;
                    FS1.clear();
                }
                else{
                    Ctx->Global_MLTA_Reualt_Map[CI] = TypeEscape;
                }
			}
		}
		else{

            Ctx->Global_MLTA_Reualt_Map[CI] = OneLayer;
		}

	}

	FS = FS1;

	if(LayerNo > 1){
		Ctx->valied_icallNumber++;
		Ctx->valied_icallTargets+=FS.size();
	}

	#if 0
	if (LayerNo > 1 && FS.size()) {
		OP<<"[CallGraph] Indirect call: "<<*CI<<"\n";
		printSourceCodeInfo(CI);
		OP<<"\n\t Indirect-call targets:\n";
		for (auto F : FS) {
			printSourceCodeInfo(F);
		}
		OP<<"\n";
	}
	#endif
	return true;
}