#include "Tools.h"

//Used for debug
unsigned getInstLineNo(Instruction *I){

    begin:

    if(!I){
        //OP << "No such Inst\n";
        return 0;
    }
        
    //DILocation *Loc = dyn_cast<DILocation>(N);
    DILocation *Loc = I->getDebugLoc();
    if (!Loc ){
        //OP << "No such DILocation\n";
        auto nextinst = I->getNextNonDebugInstruction();
        I = nextinst;
		//return 0;
        goto begin;
    }

    unsigned Number = Loc->getLine();
    //Loc->getFilename();
    //Loc->getDirectory();

    if(Number < 1){
        //OP << "Number < 1\n";
        auto nextinst = I->getNextNonDebugInstruction();
        I = nextinst;
        goto begin;
    }

    return Number;
}

//Used for debug
unsigned getInstLineNo(Function *F){

    if(!F){
        //OP << "No such Inst\n";
        return 0;
    }
        
    //DILocation *Loc = dyn_cast<DILocation>(N);
    MDNode *N = F->getMetadata("dbg");
    if (!N)
        return 0;

    //DILocation *Loc = F->getDebugLoc();
    DISubprogram *Loc = dyn_cast<DISubprogram>(N);
    if (!Loc ){
		return 0;
    }

    unsigned Number = Loc->getLine();
    //Loc->getFilename();
    //Loc->getDirectory();

    return Number;
}


//Used for debug
std::string getInstFilename(Instruction *I){
    begin:

    if(!I){
        //OP << "No such Inst\n";
        return "";
    }
        
    //DILocation *Loc = dyn_cast<DILocation>(N);
    DILocation *Loc = I->getDebugLoc();
    if (!Loc ){
        //OP << "No such DILocation\n";
        auto nextinst = I->getNextNonDebugInstruction();
        I = nextinst;
		//return 0;
        goto begin;
    }

    string Filename = Loc->getFilename().str();
    //Loc->getFilename();
    //Loc->getDirectory();

    if(Filename.length() == 0){
        //OP << "Number < 1\n";
        auto nextinst = I->getNextNonDebugInstruction();
        I = nextinst;
        goto begin;
    }

    return Filename;
}

//Used for debug
string getBlockName(BasicBlock *bb){
    if(bb == NULL)
        return "NULL block";
    std::string Str;
    raw_string_ostream OS(Str);
    bb->printAsOperand(OS,false);
    return OS.str();
}

//Used for debug
string getValueName(Value* V){
    std::string Str;
    raw_string_ostream OS(Str);
    V->printAsOperand(OS,false);
    return OS.str();
}

//Used for debug
std::string getValueContent(Value* V){
    std::string Str;
    raw_string_ostream OS(Str);
    V->print(OS,false);
    return OS.str();
}

//Used for debug
void printInstMessage(Instruction *inst){
    if(!inst){
        OP << "No such instruction";
        return;
    }
        
    MDNode *N = inst->getMetadata("dbg");

    if (!N)
        return;
    
    DILocation *Loc = dyn_cast<DILocation>(N);
    string SCheckFileName = Loc->getFilename().str();
    unsigned SCheckLineNo = Loc->getLine();
    //OP << "Filename: "<<SCheckFileName<<"\n";
    OP << "LineNo: " << SCheckLineNo<<"\n";

}

//Used for debug
void printBlockMessage(BasicBlock *bb){

    if(!bb){
        OP << "No such block";
        return;
    }
    
    auto begininst = dyn_cast<Instruction>(bb->begin());
    auto endinst = bb->getTerminator();

    OP << "\nBegin at --- ";
    printInstMessage(begininst);
    OP << "End   at --- ";
    printInstMessage(endinst);

    /* for(BasicBlock::iterator i = bb->begin(); 
        i != bb->end(); i++){

        auto midinst = dyn_cast<Instruction>(i);
        printInstMessage(midinst);        
    } */

}

//Used for debug
void printBlockLineNoRange(BasicBlock *bb){
    if(!bb){
        OP << "No such block";
        return;
    }
    
    auto begininst = dyn_cast<Instruction>(bb->begin());
    auto endinst = bb->getTerminator();

    OP << "("<<getInstLineNo(begininst)<<"-"<<getInstLineNo(endinst)<<")";

}

//Used for debug
void printFunctionMessage(Function *F){

    if(!F)
        return;
    
    for(Function::iterator b = F->begin(); 
        b != F->end(); b++){
        
        BasicBlock * bb = &*b;
        OP << "\nCurrent block: block-"<<getBlockName(bb)<<"\n";
        //printBlockMessage(bb);

        OP << "Succ block: \n";
        for (BasicBlock *Succ : successors(bb)) {
			//printBlockMessage(Succ);
            OP << " block-"<<getBlockName(Succ)<<" ";
		}

        OP<< "\n";
    }
}

