#include "CallGraph.h"

//This function could speed up
void CallGraphPass::funcSetIntersection(FuncSet &FS1, FuncSet &FS2, 
		FuncSet &FS) {
	FS.clear();
	
	for (auto F : FS1) {
		//Do not use pointer, use name, or we will miss function delcare
		if (FS2.find(F) != FS2.end())
			FS.insert(F);
	}

	//Use string match, 
	map<string, Function *> FS1_name_set, FS2_name_set;
	FS1_name_set.clear();
	FS2_name_set.clear();

	for (auto F : FS1) {
		string f_name = F->getName().str();
		if(f_name.size()>0)
			FS1_name_set.insert(make_pair(f_name,F));
	}

	for (auto F : FS2) {
		string f_name = F->getName().str();
		if(f_name.size()>0)
			FS2_name_set.insert(make_pair(f_name,F));
	}

	for (auto FName : FS1_name_set) {
		//Do not use pointer, use name, or we will miss function delcare
		if (FS2_name_set.find(FName.first) != FS2_name_set.end())
			FS.insert(FName.second);
	}
}	

//This function could speed up
//Merge FS2 into FS1
void CallGraphPass::funcSetMerge(FuncSet &FS1, FuncSet &FS2){
	for(auto F : FS2)
		FS1.insert(F);
}

Type *CallGraphPass::getLayerTwoType(Type* baseTy, vector<int> ids){

	StructType *sty = dyn_cast<StructType>(baseTy);
	if(!sty)
		return NULL;
	
	for(auto it = ids.begin(); it!=ids.end(); it++){
		int idx = *it;
		Type* subTy = sty->getElementType(idx);
		StructType *substy = dyn_cast<StructType>(subTy);
		if(!substy)
			return NULL;
		
		sty = substy;
	}

	return sty;
}

void CallGraphPass::updateStructInfo(Function *F, Type* Ty, int idx, Value* context){

	//Union prelayer is regarded as escape
	if(Ctx->Globa_Union_Set.count(typeHash(Ty))){
		Ctx->num_local_info_name++;
		Ctx->Global_EmptyTy_Funcs[funcHash(F, false)].insert(F);
		return;
	}

	//Pre layer is struct without name
	bool isLiteralTag = false;
	if(Ty->isStructTy()){
		StructType* STy = dyn_cast<StructType>(Ty);
		isLiteralTag = STy->isLiteral();
	}

	if(Ty->isStructTy() && isLiteralTag){
		string Funcname = F->getName().str();

		if(Ctx->Global_Literal_Struct_Map.count(typeHash(Ty))){
			//OP<<"empty struct name but have debug info\n";
			Ctx->num_emptyNameWithDebuginfo++;
			string TyName = Ctx->Global_Literal_Struct_Map[typeHash(Ty)];
			if(checkStringContainSubString(TyName, "union.")){
				Ctx->num_local_info_name++;
				if(Funcname.size() != 0)
					Ctx->Global_EmptyTy_Funcs[funcHash(F, false)].insert(F);
				return;
			}

			if(checkStringContainSubString(TyName, ".anon")){
				Ctx->num_local_info_name++;
				if(Funcname.size() != 0)
					Ctx->Global_EmptyTy_Funcs[funcHash(F, false)].insert(F);
				return;
			}

			typeFuncsMap[typeNameIdxHash(TyName, idx)].insert(F);
			hashIDTypeMap[typeNameIdxHash(TyName,idx)] = make_pair(Ty,idx);
			if(context){
				Func_Init_Map[context][F].insert(typeNameIdxHash(TyName, idx));
			}
		}
		else{

			//TODO: trace the typename in debuginfo
			Ctx->num_emptyNameWithoutDebuginfo++;
			
			if(Funcname.size() != 0)
				Ctx->Global_EmptyTy_Funcs[funcHash(F, false)].insert(F);
		}
	}
	
	//Pre layer is struct with name
	else if(Ty->isStructTy()){

		Ctx->num_haveLayerStructName++;
		auto TyName = Ty->getStructName().str();
		if(checkStringContainSubString(TyName, "union.")){
			Ctx->Globa_Union_Set.insert(typeHash(Ty));
			Ctx->num_local_info_name++;
			string Funcname = F->getName().str();
			if(Funcname.size() != 0){
				Ctx->Global_EmptyTy_Funcs[funcHash(F, false)].insert(F);
			}
			return;
		}

		if(checkStringContainSubString(TyName, ".anon")){
			Ctx->num_local_info_name++;
			string Funcname = F->getName().str();
			if(Funcname.size() != 0){
				Ctx->Global_EmptyTy_Funcs[funcHash(F, false)].insert(F);
			}
			return;
		}

		string parsed_TyName = parseIdentifiedStructName(TyName);
		typeFuncsMap[typeNameIdxHash(parsed_TyName, idx)].insert(F);
		hashIDTypeMap[typeNameIdxHash(parsed_TyName,idx)] = make_pair(Ty,idx);
		if(context){
			Func_Init_Map[context][F].insert(typeNameIdxHash(parsed_TyName, idx));
		}
	}
	
	//Pre type layer is array
	else if(Ty->isArrayTy()){
		Ctx->num_array_prelayer++;
		Ctx->num_local_info_name++;
		Ctx->Global_EmptyTy_Funcs[funcHash(F, false)].insert(F);
	}
	else{
		Ctx->num_local_info_name++;
		Ctx->Global_EmptyTy_Funcs[funcHash(F, false)].insert(F);
	}
}

