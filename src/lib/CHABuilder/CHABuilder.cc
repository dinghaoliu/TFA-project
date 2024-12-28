#include "CHABuilder.h"

using namespace llvm;

map<GlobalVariable*, vector<vector<Value*>>> CHABuilderPass::GlobalVTableMap;
map<string, GlobalVariable*> CHABuilderPass::GlobalNameGVMap;
map<string, vector<string>> CHABuilderPass::ParentClassMap;
map<string, vector<string>> CHABuilderPass::ChildClassMap;
map<GlobalVariable*, string> CHABuilderPass::ClassNameMap;

bool CHABuilderPass::doInitialization(Module *M) {

    for (Module::global_iterator gi = M->global_begin(); 
		gi != M->global_end(); ++gi) {

		GlobalVariable* GV = &*gi;

		//Build class hierarchy graph
		CPPClassHierarchyRelationCollector(GV);

        //Record C++ VTable info
        CPPVTableBuilder(GV);

	}

    return false;
}

bool CHABuilderPass::doModulePass(Module *M) {

    for (Module::global_iterator gi = M->global_begin(); 
		gi != M->global_end(); ++gi) {

        GlobalVariable* GV = &*gi;

        CPPVTableMapping(GV);
    
    }

    return false;
}

static bool analyze_once = true;

bool CHABuilderPass::doFinalization(Module *M) {

    if(analyze_once){
        analyze_once = false;

        for(auto it = ChildClassMap.begin(); it != ChildClassMap.end(); it++){
            string parent_class_RTTI_name = it->first;
            string parent_class_vt_name = parent_class_RTTI_name;
            parent_class_vt_name.replace(0, 4, "_ZTV");
            if(GlobalNameGVMap.count(parent_class_vt_name) == 0)
                continue;

            GlobalVariable* vt = GlobalNameGVMap[parent_class_vt_name];
            if(ClassNameMap.count(vt) == 0)
                continue;

            string vt_class = ClassNameMap[vt];

            for(string derived_class_RTTI_name : it->second){
                string derived_class_vt_name = derived_class_RTTI_name;
                derived_class_vt_name.replace(0, 4, "_ZTV");
                if(GlobalNameGVMap.count(derived_class_vt_name) == 0)
                    continue;

                GlobalVariable* derived_vt = GlobalNameGVMap[derived_class_vt_name];
                if(ClassNameMap.count(derived_vt) == 0)
                    continue;

                string derived_vt_class = ClassNameMap[derived_vt];
                Ctx->GlobalClassHierarchyMap[vt_class].insert(derived_vt_class);
            }
        }

    }

    return false;
}