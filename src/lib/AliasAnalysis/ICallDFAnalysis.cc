#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LegacyPassManager.h>

#include "AliasAnalysis.h"

using namespace llvm;

//Further trace if one layer icall has two layer info
void oneLayerHandler(GlobalContext *Ctx){

    OP<<"\nstart oneLayerHandler\n";

    CallGraphPass CGPass(Ctx);

    size_t success_num = 0;
    for(auto it = Ctx->Global_MLTA_Reualt_Map.begin(); it != Ctx->Global_MLTA_Reualt_Map.end(); it++){
		CallInst* cai = it->first;
        TypeAnalyzeResult type_result = it->second;
		
        //Alias analysis has succeed
        if(Ctx->Global_Alias_Results_Map.count(cai))
            if(Ctx->Global_Alias_Results_Map[cai] == 1)
                continue;
        
        //Only target one-layer icall
        if(type_result == OneLayer){
			CallInst* CI = it->first;
            FuncSet FS;
            
			if(CGPass.oneLayerChecker(CI, FS)){
                success_num++;
            }
		}
	}

    //OP<<"success one layer num: "<<success_num<<"\n";

    //Update analysis results
	Ctx->icallTargets = 0;
	for(auto i = Ctx->ICallees.begin(); i!= Ctx->ICallees.end(); i++){
		FuncSet fset = i->second;
		Ctx->icallTargets+=fset.size();
	}
}