#include "CallGraph.h"

using namespace llvm;

BasicBlock* CallGraphPass::getParentBlock(Value* V){

    if(!V)
        return NULL;
    
    if(Instruction* I = dyn_cast<Instruction>(V)){
        return I->getParent();
    }

    if(llvm::Argument* Arg = dyn_cast<llvm::Argument>(V)){
        return &(Arg->getParent()->getEntryBlock());
    }

    return NULL;
}

void CallGraphPass::getDebugCall(Function* F){
    
    if(!F)
        return;

    for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
        CallInst* CI = dyn_cast<CallInst>(&*i);
        if(CI){
            Function *CF = CI->getCalledFunction();
            if(!CF)
                continue;

			if(!CF->getName().contains("llvm.dbg.value")){
				continue;
			}

            LLVMDebugCallMap[F].insert(CI);
        }
    }
}

Type* CallGraphPass::getRealType(Value* V){

    BasicBlock* BB = getParentBlock(V);
    if(!BB)
        return NULL;

    set<StringRef> nameSet;
    
    //This loop is time consuming
    for(BasicBlock::iterator i = BB->begin(); i != BB->end(); i++){

        CallInst* CI = dyn_cast<CallInst>(i);
        if(CI){
            
            Function *CF = CI->getCalledFunction();
            if(!CF)
                continue;

			if(!CF->getName().contains("llvm.dbg.value")){
				continue;
			}
            
            Value* cai_first_arg = CI->getArgOperand(0);            

            string arg_name = getValueName(cai_first_arg);
            if(checkStringContainSubString(arg_name, " ")){
                int start_point = arg_name.find(" ");
                string substr = arg_name.substr(start_point+1, arg_name.size()-1);
                arg_name = substr;
            }

            //NOTE: cai_first_arg and V are different pointers even if they are 
            //the same in ir, so here we compare their string directly
            if(arg_name != getValueName(V)){
                continue;
            }

            Value* cai_second_arg = CI->getArgOperand(1);
            
            if(MetadataAsValue* MDAV = dyn_cast<MetadataAsValue>(cai_second_arg)){
                
                Metadata* MD = MDAV->getMetadata();
                if(!MD)
                    continue;
                
                MDNode* N = dyn_cast<MDNode>(MD);
                if(!N)
                    continue;
                
                DILocalVariable *DILV = dyn_cast<DILocalVariable>(N);
                if(!DILV)
                    continue;

                DIType *DITy = DILV->getType();
                if(!DITy)
                    continue;
                
                DIType * currentDITy = DITy;

                while (true) {
                    
                    if(currentDITy == NULL)
                        break;

                    DIDerivedType *DIDTy = dyn_cast<DIDerivedType>(currentDITy);
                    if(DIDTy){
                        currentDITy = DIDTy->getBaseType();
                        continue;
                    }

                    DIBasicType* DIBTy = dyn_cast<DIBasicType>(currentDITy);
                    if(DIBTy){
                        break;
                    }

                    DICompositeType *DICTy = dyn_cast<DICompositeType>(currentDITy);
                    if(DICTy){
                        
                        //Check if the type is a struct type
                        unsigned tag = DICTy->getTag();
                        if(tag != 19){
                            return NULL;
                        }

                        StringRef typeName = DICTy->getName();
                        nameSet.insert(typeName);
                        
                        if(Ctx->Global_Literal_Struct_Name_Map.count("struct." + typeName.str())){
                            return Ctx->Global_Literal_Struct_Name_Map["struct." + typeName.str()];
                        }
                        else{
                            Ctx->Global_missing_type_def_struct_num++;
                        }

                        return NULL;
                    }
                }
            }
        }
    }

    return NULL;
}