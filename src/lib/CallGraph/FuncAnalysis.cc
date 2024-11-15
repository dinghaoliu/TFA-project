#include "CallGraph.h"

string getGlobalMacrosource(GlobalVariable* GV){

    if(!GV)
        return "";
    
    MDNode *N = GV->getMetadata("dbg");
    if (!N)
        return "";
    
    DIGlobalVariableExpression *DLGVE = dyn_cast<DIGlobalVariableExpression>(N);
    if(!DLGVE)
        return "";

    DIGlobalVariable* DIGV = DLGVE->getVariable();
    if(!DIGV)
        return "";
    
    string fn_str = getFileName(NULL, NULL, DIGV);
    unsigned lineno = DIGV->getLine();
    string source_line = getSourceLine(fn_str, lineno);

    return source_line;
}

bool CallGraphPass::FuncSetHandler(FuncSet FSet, CallInstSet &callset){

    bool batch_result = true;
    set<size_t> hashSet;
    hashSet.clear();
    for(Function *F : FSet){
        hashSet.clear();

        batch_result = FindNextLayer(F, hashSet, callset);
        if(batch_result == false)
            return false; 
    }

    //Here all FuncHandler must have succeed

    return true;
}

bool CallGraphPass::FindNextLayer(Function* F, set<size_t> &HashSet, CallInstSet &callset){

    OP<<"\nCurrent func: "<<F->getName()<<"\n";

    map<Value*, Value*> PreMap; //Record user info
    list<Value *>LV;
    set<Value *> GV;
	for(User *U : F->users()){

		LV.push_back(U);
        PreMap[U] = F;
	}

    while (!LV.empty()) {
        Value *TV = LV.front();
        Value *PreV = PreMap[TV];
        
        LV.pop_front();

        if (GV.find(TV) != GV.end()){
            continue;
        }
        GV.insert(TV);
        
        CallInst *CAI = dyn_cast<CallInst>(TV);
        if(CAI){
            
            //Ignore llvm func
            StringRef FName = getCalledFuncName(CAI);
            if(Ctx->DebugFuncs.count(FName.str())){
                continue;
            }

            //Direct call
            if(FName == F->getName())
                continue;
            
            //Indirect call
            if (CAI->isIndirectCall()){
                Value *CO = CAI->getCalledOperand();
                if(CO == PreV){
                    callset.insert(CAI);
                    continue;
                }
            }

            if(!Ctx->Callees.count(CAI))
                continue;
        
            //Use as a func arg
            unsigned argnum = CAI->arg_size();
            int arg_index = -1;
            for(unsigned i = 0; i < argnum; i++){
                Value* arg = CAI->getArgOperand(i);
                if(arg == PreV){
                    arg_index = i;
                    break;
                }
            }

            if(arg_index == -1)
                return false;

            for(Function *f : Ctx->Callees[CAI]){
                for (Function::arg_iterator FI = f->arg_begin(), 
                    FE = f->arg_end(); FI != FE; ++FI) {
                    unsigned argno = FI->getArgNo();
                    if(argno == arg_index)
                        LV.push_back(FI);
                }
            }
            continue;
        }

        //Used in initing global variables
        GlobalVariable* GV = dyn_cast<GlobalVariable>(TV);
        if(GV){

            if(GV->getName().contains("__UNIQUE_ID___addressable")){

                //Find the macro
                string source = getGlobalMacrosource(GV);
                if(source.size() != 0){
                    for(string safe_macro : Ctx->SafeMacros){
                        if(checkStringContainSubString(source, safe_macro)){
                            //Aliased with a safe macro, stop further analysis
                            continue;
                        }
                    }
                }
            }

            if(GV->getName() == "llvm.used" || GV->getName() == "llvm.compiler.used"
                || GV->getName().contains("__start_set")){
                return false;
            }

            if(!GV->hasInitializer())
                continue;

            //Check the field that F initialized
            Constant *Ini = GV->getInitializer();
            if(!Ini)
                continue;
            
            //If f is used to init GV, we should have analyzed it in InitinGlobal.cc
            if(Func_Init_Map.count(GV)){
                map<Function*, set<size_t>> Init_List = Func_Init_Map[GV];
                for(size_t hash : Init_List[F]){
                    HashSet.insert(hash);
                }
            }
            else{
                return false;
            }

            continue;
        }

        //F is stored into sth
        StoreInst *SI = dyn_cast<StoreInst>(TV);
        if(SI){
            Value *PO = SI->getPointerOperand();
            Value *VO = SI->getValueOperand();

            if (isa<ConstantData>(VO))
                continue;

            //Check prelayer info
            set<Value*> VisitedSet;
            list<CompositeType> TyList;
            if (nextLayerBaseType_new(PO, TyList, VisitedSet, Precise_Mode)) {
                continue;
            }

            LV.push_back(PO);
            continue;
        }

        AllocaInst* ALI = dyn_cast<AllocaInst>(TV);
        llvm::Argument *Arg = dyn_cast<llvm::Argument>(TV);
        LoadInst* LI = dyn_cast<LoadInst>(TV);
        BitCastOperator *CastO = dyn_cast<BitCastOperator>(TV);
        ConstantAggregate* CA = dyn_cast<ConstantAggregate>(TV);
        if(ALI || Arg || LI || CastO || CA){
            for(User *u : TV->users()){
                LV.push_back(u);
                PreMap[u] = TV;
            }
            continue;
        }

        ICmpInst* CMPI = dyn_cast<ICmpInst>(TV);
        if(CMPI){
            continue;
        }

        //Unkown cases
        //OP<<"Unsupported inst: "<<*TV<<"\n";
        return false;

    }

    return true;
}