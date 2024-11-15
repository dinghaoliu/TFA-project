#include "TypeBuilder.h"

using namespace llvm;

string TypeBuilderPass::parseIdentifiedStructName(StringRef str_name){

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

void TypeBuilderPass::updateTypeQueue(Type* Ty, deque<Type*> &Ty_queue){
    
    unsigned subnum = Ty->getNumContainedTypes();
			
    vector<Type*> struct_fields;
    struct_fields.clear();
    for(auto it = 0; it < subnum; it++){
        Type* subtype = Ty->getContainedType(it);
        struct_fields.push_back(subtype);
    }

    reverse(begin(struct_fields), end(struct_fields));

    for(auto it = struct_fields.begin(); it != struct_fields.end(); it++){
        Type* subtype = *it;
        Ty_queue.push_front(subtype);
    }
}

bool TypeBuilderPass::updateUserQueue(User* U, deque<User*> &U_queue){
    
    vector<Value*> struct_fields;
    for (auto oi = U->op_begin(), oe = U->op_end(); oi != oe; ++oi) {
        Value *O = *oi;
        Type *OTy = O->getType();
        //OP<<"O: "<<*O<<"\n";
        UndefValue *UV = dyn_cast<UndefValue>(O);
        if(UV){
            //OP<<"UndefValue\n";
            continue;
        }

        struct_fields.push_back(O);
    }

    if(struct_fields.empty()){
        OP<<"empty struct_fields\n";
        return false;
    }

    reverse(begin(struct_fields), end(struct_fields));

    for(auto it = struct_fields.begin(); it != struct_fields.end(); it++){
        Value* v = *it;
        User *OU = dyn_cast<User>(v);
        U_queue.push_front(OU);
    }

    return true;
}

void TypeBuilderPass::updateQueues(Type* Ty1, Type* Ty2, deque<Type*> &Ty_queue){
    
    Type* subtype2 = Ty2->getArrayElementType();
    unsigned subnum2 = Ty2->getArrayNumElements();

    if(subtype2->getTypeID() != Ty1->getTypeID())
        return;
    
    for(int i = 0; i < subnum2-1; i++){
        Type* type1 = Ty_queue.front();
        Ty_queue.pop_front();

        if(subtype2->getTypeID() != type1->getTypeID()){
            OP<<"error in updateQueues\n";
            return;
        }
    }
}

size_t funcTypeHash(Type *FTy) {

	hash<string> str_hash;
	string output;

    string sig;
    raw_string_ostream rso(sig);
    FTy->print(rso);
    output = rso.str();

	string::iterator end_pos = remove(output.begin(), 
			output.end(), ' ');
	output.erase(end_pos, output.end());
	return str_hash(output);
}

vector<vector<Type*>> ParamTysArray;

//Check if function arg cast to another type (for function pointer args)
bool TypeBuilderPass::checkArgCast(Function *F){

    bool result = false;

    int num = 0;

    map<unsigned, set<Type*>> CastMap;
    CastMap.clear();

    for (Function::arg_iterator FI = F->arg_begin(), FE = F->arg_end(); FI != FE; ++FI) {

        Type* argTy = FI->getType();
        unsigned argno = FI->getArgNo();
        CastMap[argno].insert(argTy);

        if(argTy->isPointerTy() || true){

            for(User *U : FI->users()){
                BitCastInst *BCI = dyn_cast<BitCastInst>(U);
                if(BCI){

                    Value *ToV = BCI;
                    Value *FromV = BCI->getOperand(0);
	                Type *ToTy = ToV->getType(), *FromTy = FromV->getType();
                    CastMap[argno].insert(ToTy);

                    result = true;
                    num++;
                }
            }
        }
    }

    if(result){

        for(auto it = CastMap.begin(); it != CastMap.end(); it++){
            int argno = it->first;
            set<Type*> castTypes = it->second;
            for(auto j = castTypes.begin(); j!=castTypes.end(); j++){
                Type* castTy = *j;
            }
        }

        vector<Type*> cur_results;
        cur_results.clear();
        combinate(0,CastMap, cur_results);


        Type* returnTy = F->getReturnType();
        for(auto it = ParamTysArray.begin(); it != ParamTysArray.end(); it++){
            vector<Type*> cur_results = *it;
            FunctionType *new_func_type = FunctionType::get(returnTy,cur_results,false);
            size_t typehash = funcTypeHash(new_func_type);
            Ctx->Global_Arg_Cast_Func_Map[F].insert(typehash);
            Ctx->sigFuncsMap[typehash].insert(F); 
					
        }
    }

    ParamTysArray.clear();

    return result;
}

void TypeBuilderPass::combinate(int start, map<unsigned, set<Type*>> CastMap, 
    vector<Type*> &cur_results){
    int size = CastMap.size();

    //Collection is over
    if(start == CastMap.size()){
        ParamTysArray.push_back(cur_results);
        return;
    }
    
    set<Type*> typeSet = CastMap[start];
    for(auto it = typeSet.begin(); it!=typeSet.end(); it++){
        Type* castTy = *it;
        cur_results.push_back(castTy);
        combinate(start+1, CastMap, cur_results);
        cur_results.pop_back();
    }

}