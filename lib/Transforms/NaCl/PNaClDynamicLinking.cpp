//===-- PNaClDynamicLinking.cpp - Lists PNaCl Dynamic Linking passes ------===//
//
//                   The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// Licence. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements the meta-passes "-pnacl-pso-root".
// It lists their constituent passes.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Triple.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/NaCl.h"
#include "llvm/Transforms/Scalar.h"

using namespace llvm;

void llvm::PNaClDynamicLinkingPasses(Triple *T, PassManagerBase &PM) {
  
  PM.add(createPNaClPsoRootPass());
}
