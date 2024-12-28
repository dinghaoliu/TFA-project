#include "CHABuilder.h"

using namespace llvm;

StringRef getClassName(GlobalVariable* GV){
    StringRef className = "";
	for(User* u : GV->users()){
		if(GEPOperator *GEPO = dyn_cast<GEPOperator>(u)){

			for(User* u2 : GEPO->users()){
				if(BitCastOperator *CastO = dyn_cast<BitCastOperator>(u2)){
					for(User* u3 : u2->users()){
						//There could be multiple store
						if(StoreInst *STI = dyn_cast<StoreInst>(u3)){
							
							Value* vop = STI->getValueOperand();
                    		Value* pop = STI->getPointerOperand();
							if(vop != u2)
								continue;
							
							BitCastOperator *CastO = dyn_cast<BitCastOperator>(pop);

							if(CastO){
								Value *ToV = CastO, *FromV = CastO->getOperand(0);
								Type *ToTy = ToV->getType(), *FromTy = FromV->getType();

								if(FromTy->isPointerTy()){
									Type *ToeleType = FromTy->getPointerElementType();
									if(ToeleType->isStructTy()){
										className = ToeleType->getStructName();
									}
								}
							}

							GEPOperator *GEPO_outer = dyn_cast<GEPOperator>(pop);
							if(GEPO_outer){
								auto sourceTy = GEPO_outer->getSourceElementType();
								if(sourceTy->isStructTy()){
									className = sourceTy->getStructName();
								}
							}
						}
					}
				}
			}
		}
	}
    return className;
}

void CHABuilderPass::CPPClassHierarchyRelationCollector(GlobalVariable* GV){
	
	if(!GV->getName().startswith("_ZTI"))
		return;
	
	if (!GV->hasInitializer()){
		return;
	}

	Constant *Ini = GV->getInitializer();
	if (!isa<ConstantAggregate>(Ini)){
		return;
	}

    GlobalNameGVMap[GV->getName().str()] = GV;

	for (auto oi = Ini->op_begin(), oe = Ini->op_end(); oi != oe; ++oi) {
		
		Value *ele = *oi;

		BitCastOperator *CastO = dyn_cast<BitCastOperator>(ele);
		if(!CastO)
			continue;
		
		Value *FromV = CastO->getOperand(0);

		GlobalVariable* gv = dyn_cast<GlobalVariable>(FromV);
        if(!gv)
            continue;
		
		StringRef gvname = gv->getName();
		if(!gvname.startswith("_ZTI"))
			continue;
		//OP<<"gv: "<<gv->getName()<<"\n";
		ParentClassMap[GV->getName().str()].push_back(gvname.str());
		ChildClassMap[gvname.str()].push_back(GV->getName().str());
	}

}

//Handle C++ virtual tables
//In most cases, VTable is a struct with one array element
void CHABuilderPass::CPPVTableBuilder(GlobalVariable* GV){
	
	if(!GV->getName().startswith("_ZTV"))
		return;

	//TODO: handle this
	if (!GV->hasInitializer()){
		return;
	}

	Constant *Ini = GV->getInitializer();
	if (!isa<ConstantAggregate>(Ini)){
		return;
	}

    GlobalNameGVMap[GV->getName().str()] = GV;
	
	//VTable is a struct, with one or multiple arrays inside
	if(!Ini->getType()->isStructTy()){
		return;
	}

	//TODO: support multiple inheritance here
	//This implementation only supports single inheritance
	unsigned num_operand = Ini->getNumOperands();

	//Get the class name
	//Using demangle sometimes cannot get usable results
	//string className_old = demangle(GV->getName().str());
    auto className = getClassName(GV);
	if(className == "")
		return;

	ClassNameMap[GV] = className.str();

    //Handle multiple inheritance
	for(int idx = 0; idx < num_operand; idx++){
		auto arr = Ini->getOperand(idx);
		//OP<<"arr: "<<*arr<<"\n";
		ConstantArray *CA = dyn_cast<ConstantArray>(arr);
		if(!CA){
			continue;
		}

		int i = 0; //Used to ignore the first two elements
		vector<Value*> single_inheritance_vtable;
		for (auto oi = CA->op_begin(), oe = CA->op_end(); oi != oe; ++oi) {
			Value *ele = *oi;
			
			//Ignore the first two elements (vtable info, RTTI and offset to top)
			if(i <= 1){
				i++;
				continue;
			}

			BitCastOperator *CastO = dyn_cast<BitCastOperator>(ele);
			if(!CastO)
				continue;

            Value *FromV = CastO->getOperand(0);

            GlobalAlias *GA = dyn_cast<GlobalAlias>(FromV);
            if(GA)
                FromV = GA->getAliasee();
			
			Function* vf = dyn_cast<Function>(FromV);
			if(!vf)
				continue;

			single_inheritance_vtable.push_back(FromV);

			i++;
		}

		GlobalVTableMap[GV].push_back(single_inheritance_vtable);
	}

}

