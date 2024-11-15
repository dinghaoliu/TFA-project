#ifndef _COMMON_H_
#define _COMMON_H_

#include <llvm/IR/Module.h>
#include <llvm/Analysis/TargetLibraryInfo.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/IR/DebugInfo.h>

#include <unistd.h>
#include <bitset>
#include <chrono>
#include <time.h>

using namespace llvm;
using namespace std;

#define LOG(lv, stmt)							\
	do {											\
		if (VerboseLevel >= lv)						\
		errs() << stmt;							\
	} while(0)


#define OP llvm::errs()

#define WARN(stmt) LOG(1, "\n[WARN] " << stmt);

#define ERR(stmt)													\
	do {																\
		errs() << "ERROR (" << __FUNCTION__ << "@" << __LINE__ << ")";	\
		errs() << ": " << stmt;											\
		exit(-1);														\
	} while(0)

/// Different colors for output
#define KNRM  "\x1B[0m"   /* Normal */
#define KRED  "\x1B[31m"  /* Red */
#define KGRN  "\x1B[32m"  /* Green */
#define KYEL  "\x1B[33m"  /* Yellow */
#define KBLU  "\x1B[34m"  /* Blue */
#define KMAG  "\x1B[35m"  /* Magenta */
#define KCYN  "\x1B[36m"  /* Cyan */
#define KWHT  "\x1B[37m"  /* White */

extern cl::opt<unsigned> VerboseLevel;

//
// Common functions
//

string getFileName(DILocation *Loc, 
		DISubprogram *SP=NULL,
    DIGlobalVariable *GV=NULL);
  
bool isConstant(Value *V);

string getSourceLine(string fn_str, unsigned lineno);
string getSourceLine(Instruction *I);
string getSourceLine(Function *F);

string getSourceFuncName(Instruction *I);

bool checkprintk(Instruction *I);

StringRef getCalledFuncName(CallInst *CI);

string extractMacro(string, Instruction* I);

DILocation *getSourceLocation(Instruction *I);

void printSourceCodeInfo(Value *V);
void printSourceCodeInfo(Function *F);
string getMacroInfo(Value *V);

void getSourceCodeInfo(Value *V, string &file,
                               unsigned &line);

Argument *getArgByNo(Function *F, int8_t ArgNo);

size_t valueHash(Value* V);
size_t funcHash(Function *F, bool withName = true);
size_t callHash(CallInst *CI);
size_t stringIdHash(string str, int Idx);
size_t typeHash(Type *Ty);
size_t typeNameIdxHash(Type *Ty, int Idx = -1);
size_t typeNameIdxHash(string Ty_name, int Idx = -1);
size_t typeIdxHash(Type *Ty, int Idx = -1);
size_t strHash(string str);
size_t hashIdxHash(size_t Hs, int Idx = -1);
string getTypeStr(Type *Ty);

void getSourceCodeLine(Value *V, string &line);

//
// Common data structures
//

class Helper {
public:
  // LLVM value
  static string getValueName(Value *v) {
    if (!v->hasName()) {
      return to_string(reinterpret_cast<uintptr_t>(v));
    } else {
      return v->getName().str();
    }
  }

  static string getValueType(Value *v) {
    if (Instruction *inst = dyn_cast<Instruction>(v)) {
      return string(inst->getOpcodeName());
    } else {
      return string("value " + to_string(v->getValueID()));
    }
  }

  static string getValueRepr(Value *v) {
    string str;
    raw_string_ostream stm(str);

    v->print(stm);
    stm.flush();

    return str;
  }

};

class Dumper {
public:
  Dumper() {}
  ~Dumper() {}

  // LLVM value
  void valueName(Value *val) {
    errs() << Helper::getValueName(val) << "\n";
  }

  void typedValue(Value *val) {
    errs() << "[" << Helper::getValueType(val) << "]"
           << Helper::getValueRepr(val)
           << "\n";
  }

};

extern Dumper DUMP;

#endif
