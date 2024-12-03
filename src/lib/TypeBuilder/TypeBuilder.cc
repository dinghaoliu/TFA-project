#include "TypeBuilder.h"

using namespace llvm;

//#define TEST_ONE_INIT_GLOBAL ""
//#define DEBUG_PRINT

map<size_t, DIType*> TypeBuilderPass::structDebugInfoMap;
map<string, StructType*> TypeBuilderPass::identifiedStructType;

map<size_t, string> TypeBuilderPass::ArrayBaseDebugTypeMap;
set<GlobalVariable*> TypeBuilderPass::targetGVSet;

void TypeBuilderPass::checkGlobalDebugInfo(GlobalVariable *GV, size_t Tyhash){

    MDNode *N = GV->getMetadata("dbg");
    if (!N)
        return;
    
    DIGlobalVariableExpression *DLGVE = dyn_cast<DIGlobalVariableExpression>(N);
    if(!DLGVE)
        return;

    DIGlobalVariable* DIGV = DLGVE->getVariable();
    if(!DIGV)
        return;

    DIType *DITy = DIGV->getType();
    if(!DITy)
        return;

    DIType * currentDITy = DITy;
    while(true){

        DIDerivedType *DIDTy = dyn_cast<DIDerivedType>(currentDITy);
        if(DIDTy){
            currentDITy = DIDTy->getBaseType();
            continue;
        }

        //Our target is CompositeType
        DICompositeType *DICTy = dyn_cast<DICompositeType>(currentDITy);
        if(DICTy){
            
            unsigned tag = DICTy->getTag();

            //DW_TAG_array_type is 1
            if(tag == 1){
                structDebugInfoMap[Tyhash] = currentDITy;
                Ctx->Global_Literal_Struct_Map[Tyhash] = "Array";

                currentDITy = DICTy->getBaseType();

                DIDerivedType *DIDTy = dyn_cast<DIDerivedType>(currentDITy);
                if(DIDTy){
                    currentDITy = DIDTy->getBaseType();
                }

                DICTy = dyn_cast<DICompositeType>(currentDITy);
                if(!DICTy)
                    return;
                        
                tag = DICTy->getTag();
                if(tag == 19){

                    StringRef typeName = DICTy->getName();
                    if(typeName.size() != 0)
                        ArrayBaseDebugTypeMap[Tyhash] = typeName.str();
                    return;
                }
                return;
            }

            //DW_TAG_structure_type is 19, only record this debuginfo
            if(tag == 19){
                structDebugInfoMap[Tyhash] = currentDITy;
                StringRef typeName = DICTy->getName();
                if(typeName.size() != 0)
                    Ctx->Global_Literal_Struct_Map[Tyhash] = typeName.str();
                return;
            }

            break;
        }

        DIBasicType *DIBTy = dyn_cast<DIBasicType>(currentDITy);
        if(DIBTy){
            break;
        }

        DISubroutineType *DISRTy = dyn_cast<DISubroutineType>(currentDITy);
        if(DISRTy){
            break;
        }

        //TODO: support more other types
    }

    return;
}