string CallGraphPass::parseIdentifiedStructName(StringRef str_name){

    if(str_name.size() == 0)
        return "";

    if(str_name.contains("struct.")){
        string substr = str_name.str();
        substr = substr.substr(7, substr.size()-1); //remove "struct." in name
        return substr;
    }
    else if(str_name.contains("union.")){
        string substr = str_name.str();
        substr = substr.substr(6, substr.size()-1); //remove "union." in name
        return substr;
    }
	else if(str_name.contains("class.")){
        string substr = str_name.str();
        substr = substr.substr(6, substr.size()-1); //remove "class." in name
        return substr;
    }

    return "";
}

size_t CallGraphPass::callInfoHash(CallInst* CI, Function *Caller, int index){
    
    hash<string> str_hash;
	string output;
    output = getInstFilename(CI);

	string sig;
	raw_string_ostream rso(sig);
	Type *FTy = Caller->getFunctionType();
	FTy->print(rso);
	output += rso.str();
	output += Caller->getName();

	string::iterator end_pos = remove(output.begin(), 
			output.end(), ' ');
	output.erase(end_pos, output.end());
	
    stringstream ss;
    unsigned linenum = getInstLineNo(CI);
    ss<<linenum;
	ss<<index;
	output += ss.str();

	return str_hash(output);

}

//Given a func declare, find its global definition
void CallGraphPass::getGlobalFuncs(Function *F, FuncSet &FSet){

    StringRef FName = F->getName();

    if(Ctx->GlobalFuncs.count(FName.str())){
        set<size_t> hashSet = Ctx->GlobalFuncs[FName.str()];
        for(auto it = hashSet.begin(); it != hashSet.end(); it++){
            Function *f = Ctx->Global_Unique_Func_Map[*it];
			FSet.insert(f);
        }
    }

	if(FSet.empty()){
		size_t funchash = funcInfoHash(F);
		if(Ctx->Global_Unique_Func_Map.count(funchash)){
			Function *f = Ctx->Global_Unique_Func_Map[funchash];
			FSet.insert(f);
		}
	}
}

bool CallGraphPass::checkValidStructName(Type *Ty){

	if(!Ty->isStructTy())
		return false;

	StructType* STy = dyn_cast<StructType>(Ty);

	if(!STy->isLiteral()){

		auto TyName = Ty->getStructName();
		if(TyName.contains(".union")){
			return false;
		}

		if(TyName.contains(".anon")){
			return false;
		}

		return true;
	}
	else{
		if(Ctx->Global_Literal_Struct_Map.count(typeHash(Ty))){

			string TyName = Ctx->Global_Literal_Struct_Map[typeHash(Ty)];
			if(checkStringContainSubString(TyName, ".union")){
				return false;
			}

			if(checkStringContainSubString(TyName, ".anon")){
				return false;
			}

			return true;
		}
		else{
			return false;
		}
	}
}

//Used to check the Linux Static calls
//This could be improved
bool CallGraphPass::checkValidIcalls(CallInst *CI){

	unsigned line_number = getInstLineNo(CI);
	string file_loc = getInstFilename(CI);

	for(auto i : Ctx->IcallIgnoreLineNum){
		if(to_string(line_number) == i){
			for(auto j : Ctx->IcallIgnoreFileLoc){
				if(file_loc == j){
					return false;
				}
			}
		}
	}

	return true;
}