#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/LegacyPassManager.h>

#include "AliasAnalysis.h"

using namespace llvm;

//#define TEST_ONE_FUNC "kdb_getchar"
//#define PRINT_ALIAS_RESULT
#define ENAMBE_OMP

//Used for test
void ModuleAliasAnalysis(GlobalContext *Ctx){

	if(!Ctx)
		return;
	
	ModuleList modules = Ctx->Modules;
	unsigned total_modules = modules.size();
	for (int it = 0; it < total_modules; ++it) {

		Module *M = modules[it].first;
		for (Module::iterator f = M->begin(), fe = M->end(); 
			f != fe; ++f) {

			Function *F = &*f;
			AliasContext* aliasCtx = new AliasContext();
			FunctionAliasAnalysis(F, aliasCtx, Ctx);
		}	
	}
}

//Used for test
void FunctionAliasAnalysis(Function* F, AliasContext *aliasCtx, GlobalContext *Ctx){

	if(!F)
		return;

#ifdef TEST_ONE_FUNC
	if(F->getName()!=TEST_ONE_FUNC){
		return;
	}
#endif

	for (inst_iterator i = inst_begin(F), e = inst_end(F); i != e; ++i) {
		
		Instruction *iInst = dyn_cast<Instruction>(&*i);
		HandleInst(iInst, aliasCtx, Ctx);
	}

}

//Start icall alias analysis here
void ICallAliasAnalysis(GlobalContext *Ctx){

    //Do alias analysis for each icall
	//Here we could enable concurrency to speed up

	OP<<"\n Start ICall Alias Analysis\n";
	
	vector<pair<CallInst*, FuncSet>> TargetVec;
	TargetVec.clear();
	for(auto i = Ctx->ICallees.begin(); i!= Ctx->ICallees.end(); i++){
		TargetVec.push_back(make_pair(i->first, i->second));
	}
	size_t ICall_num = TargetVec.size();

	omp_lock_t lock;
	omp_init_lock(&lock);

#ifdef ENAMBE_OMP
	#pragma omp parallel for schedule(dynamic,1)
#endif
	for(auto i = 0; i < ICall_num; i++){
        //OP<<"begin\n";
        CallInst* cai = TargetVec[i].first;
        FuncSet fset = TargetVec[i].second;
		
		TypeAnalyzeResult cai_analysis_type = Ctx->Global_MLTA_Reualt_Map[cai];
		if(cai_analysis_type == NoTwoLayerInfo || cai_analysis_type == MissingBaseType || cai_analysis_type == VTable){
#ifdef ENAMBE_OMP
			omp_set_lock(&lock);
			Ctx->Global_Alias_Results_Map[cai] = 0;
			omp_unset_lock(&lock);
#else 
			Ctx->Global_Alias_Results_Map[cai] = 0;
#endif
			continue;
		}

		AliasContext* aliasCtx = new AliasContext();
		FuncSet aliased_fset;
		aliased_fset.clear();
        HandleICall(cai, aliasCtx, Ctx, aliased_fset, &lock);


		if(aliasCtx->Is_Analyze_Success == false){
#ifdef ENAMBE_OMP
			omp_set_lock(&lock);
			Ctx->Global_Alias_Results_Map[cai] = 0;
			Ctx->failure_reasons[aliasCtx->failreason]++;
			Ctx->Global_ICall_Alias_Result_Map[cai] = aliasCtx->failreason;
			omp_unset_lock(&lock);
#else
			Ctx->Global_Alias_Results_Map[cai] = 0;
			Ctx->failure_reasons[aliasCtx->failreason]++;
			Ctx->Global_ICall_Alias_Result_Map[cai] = aliasCtx->failreason;
#endif
			delete aliasCtx;
			continue;
		}

#ifdef ENAMBE_OMP
		omp_set_lock(&lock);
#endif

		Ctx->icall_support_dataflow_Number++;

		OP<<"aliased_fset size: "<<aliased_fset.size()<<"\n";

		//Merge the result of type analysis and alias analysis
		FuncSet result_set;
		result_set.clear();
		for(auto f1 : aliased_fset){
			if(fset.count(f1)){
				result_set.insert(f1);
			}
		}

		//Update analysis results
		Ctx->ICallees[cai] = result_set;
		Ctx->Global_Alias_Results_Map[cai] = 1;

		delete aliasCtx;
#ifdef ENAMBE_OMP
		omp_unset_lock(&lock);
#endif

    }

	omp_destroy_lock(&lock);

	//Update analysis results
	Ctx->icallTargets = 0;
	for(auto i = Ctx->ICallees.begin(); i!= Ctx->ICallees.end(); i++){
		FuncSet fset = i->second;
		Ctx->icallTargets+=fset.size();
	}

}