void TypeBuilderPass::matchStructTypes(Type *identifiedTy, User *U){

    if(!identifiedTy || !U)
        return;
    
    Type *UTy = U->getType();

    deque<Type*> Ty1_queue;
    deque<User*> U2_queue;
	deque<Type*> Ty2_queue;
	Ty1_queue.push_back(identifiedTy);
    U2_queue.push_back(U);
	Ty2_queue.push_back(UTy);

	while (!(Ty1_queue.empty() || U2_queue.empty())){

		Type* type1 = Ty1_queue.front();
        User* u2 = U2_queue.front();
		Type* type2 = u2->getType();
		Ty1_queue.pop_front();
		U2_queue.pop_front();
    
        if(type1 == type2){
			continue;
		}
		
		if(typeHash(type1) == typeHash(type2)){
			continue;
		}

        if(type1->isPointerTy() && type2->isPointerTy()){
            continue;
        }

        if(type1->isFunctionTy() && type2->isFunctionTy()){
			continue;
		}

        if(type1->isIntegerTy() && type2->isIntegerTy()){

			IntegerType* inty1 = dyn_cast<IntegerType>(type1);
			IntegerType* inty2 = dyn_cast<IntegerType>(type2);
			unsigned bitwidth1 = inty1->getBitWidth();
			unsigned bitwidth2 = inty2->getBitWidth();

			//This will not happen for the type hash is different
			if(bitwidth1 == bitwidth2)
				continue;

			LLVMContext &C = type1->getContext();
            IntegerType* generated_int = IntegerType::get(C,bitwidth2);

            int times = bitwidth1/bitwidth2;
            for(int i = 0; i < times; i++){
				Ty1_queue.push_front(generated_int);
			}
            U2_queue.push_front(u2);
			
			continue;
		}

		if(type2->isStructTy() ){
            
            //We get the name of type2
            StringRef type2_structname = type2->getStructName();
            if(type2_structname.size() != 0)
                continue;
            
            if(type1->isStructTy()){
                
                //type2 has no name, find it
                if(Ctx->Global_Literal_Struct_Map.count(typeHash(type2)) == 0){
                    StringRef type1_structname = type1->getStructName();
                    string parsed_name = parseIdentifiedStructName(type1_structname);
                    if(parsed_name.size() != 0){
                        Ctx->Global_Literal_Struct_Map[typeHash(type2)] = parsed_name;
                    }
                }

                StringRef type1_structname = type1->getStructName();
                if(type1_structname.contains("union.")){
                    //Once we meet a union, stop further analysis 
                    Ctx->Globa_Union_Set.insert(typeHash(type2));
                    continue;
                }

                bool userqueue_updatr = updateUserQueue(u2, U2_queue);
                if(userqueue_updatr){
                    updateTypeQueue(type1, Ty1_queue);
                }
                continue;
            }
            else if (type1->isArrayTy()){

                //We need to mark this case
                if(Ctx->Global_Literal_Struct_Map.count(typeHash(type2)) == 0){
                    Ctx->Global_Literal_Struct_Map[typeHash(type2)] = "Array";
                }

                bool userqueue_updatr = updateUserQueue(u2, U2_queue);
                if(userqueue_updatr == false)
                    continue;

                ArrayType* arrty = dyn_cast<ArrayType>(type1);
                unsigned subnum = arrty->getNumElements();
                Type* subtype = arrty->getElementType();
                if(subnum > 0){
                    for(auto it = 0; it < subnum; it++){
                        Ty1_queue.push_front(subtype);
                    }
                }
                else{
                    //dynamic array
                }

                continue;
            }
            continue;
		}

        if(type2->isArrayTy()){

            Type* subtype2 = type2->getArrayElementType();
            unsigned subnum2 = type2->getArrayNumElements();


            if(type1->isArrayTy()){
                
                Type* subtype1 = type1->getArrayElementType();
                unsigned subnum1 = type1->getArrayNumElements();

                if(subnum1 == 0 || subnum2 == 0){
                    continue;
                }

                if((subnum2 == subnum1) || (subnum1 == 0)){
                    Ty1_queue.push_front(subtype1);
                    Value *O = *(u2->op_begin());
                    User *OU = dyn_cast<User>(O);
                    U2_queue.push_front(OU);
                }

                else{
                    //OP<<"array size is not equal!\n";
                    if(subtype2->getTypeID() == type1->getTypeID()){
                        //continue;
                    }
                }
            }
            else{
                //Here type1 usually is a single value
                updateQueues(type1, type2, Ty1_queue);

            }


            continue;
        }

        OP<<"Unexpected case!\n";

    }

    if(Ty1_queue.size() != U2_queue.size()){
        OP<<"WARNING: matchStructTypes problem!\n";
    }

}

void TypeBuilderPass::checkLayeredDebugInfo(GlobalVariable *GV){

    Constant *Ini = GV->getInitializer();
    list<User *>LU;
	LU.push_back(Ini);

	//maybe should consider deadloop
	set<User *> PB; //Global value set to avoid loop
	PB.clear();

	//should consider global struct array
	while (!LU.empty()) {
		User *U = LU.front();
		LU.pop_front();

		if (PB.find(U) != PB.end()){
			continue;
		}
		PB.insert(U);


        Type *ITy = U->getType();
        size_t PreTyhash = typeHash(ITy);

#ifdef DEBUG_PRINT
        OP<<"\n\nU: "<<*U<<"\n";
        OP<<"UTY: "<<*ITy<<"\n";
#endif

        if(ITy->isArrayTy()){
            Type* subtype = ITy->getArrayElementType();
        }

        if(Ctx->Global_Literal_Struct_Map.count(PreTyhash)){
            string PreLayerName = Ctx->Global_Literal_Struct_Map[PreTyhash];
            
            if(identifiedStructType.count(PreLayerName)){
                matchStructTypes(identifiedStructType[PreLayerName], U);
                continue;
            }

            else if(PreLayerName == "Array"){
                if(ITy->isStructTy() || ITy->isArrayTy()){
                    //this is an array in source, but struct in llvm bc
                    for (auto oi = U->op_begin(), oe = U->op_end(); oi != oe; ++oi) {
                        Value *O = *oi;
                        Type *OTy = O->getType();
                        UndefValue *UV = dyn_cast<UndefValue>(O);
                        if(UV){
                            //OP<<"UndefValue\n";
                            continue;
                        }

                        if(ArrayBaseDebugTypeMap.count(PreTyhash)){
                            string ArrayEleTypeName = ArrayBaseDebugTypeMap[PreTyhash];
                            if(identifiedStructType.count(ArrayEleTypeName) == 0){
                                continue;
                            }

                            if(OTy->isArrayTy())
                                continue;

                            User *OU = dyn_cast<User>(O);
                            matchStructTypes(identifiedStructType[ArrayEleTypeName], OU);
                            continue;
                        }
                        else{
                            //OP<<"not found in ArrayEleTypeName\n";
                        }
                    }
                }

                continue;
            }
            
            else{
                //Usually this is the case that the struct type definition
                // does not exist in current module
                
            }
        }


    }
}

