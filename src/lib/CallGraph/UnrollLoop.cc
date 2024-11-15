#include "CallGraph.h"

//return true if sort successed, or return false (there is a loop inside function)
bool CallGraphPass::topSort(Function *F){
	if(!F){
		return true;
	}

	map<BasicBlock*,int> indegreeMap;
	BasicBlock* currentblock;
	indegreeMap.clear();

	//init
	for (Function::iterator b = F->begin(), e = F->end();
		b != e; ++b) {
        BasicBlock * B = &*b;
		pair<BasicBlock*,int> value(B,0);
        indegreeMap.insert(value);
    }

	//init indegreeMap
	for (Function::iterator b = F->begin(), e = F->end();
		b != e; ++b) {
        BasicBlock * B = &*b;

		for (BasicBlock *Succ : successors(B)) {
			if(Succ == B)
				continue;
			indegreeMap[Succ]++;
		}
    }

	bool found = false;
	while(!indegreeMap.empty()){

		found = false;
		for(auto it = indegreeMap.begin(); it != indegreeMap.end(); it++){
			if(it->second==0){
				currentblock = it->first;
				found=true;
				break;
			}
		}

		if(!found){
			return false;
		}

		for (BasicBlock *Succ : successors(currentblock)) {
			if(Succ == currentblock)
				continue;
			indegreeMap[Succ]--;
		}

		indegreeMap.erase(currentblock);

	}
	return true;

}

void CallGraphPass::unrollLoops(Function *F) {

	if (F->isDeclaration())
		return;

	DominatorTree DT = DominatorTree();
	DT.recalculate(*F);
	LoopInfo *LI = new LoopInfo();
	LI->releaseMemory();
	LI->analyze(DT);

	// Collect all loops in the function
	set<Loop *> LPSet;
	for (LoopInfo::iterator i = LI->begin(), e = LI->end(); i!=e; ++i) {

		Loop *LP = *i;
		LPSet.insert(LP);

		list<Loop *> LPL;

		LPL.push_back(LP);
		while (!LPL.empty()) {
			LP = LPL.front();
			LPL.pop_front();
			vector<Loop *> SubLPs = LP->getSubLoops();
			for (auto SubLP : SubLPs) {
				LPSet.insert(SubLP);
				LPL.push_back(SubLP);
			}
		}
	}

	//OP << "First Loop Number: "<<LPSet.size()<<"\n";

	for (Loop *LP : LPSet) {

		// Get the header,latch block, exiting block of every loop
		BasicBlock *HeaderB = LP->getHeader();

		unsigned NumBE = LP->getNumBackEdges();
		SmallVector<BasicBlock *, 4> LatchBS;

		LP->getLoopLatches(LatchBS);

		for (BasicBlock *LatchB : LatchBS) {
			if (!HeaderB || !LatchB) {
				OP<<"ERROR: Cannot find Header Block or Latch Block\n";
				continue;
			}

			//OP << "  LatchB: Block-"<<getBlockName(LatchB)<<"\n";
			// Two cases:
			// 1. Latch Block has only one successor:
			// 	for loop or while loop;
			// 	In this case: set the Successor of Latch Block to the 
			//	successor block (out of loop one) of Header block
			// 2. Latch Block has two successor: 
			// do-while loop:
			// In this case: set the Successor of Latch Block to the
			//  another successor block of Latch block 

			// get the last instruction in the Latch block
			Instruction *TI = LatchB->getTerminator();
			//int NumSucc = TI->getNumSuccessors();
			//OP << "Current TI Block-"<<getBlockName(LatchB)<<"\n";
			// Case 1:
			//If assume this is a for loop, then assume there must be one
			//successor of HeaderB can jump out the loop, which may be broken
			//by some goto instructions (no successor can jump out) 

			if (LatchB->getSingleSuccessor() != NULL) {
				int Numdominate = 0;
				
				for (succ_iterator sit = succ_begin(HeaderB); 
						sit != succ_end(HeaderB); ++sit) {  

					BasicBlock *SuccB = *sit;
					BasicBlockEdge BBE = BasicBlockEdge(HeaderB, SuccB);
					// Header block has two successor,
					// one edge dominate Latch block;
					// another does not.
					if (DT.dominates(BBE, LatchB)){
						continue;
					}
					else {
						Numdominate++;
						TI->setSuccessor(0, SuccB);
					}
				}

				//Special case: all successors fall in loop or out of loop
				//Equal to 0 or 2
				if(Numdominate!=1){
					TI->setSuccessor(0, LatchB);
				}
			}
			// Case 2:
			else {

				for (int i = 0; i < TI->getNumSuccessors(); ++i) {

					BasicBlock *SuccB = TI->getSuccessor(i);

					if (SuccB == HeaderB){
						BasicBlock* targetbb;
						if(i!=0)
							targetbb=TI->getSuccessor(0);
						else
							targetbb=TI->getSuccessor(1);

						Value *Cond = NULL;
						BranchInst *BI = dyn_cast<BranchInst>(TI);
						if(BI){
							if(BI->isConditional())
			    				Cond = BI->getCondition();
						}
						if(Cond){
							Constant *Ct = dyn_cast<Constant>(Cond);
							if(Ct && Ct->isOneValue() && targetbb != TI->getSuccessor(0)){
								continue;
							}
						}

						TI->setSuccessor(i, targetbb);
						continue;
					}

				}	
			}
		}

		Instruction *HeaderB_TI = HeaderB->getTerminator();
		map<BasicBlock *,int> HeaderB_Follow_BBs;
		HeaderB_Follow_BBs.clear();
		for(int i = 0; i < HeaderB_TI->getNumSuccessors(); ++i){
			BasicBlock *SuccB = HeaderB_TI->getSuccessor(i);
			if(SuccB == HeaderB)
				continue;

			HeaderB_Follow_BBs[SuccB] = i;
		}

		for (BasicBlock *LatchB : LatchBS) {
			if (!HeaderB || !LatchB) {
				OP<<"ERROR: Cannot find Header Block or Latch Block\n";
				continue;
			}
			
			Instruction *LatchB_TI = LatchB->getTerminator();
			for (int i = 0; i < LatchB_TI->getNumSuccessors(); ++i) {
				BasicBlock *SuccB = LatchB_TI->getSuccessor(i);
				if(HeaderB_Follow_BBs.count(SuccB) != 0 && SuccB!= LatchB){
					HeaderB_TI->setSuccessor( HeaderB_Follow_BBs[SuccB],HeaderB);
				}
			}

		}
	}
}