void HandleICall(CallInst* icall, AliasContext *aliasCtx, 
	GlobalContext *Ctx, FuncSet &fset, omp_lock_t *lock){

	static size_t num = 0;

    if(icall == NULL)
        return;
    
    Function* icaller = icall->getFunction();

#ifdef TEST_ONE_FUNC
	if(icaller->getName()!=TEST_ONE_FUNC){
		aliasCtx->Is_Analyze_Success = false;
		return;
	}
#endif

#ifdef ENAMBE_OMP
	omp_set_lock(lock);
	OP<< "\n"<< num++<< " Top func: "<<icaller->getName()<<"\n";
	OP<<"---icall: "<<*icall<<"\n";
	omp_unset_lock(lock);
#else
	OP<< "\n"<< num++<< " Top func: "<<icaller->getName()<<"\n";
	OP<<"---icall: "<<*icall<<"\n";
#endif

	//Check the alias results of icall's caller function
    for (inst_iterator i = inst_begin(icaller), ei = inst_end(icaller); i != ei; ++i) {
        
        Instruction *iInst = dyn_cast<Instruction>(&*i);
        HandleInst(iInst, aliasCtx, Ctx);
    }

	aliasCtx->AnalyzedFuncSet.insert(icaller);

	//Extracted target-related values
	Value* icall_V = icall->getCalledOperand();
	set<Value*> targetValueSet;
	getClusterValues(icall_V, targetValueSet, aliasCtx);

	list<Value *>AnalysisList;
	set<Value *> AnalyzedList; //Global value set to avoid loop
	AnalysisList.clear();
	AnalyzedList.clear();

	//OP<<"\naliased values: \n";
	for(auto it = targetValueSet.begin(); it != targetValueSet.end(); it++){
		Value* v = *it;
		//OP<<"v: "<<*v<<"\n";
		AnalysisList.push_back(v);
	}

	while (!AnalysisList.empty()) {
		Value *CV = AnalysisList.front();
		AnalysisList.pop_front();

		if (AnalyzedList.find(CV) != AnalyzedList.end()){
			continue;
		}
		AnalyzedList.insert(CV);

		if(aliasCtx->Is_Analyze_Success == false)
			break;

		//Too complex, stop analysis
		if(AnalysisList.size() > MAX_ANALYSIS_NUM){
			aliasCtx->Is_Analyze_Success = false;
			aliasCtx->failreason = exceed_max;
			break;
		}
		//OP<<"---inter analysis begin\n";
		interCaseHandler(CV, AnalysisList, AnalyzedList, aliasCtx, Ctx);
		//OP<<"---inter analysis fini\n";
	}

	if(aliasCtx->Is_Analyze_Success == false){
		return;
	}

	getClusterValues(icall_V, targetValueSet, aliasCtx);
#ifdef PRINT_ALIAS_RESULT
	OP<<"\n aliased v: \n";
#endif
	for(auto it = targetValueSet.begin(); it != targetValueSet.end(); it++){
		Value* aliased_v = *it;
		if(Function *F = dyn_cast<Function>(aliased_v)){
#ifdef PRINT_ALIAS_RESULT
			OP<<"func: "<<F->getName()<<"\n";
#endif		

			FuncSet SingleFSet;
			SingleFSet.clear();
			getGlobalFuncs(F, SingleFSet, Ctx);
			funcSetMerge(fset, SingleFSet);
		}
	}
}