//Check if there exits common element of two sets
bool findCommonOfSet(set<Value *> setA, set<Value *> setB){
    if(setA.empty() || setB.empty())
        return false;
    
    bool foundtag = false;
    for(auto i = setA.begin(); i != setA.end(); i++){
        Value * vi = *i;
        for(auto j = setB.begin(); j != setB.end(); j++){
            Value * vj = *j;
            if(vi == vj){
                foundtag = true;
                return foundtag;
            }
        }
    }

    return foundtag;
}

bool findCommonOfSet(set<std::string> setA, set<std::string> setB){
    if(setA.empty() || setB.empty())
        return false;
    
    bool foundtag = false;
    for(auto i = setA.begin(); i != setA.end(); i++){
        string vi = *i;
        for(auto j = setB.begin(); j != setB.end(); j++){
            string vj = *j;
            if(vi == vj){
                foundtag = true;
                return foundtag;
            }
        }
    }

    return foundtag;
}


/// Check alias result of two values.
/// True: alias, False: not alias.
bool checkAlias(Value *Addr1, Value *Addr2,
		PointerAnalysisMap &aliasPtrs) {

	if (Addr1 == Addr2)
		return true;

	auto it = aliasPtrs.find(Addr1);
	if (it != aliasPtrs.end()) {
		if (it->second.count(Addr2) != 0)
			return true;
	}

	// May not need to do this further check.
	it = aliasPtrs.find(Addr2);
	if (it != aliasPtrs.end()) {
		if (it->second.count(Addr1) != 0)
			return true;
	}

	return false;
}


bool checkStringContainSubString(string origstr, string targetsubstr){
    
    if(origstr.length() == 0 || targetsubstr.length() == 0)
        return false;
    
    string::size_type idx;
    idx = origstr.find(targetsubstr);
    if(idx == string::npos)
        return false;
    else
        return true;
}

//Check if there is a path from fromBB to toBB 
bool checkBlockPairConnectivity(
    BasicBlock* fromBB, 
    BasicBlock* toBB){

    if(fromBB == NULL || toBB == NULL)
        return false;
    
    //Use BFS to detect if there is a path from fromBB to toBB
    std::list<BasicBlock *> EB; //BFS record list
    std::set<BasicBlock *> PB; //Global value set to avoid loop
    EB.push_back(fromBB);

    while (!EB.empty()) {

        BasicBlock *TB = EB.front(); //Current checking block
		EB.pop_front();

		if (PB.find(TB) != PB.end())
			continue;
		PB.insert(TB);

        //Found a path
        if(TB == toBB)
            return true;

        auto TI = TB->getTerminator();

        for(BasicBlock *Succ: successors(TB)){

            EB.push_back(Succ);
        }

    }//end while

    return false;
}

bool isCompositeType(Type *Ty) {
	if (Ty->isStructTy() 
			|| Ty->isArrayTy() 
			|| Ty->isVectorTy())
		return true;
	else 
		return false;
}

bool isStructorArrayType(Type *Ty) {
	if (Ty->isStructTy() || Ty->isArrayTy() )
		return true;
	else 
		return false;
}

size_t funcInfoHash(Function *F){
    
    hash<string> str_hash;
	string output;
    
    DISubprogram *SP = F->getSubprogram();

	if (SP) {
		output = SP->getFilename().str();
        stringstream ss;
        unsigned linenum = SP->getLine();
        ss<<linenum;
		output += ss.str();
	}

	output += F->getName();
	string::iterator end_pos = remove(output.begin(), 
			output.end(), ' ');
	output.erase(end_pos, output.end());

	return str_hash(output);

}

//Get the target string of __symbol_get()
string symbol_get_hander(CallInst* CAI){

    if(!CAI)
        return "";

    Value* __symbol_get_arg = CAI->getArgOperand(0);
    if(GEPOperator *GEP = dyn_cast<GEPOperator>(__symbol_get_arg)){
        if(!GEP->hasAllConstantIndices())
            return "";
        
        for(auto it = GEP->idx_begin() + 1; it != GEP->idx_end(); it++){
            ConstantInt *ConstI = dyn_cast<ConstantInt>(it->get());
            int Id = ConstI->getSExtValue();
            if(Id != 0)
                return "";
        }

        Value *PO = GEP->getPointerOperand();
        GlobalVariable* globalvar = dyn_cast<GlobalVariable>(PO);
        if(!globalvar)
            return "";
        
        Constant *Ini = globalvar->getInitializer();
        ConstantDataArray* CDA = dyn_cast<ConstantDataArray>(Ini);
        if(!CDA)
            return "";

        StringRef name = CDA->getAsString();
        if(name.size() == 0)
            return "";

        //NOTE: we need to filter the last char in the string
        return name.str().substr(0, name.str().length()-1);
    }

    return "";
}