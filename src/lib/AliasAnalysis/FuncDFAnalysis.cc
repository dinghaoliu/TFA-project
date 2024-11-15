#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LegacyPassManager.h>

#include "AliasAnalysis.h"

using namespace llvm;

//TODO: Check whether an address-taken function is only
//used in initializing layered icalls or limited icalls.
//If so, we could eliminate it from icalls with one layer type.

//Note: we do NOT need to know if an address-taken function
//is used to init which specific field, this will be done in type analysis

//New: this analysis is not sound, because we cannot know whether an icall
//is onelayer with soundness guaranteed, just disbale it.

//Run it after func alias analysis
void FuncDFHandler(GlobalContext *Ctx){

    OP<<"\nstart FuncDFHandler\n";
    
    map<string, FuncSet> FuncBatchMap;
    map<string, CallInstSet> FuncInvolvedIcallMap;
    CallGraphPass CGPass(Ctx);

    omp_lock_t lock;
	omp_init_lock(&lock);
    
    for(auto i = Ctx->ICallees.begin(); i!= Ctx->ICallees.end(); i++){
        
        CallInst* cai = i->first;
        FuncSet fset = i->second;
        for(auto f : fset){
            Ctx->Global_Func_Alias_Frequency_Map[f].insert(cai);
        }
    }

    size_t success_num = 0;

    for(auto it = Ctx->Global_AddressTaken_Func_Set.begin(); 
        it != Ctx->Global_AddressTaken_Func_Set.end(); it++){

        Function* F = *it;
        FuncBatchMap[F->getName().str()].insert(F);
    }

    for(auto it = FuncBatchMap.begin(); it != FuncBatchMap.end(); it++){
        string fname = it->first;
        FuncSet fset = it->second;
        CallInstSet caiset;
        

        bool ret = CGPass.FuncSetHandler(fset, caiset);
        if(ret == false){
            continue;
        }
        
        OP<<"success\n";
        for(auto cai : caiset){
            OP<<"cai: "<<*cai<<"\n";
        }
        FuncInvolvedIcallMap[fname] = caiset;

        success_num++;
    }

    OP<<"success_num: "<<success_num<<"\n";

    //Update Global Info
    OP<<"\nUpdate global ICallees info\n";
    vector<pair<CallInst*, FuncSet>> UpdateVec;
	UpdateVec.clear();
	for(auto i = Ctx->ICallees.begin(); i!= Ctx->ICallees.end(); i++){
        if(Ctx->Global_MLTA_Reualt_Map[i->first]){
            if(Ctx->Global_MLTA_Reualt_Map[i->first] == OneLayer){
                UpdateVec.push_back(make_pair(i->first, i->second));
            }
        }
	}
	size_t ICall_num = UpdateVec.size();
    static size_t update_num = 0;
    OP<<"ICall_num: "<<ICall_num<<"\n";


#ifdef ENAMBE_OMP
	#pragma omp parallel for schedule(dynamic,1)
#endif
	for(auto i = 0; i < ICall_num; i++){

        CallInst* ICall = UpdateVec[i].first; //icalls with one-layer
        FuncSet ICall_Targets = UpdateVec[i].second; //icall targets

#ifdef ENAMBE_OMP
        omp_set_lock(&lock);
        OP<< "\n"<< update_num++<< " Update icall: "<<*ICall<<"\n";
        omp_unset_lock(&lock);
#else
        //OP<< "\n"<< update_num++<< " Update icall: "<<*ICall<<"\n";
#endif

        for(Function* f : ICall_Targets){
            string fname = f->getName().str();
            if(FuncInvolvedIcallMap.count(fname)){
                CallInstSet involved_icall_set = FuncInvolvedIcallMap[fname];
                if(involved_icall_set.count(ICall) == 0){
#ifdef ENAMBE_OMP
                    omp_set_lock(&lock);
                    Ctx->ICallees[ICall].erase(f);
                    omp_unset_lock(&lock);
#else
                    Ctx->ICallees[ICall].erase(f);
#endif
                }
            }
        }
    }

    update_num = 0;

    //Update analysis results
	Ctx->icallTargets = 0;
	for(auto i = Ctx->ICallees.begin(); i!= Ctx->ICallees.end(); i++){
		FuncSet fset = i->second;
		Ctx->icallTargets+=fset.size();
	}

    omp_destroy_lock(&lock);
}