bool CallGraphPass::checkLoop(Function *F){
	if (F->isDeclaration())
		return false;

	DominatorTree DT = DominatorTree();
	DT.recalculate(*F);
	LoopInfo *LI = new LoopInfo();
	LI->releaseMemory();
	LI->analyze(DT);

	// Collect all loops in the function
	set<Loop *> LPSet;
	for (LoopInfo::iterator i = LI->begin(), e = LI->end(); i!=e; ++i) {

		Loop *LP = *i;
		LPSet.insert(LP);

		list<Loop *> LPL;

		LPL.push_back(LP);
		while (!LPL.empty()) {
			LP = LPL.front();
			LPL.pop_front();
			vector<Loop *> SubLPs = LP->getSubLoops();
			for (auto SubLP : SubLPs) {
				LPSet.insert(SubLP);
				LPL.push_back(SubLP);
			}
		}
	}

	if(LPSet.empty())
		return false;
	else{

		int Loopnum = LPSet.size();
		for(Function::iterator b = F->begin(); 
            b != F->end(); b++){
			
			BasicBlock* bb = &*b;
			auto TI = bb->getTerminator();
			int NumSucc = TI->getNumSuccessors();
			
			if(NumSucc == 0)
				continue;

			for(BasicBlock *succblock : successors(bb)){
				
				if(succblock==bb){
					Loopnum--;
					continue;
				}
			}
		}

		if(Loopnum<1){
			return false;
		}
			
		else 
			return true;
	}

}