#ifndef _GLOBAL_DBTOOLS_H
#define _GLOBAL_DBTOOLS_H

#include <llvm/IR/DebugInfo.h>
#include <llvm/Pass.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Function.h>
#include <llvm/Support/Debug.h>
#include <llvm/IR/InstIterator.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Value.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Analysis/CallGraph.h>
#include <llvm/IR/Dominators.h>
#include <llvm/Analysis/BasicAliasAnalysis.h>

#include "Analyzer.h"
#include "Common.h"
#include "Config.h"
#include "Tools.h"
#include "../CallGraph/CallGraph.h"
#include <algorithm>
#include <mysql/mysql.h>


void update_database(GlobalContext *Ctx);

void update_database_fset(GlobalContext *Ctx);

//Used to speed up database insert
void build_insert_batch_for_icall_table(GlobalContext *Ctx, int batch_size, vector<string> &cmds);
void build_insert_batch_for_caller_table(GlobalContext *Ctx, int batch_size, vector<string> &cmds);
void build_insert_batch_for_func_table(GlobalContext *Ctx, int batch_size, vector<string> &cmds);

void rearrange_fset(GlobalContext *Ctx);

size_t funcSetHash(FuncSet fset);

#endif