bool TypeBuilderPass::doInitialization(Module *M) {

    collectLiteralStruct(M);
    Int8PtrTy[M] = Type::getInt8PtrTy(M->getContext());

    for (Module::global_iterator gi = M->global_begin(); 
			gi != M->global_end(); ++gi) {

		GlobalVariable* GV = &*gi;

        //Init global variable map for dataflow analysis
        Ctx->Global_Unique_GV_Map[GV->getGUID()].insert(GV);

		if(!checkValidGV(GV))
            continue;
		
#ifdef TEST_ONE_INIT_GLOBAL
		if(GV->getName() != TEST_ONE_INIT_GLOBAL)
			continue;
#endif

        Type* GType = GV->getType();
        Type* GPType = GType->getPointerElementType();
        size_t Tyhash = typeHash(GPType);

        if(GPType->isArrayTy()){

            Type* innerTy = GPType->getArrayElementType();
            if(innerTy->isStructTy()){
                StructType* innerSTy = dyn_cast<StructType>(innerTy);
                if(innerSTy->isLiteral()){
                    checkGlobalDebugInfo(GV, Tyhash);
                    targetGVSet.insert(GV);
                }
            }

            continue;
        }

        if(GPType->isStructTy()){
        
            StructType* GPSType = dyn_cast<StructType>(GPType);
            if(GPSType->isLiteral()){
                Ctx->num_typebuilder_haveNoStructName++;
                checkGlobalDebugInfo(GV, Tyhash);
                targetGVSet.insert(GV);
			}
            else{
                Ctx->num_typebuilder_haveStructName++;
            }
            continue;
        }
	}

    //Init some global info here
    for (Function &F : *M) {

        Ctx->Global_Unique_GV_Map[F.getGUID()].insert(&F);

        if(F.hasAddressTaken()){
            Ctx->Global_AddressTaken_Func_Set.insert(&F);

            size_t funchash = funcInfoHash(&F);
            if(Ctx->Global_Unique_Func_Map.count(funchash) == 0)
                Ctx->Global_Unique_Func_Map[funchash] = &F;
        }

		if (F.isDeclaration())
			continue;

        // Collect global function definitions.
		if ((F.hasExternalLinkage() && !F.empty()) || F.hasAddressTaken()) {

			StringRef FName = F.getName();
            size_t funchash = funcInfoHash(&F);
            Ctx->GlobalFuncs[FName.str()].insert(funchash);
            Ctx->Global_Unique_Func_Map[funchash] = &F;
		}
    }

	return false;
}

static int globaltag = 0;

bool TypeBuilderPass::doFinalization(Module *M) {
	return false;
}

bool TypeBuilderPass::doModulePass(Module *M) {

    //The struct type tabel in a single module has no redundant info
    vector <StructType*> identifiedStructTys = M->getIdentifiedStructTypes();
    for(auto it = identifiedStructTys.begin(); it != identifiedStructTys.end(); it++){
        StructType* STy = *it;
        StringRef STy_name = STy->getName();
        
        if(STy_name.size() == 0)
            continue;

        string parsed_STy_name = parseIdentifiedStructName(STy_name);
        identifiedStructType[parsed_STy_name] = STy;
    }

    for (Module::global_iterator gi = M->global_begin(); 
			gi != M->global_end(); ++gi) {
		GlobalVariable* GV = &*gi;
    
        if(!checkValidGV(GV))
            continue;
        
#ifdef TEST_ONE_INIT_GLOBAL
		if(GV->getName() != TEST_ONE_INIT_GLOBAL)
			continue;
#endif

        //Only focus on target set
        if(targetGVSet.count(GV) == 0)
            continue;

        checkLayeredDebugInfo(GV);
    }

    identifiedStructType.clear();


    return false;
}

void TypeBuilderPass::collectLiteralStruct(Module* M){

    if(!M)
        return;

    vector<StructType*> structTy_vec = M->getIdentifiedStructTypes();
    for(StructType* STy : structTy_vec){

        if(STy->isOpaque())
            continue;

        StringRef struct_name = STy->getName();
        Ctx->Global_Literal_Struct_Name_Map[struct_name.str()] = STy;
    }
}