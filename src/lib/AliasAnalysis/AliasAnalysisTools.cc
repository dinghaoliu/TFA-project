#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LegacyPassManager.h>

#include "AliasAnalysis.h"

//Merge n1 into n2
void mergeNode(AliasNode* n1, AliasNode* n2, AliasContext *aliasCtx){

    if(n1 == n2)    
        return;

    //First merge node values
    for(auto it = n1->aliasclass.begin(); it != n1->aliasclass.end(); it++){
        Value* v = *it;
        n2->insert(v);
        aliasCtx->NodeMap[v] = n2;
    }
    n1->aliasclass.clear();

    //Then change edges
    if(aliasCtx->ToNodeMap.count(n1)){
        AliasNode* n1_toNode = aliasCtx->ToNodeMap[n1];

        if(aliasCtx->ToNodeMap.count(n2)){
            AliasNode* n2_toNode = aliasCtx->ToNodeMap[n2];

            //n1 and n2 point to the same node
            //This cannot happen because one node only has one pre and post node in field-sensitive analysis,
            //but it could occur in field-insensitive analysis
            if(n1_toNode == n2_toNode){
                //do nothing here
            }
            else{
                aliasCtx->ToNodeMap.erase(n1);
                aliasCtx->ToNodeMap.erase(n2);
                aliasCtx->FromNodeMap.erase(n1_toNode);
                aliasCtx->FromNodeMap.erase(n2_toNode);
                mergeNode(n1_toNode, n2_toNode, aliasCtx);
                aliasCtx->ToNodeMap[n2] = n2_toNode;
                aliasCtx->FromNodeMap[n2_toNode] = n2;
            }
        }

        else{
            aliasCtx->ToNodeMap.erase(n1);
            aliasCtx->ToNodeMap[n2] = n1_toNode;
            aliasCtx->FromNodeMap[n1_toNode] = n2;
        }
    }

    //Check which node points to n1
    if(aliasCtx->FromNodeMap.count(n1)){
        AliasNode* n1_fromNode = aliasCtx->FromNodeMap[n1];

        if(aliasCtx->FromNodeMap.count(n2)){
            AliasNode* n2_fromNode = aliasCtx->FromNodeMap[n2];

            if(n1_fromNode == n2_fromNode){
                //do nothing here
                //OP<<"WARNING IN MERGE NODE!\n";
                //sleep(1);
            }
            else{
                aliasCtx->FromNodeMap.erase(n1);
                aliasCtx->FromNodeMap.erase(n2);
                aliasCtx->ToNodeMap.erase(n1_fromNode);
                aliasCtx->ToNodeMap.erase(n2_fromNode);
                mergeNode(n1_fromNode, n2_fromNode, aliasCtx);
                aliasCtx->FromNodeMap[n2] = n2_fromNode;
                aliasCtx->ToNodeMap[n2_fromNode] = n2;
            }
        }

        //n2 has no pre node
        else{
            aliasCtx->FromNodeMap.erase(n1);
            aliasCtx->FromNodeMap[n2] = n1_fromNode;
            aliasCtx->ToNodeMap[n1_fromNode] = n2;
        }
    }
}


AliasNode* getNode(Value *V, AliasContext *aliasCtx){

    //Use a map to speed up query
    if(aliasCtx->NodeMap.count(V))
        return aliasCtx->NodeMap[V];

    return NULL;
}


//Filter instructions we do not need to analysis
//Return true if current inst does not need analysis
bool isUselessInst(Instruction* I, GlobalContext *Ctx){

    //Filter debug functions
    CallInst *CAI = dyn_cast<CallInst>(I);
    if(CAI){
        StringRef FName = getCalledFuncName(CAI);
        if(Ctx->DebugFuncs.count(FName.str())){
            return true;
        }
    }

    return false;
}

//merge S2 into S1
void valueSetMerge(set<Value*> &S1, set<Value*> &S2){
	for(auto v : S2)
		S1.insert(v);
}

void funcSetMerge(FuncSet &S1, FuncSet &S2){
    for(auto v : S2){
		S1.insert(v);
    }
}

unsigned getArgIndex(Function* F, Argument *Arg){

    unsigned index = 0;
    for(auto it = F->arg_begin(); it != F->arg_end(); it++){
        Value* F_arg = it;
        if(Arg == F_arg){
            break;
        }
        index++;
    }

    return index;
}

unsigned getMin(unsigned n1, unsigned n2){
    if(n1 < n2)
        return n1;
    else
        return n2;
}

void getGlobalFuncs(Function *F, FuncSet &FSet, GlobalContext *Ctx){

    StringRef FName = F->getName();
    if(Ctx->GlobalFuncs.count(FName.str())){
        //OP<<"here\n";
        set<size_t> hashSet = Ctx->GlobalFuncs[FName.str()];
        for(auto it = hashSet.begin(); it != hashSet.end(); it++){
            Function *f = Ctx->Global_Unique_Func_Map[*it];
            //OP<<"f: "<<*f<<"\n";
			FSet.insert(f);
        }
    }
}

string getGlobalMacroSource(GlobalVariable* GV){

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
    OP<<"fn_str: "<<fn_str<<"\n";

    unsigned lineno = DIGV->getLine();
    string source_line = getSourceLine(fn_str, lineno);

    return source_line;
}

bool checkNodeConnectivity(AliasNode* node1, AliasNode* node2, AliasContext *aliasCtx){

    if(!node1 || !node2)
        return false;

	list<AliasNode *>LN;
	LN.push_back(node1);
	set<AliasNode *> PN; //Global value set to avoid loop
	PN.clear();

	while (!LN.empty()) {
		AliasNode *CN = LN.front();
		LN.pop_front();

		if (PN.find(CN) != PN.end()){
			continue;
		}
		PN.insert(CN);

        if(CN == node2)
            return true;

		if(aliasCtx->ToNodeMap.count(CN)){
			LN.push_back(aliasCtx->ToNodeMap[CN]);
		}

		if(aliasCtx->FromNodeMap.count(CN)){
			LN.push_back(aliasCtx->FromNodeMap[CN]);
		}
	}

    return false;
}