void CHABuilderPass::CPPVTableMapping(GlobalVariable* GV){

    if(!GV->getName().startswith("_ZTV"))
		return;

	//TODO: handle this
	if (!GV->hasInitializer()){
		return;
	}

	Constant *Ini = GV->getInitializer();
	if (!isa<ConstantAggregate>(Ini)){
		return;
	}
	
	//VTable is a struct, with one or multiple arrays inside
	if(!Ini->getType()->isStructTy()){
		return;
	}

	unsigned num_operand = Ini->getNumOperands();

	//Get the class name
	auto className = getClassName(GV);
	if(className == "")
		return;

	//Handle multiple inheritance
	for(int idx = 0; idx < num_operand; idx++){
		auto arr = Ini->getOperand(idx);

		ConstantArray *CA = dyn_cast<ConstantArray>(arr);
		if(!CA)
			continue;

		int i = 0;
		for (auto oi = CA->op_begin(), oe = CA->op_end(); oi != oe; ++oi) {
			Value *ele = *oi;
			
			//Ignore the first two elements (vtable info, RTTI and offset to top)
			if(i <= 1){
				i++;
				continue;
			}

			BitCastOperator *CastO = dyn_cast<BitCastOperator>(ele);
			if(!CastO)
				continue;
			
			Value *FromV = CastO->getOperand(0);
			Function* vf = dyn_cast<Function>(FromV);
			if(!vf)
				continue;

			string vf_method_name = getCPPFuncName(vf);

			//Find pure virtual function
			//In this case, it must be implemented by derived classes
			if(vf->getName() == "__cxa_pure_virtual"){
				vf_method_name = getMethodNameOfPureVirtualCall(GV, i);
			}

            if(vf_method_name == "")
                continue;

			Ctx->Global_VTable_Map[className.str()][vf_method_name].insert(vf);
            Ctx->Global_Class_Method_Index_Map[className.str()][i-2].insert(vf_method_name);
			i++;
		}
	}
 }

string CHABuilderPass::getMethodNameOfPureVirtualCall(GlobalVariable* GV, int idx){
	
	// Find derived class
    int index = idx - 2;
    string method_name = "";
	string class_vtable_name = GV->getName().str();
	string class_type_name = class_vtable_name;
	class_type_name.replace(0, 4, "_ZTI");
	if(ChildClassMap.count(class_type_name) == 0)
		return "";

	//Get the method name from derived class
	for(string derived_class_type_name : ChildClassMap[class_type_name]){
			
		string derived_class_vtable_name = derived_class_type_name;
		derived_class_vtable_name.replace(0, 4, "_ZTV");
		if(GlobalNameGVMap.count(derived_class_vtable_name) == 0)
			continue;

		GlobalVariable* derived_vtable_gv = GlobalNameGVMap[derived_class_vtable_name];
		if(GlobalVTableMap.count(derived_vtable_gv) == 0)
			continue;

		vector<vector<Value*>> derived_vtable_arr = GlobalVTableMap[derived_vtable_gv];

        //Case1: the method is implemented within single inheritance scenario
        //check the first vtable array
        if(derived_vtable_arr.size() == 1){
            //OP<<"here\n";
            auto method_array = derived_vtable_arr[0];
            if(index > method_array.size())
                continue;

            Value* method = method_array[index];
            Function* method_f = dyn_cast<Function>(method);
			if(!method_f)
				continue;

			method_name = getCPPFuncName(method_f);
            if(method_name != ""){
                return method_name;
            }
            continue;
        }
			
		//Case2: the method is implemented in multiple inheritance scenario
        //check the thunk of inherited class vtable
        if(ParentClassMap.count(derived_class_type_name) == 0)
			continue;

		vector<string> parent_class_arr = ParentClassMap[derived_class_type_name];
		if(parent_class_arr.empty())
			continue;

		if(derived_vtable_arr.size() != parent_class_arr.size())
			continue;
			
		int derived_idx = -1;
		for(int i = 0; i < parent_class_arr.size(); i++){
			if(parent_class_arr[i] == class_type_name){
				derived_idx = i;
				break;
			}
		}
		if(derived_idx == -1 || derived_idx > derived_vtable_arr.size())
			continue;

		auto method_array = derived_vtable_arr[derived_idx];
        if(index > method_array.size())
            continue;

        Value* method = method_array[index];
        Function* method_f = dyn_cast<Function>(method);
		if(!method_f)
			continue;

        for (inst_iterator i = inst_begin(method_f), e = inst_end(method_f); i != e; ++i) {
		
            CallInst *cai = dyn_cast<CallInst>(&*i);
            if(cai){
                if(cai->isTailCall()){
                    Function* f = cai->getCalledFunction();
                    if(!f)
                        continue;
                    method_name = getCPPFuncName(f);
                    if(method_name != ""){
                        return method_name;
                    }
                }
            }
        }
	}

	return method_name;
}