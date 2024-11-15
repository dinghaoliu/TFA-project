#ifndef _SACONFIG_H
#define _SACONFIG_H

#include "llvm/Support/FileSystem.h"

#include <map>
#include <unordered_map>
#include <set>
#include <unordered_set>
#include <fstream>

//
// Configurations for compilation.
//
#define SOUND_MODE 1

static void SetIcallIgnoreList(vector<string> &IcallIgnoreFileLoc, 
	vector<string> &IcallIgnoreLineNum) {

  string exepath = sys::fs::getMainExecutable(NULL, NULL);
  string exedir = exepath.substr(0, exepath.find_last_of('/'));
  string srcdir = exedir.substr(0, exedir.find_last_of('/'));
  srcdir = srcdir.substr(0, srcdir.find_last_of('/'));
  srcdir = srcdir + "/src/lib";
  string line;
  ifstream errfile(srcdir	+ "/configs/icall-ignore-list-fileloc");
  if (errfile.is_open()) {
		while (!errfile.eof()) {
			getline (errfile, line);
			if (line.length() > 1) {
				IcallIgnoreFileLoc.push_back(line);
			}
		}
    errfile.close();
  }

  ifstream errfile2(srcdir	+ "/configs/icall-ignore-list-linenum");
  if (errfile2.is_open()) {
		while (!errfile2.eof()) {
			getline (errfile2, line);
			if (line.length() > 1) {
				IcallIgnoreLineNum.push_back(line);
			}
		}
    errfile2.close();
  }

}

static void SetSafeMacros(set<string> &SafeMacros) {

  string exepath = sys::fs::getMainExecutable(NULL, NULL);
	string exedir = exepath.substr(0, exepath.find_last_of('/'));
  string srcdir = exedir.substr(0, exedir.find_last_of('/'));
  srcdir = srcdir.substr(0, srcdir.find_last_of('/'));
  srcdir = srcdir + "/src/lib";
	string line;
  ifstream errfile(srcdir	+ "/configs/safe-macro-list");
  if (errfile.is_open()) {
		while (!errfile.eof()) {
			getline (errfile, line);
			if (line.length() > 1) {
				SafeMacros.insert(line);
			}
		}
    errfile.close();
  }

}

// Setup debug functions here.
static void SetDebugFuncs(
  std::set<std::string> &DebugFuncs){

  std::string DebugFN[] = {
    "llvm.dbg.declare",
    "llvm.dbg.value",
    "llvm.dbg.label",
    "llvm.lifetime.start",
	"llvm.lifetime.end",
    "llvm.lifetime.start.p0i8",
    "llvm.lifetime.end.p0i8",
    "arch_static_branch",
	"printk",
  };

  for (auto F : DebugFN){
    DebugFuncs.insert(F);
  }

}

// Setup Binary Operand instructions here.
static void SetBinaryOperandInsts(
  std::set<std::string> &BinaryOperandInsts){

  std::string BinaryInst[] = {
    "and",
    "or",
    "xor",
    "shl",
    "lshr",
    "ashr",
    "add",
	"fadd",
    "sub",
	"fsub",
    "mul",
	"fmul",
    "sdiv",
    "udiv",
	"fdiv",
    "urem",
    "srem",
	"frem",
    //"alloca",
  };

  for (auto I : BinaryInst){
    BinaryOperandInsts.insert(I);
  }

}


// Setup functions that copy/move values.
static void SetCopyFuncs(
		// <src, dst, size>
		map<string, tuple<int8_t, int8_t, int8_t>> &CopyFuncs) {

	CopyFuncs["memcpy"] = make_tuple(1, 0, 2);
	CopyFuncs["__memcpy"] = make_tuple(1, 0, 2);
	CopyFuncs["llvm.memcpy.p0i8.p0i8.i32"] = make_tuple(1, 0, 2);
	CopyFuncs["llvm.memcpy.p0i8.p0i8.i64"] = make_tuple(1, 0, 2);
	CopyFuncs["strncpy"] = make_tuple(1, 0, 2);
	CopyFuncs["memmove"] = make_tuple(1, 0, 2);
	CopyFuncs["__memmove"] = make_tuple(1, 0, 2);
	CopyFuncs["llvm.memmove.p0i8.p0i8.i32"] = make_tuple(1, 0, 2);
	CopyFuncs["llvm.memmove.p0i8.p0i8.i64"] = make_tuple(1, 0, 2);
}

// Setup functions for heap allocations.
static void SetHeapAllocFuncs(
		std::set<std::string> &HeapAllocFuncs){

	std::string HeapAllocFN[] = {
		"__kmalloc",
		"__vmalloc",
		"vmalloc",
		"krealloc",
		"devm_kzalloc",
		"vzalloc",
		"malloc",
		"kmem_cache_alloc",
		"__alloc_skb",
		"kzalloc", //New added
		"kmalloc", //New added
		"kmalloc_array", //New added

	};

	for (auto F : HeapAllocFN) {
		HeapAllocFuncs.insert(F);
	}
}

#endif
