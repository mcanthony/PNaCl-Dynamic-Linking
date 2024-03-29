//===-------- PNaClPsoRoot.cpp - PNaCl Pso Root pass ---------------===//
//
//                   The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===---------------------------------------------------------------===//
//
// The PNaClPsoRoot pass is used to gather defined and declared function 
// and global variables to produce a struce named __pnacl_pso_root that
// is provided to dynamic linking using.
//
//===---------------------------------------------------------------===//

#include "llvm/Pass.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/NaCl.h"

#include <vector>
#include <string.h>
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/GlobalVariable.h"

using namespace llvm;
using namespace std;

namespace {

    class PNaClPsoRoot : public ModulePass {
    public:
      static char ID;
      PNaClPsoRoot() : ModulePass(ID) {
          initializePNaClPsoRootPass(*PassRegistry::getPassRegistry());
      }

      virtual bool runOnModule(Module &M);
    };

}

char PNaClPsoRoot::ID = 0;
INITIALIZE_PASS(PNaClPsoRoot, "_pnacl-pso-root",
                "PNaCl PSO ROOT",
		false, false)

bool PNaClPsoRoot::runOnModule(Module &M) {
   
  // Char Definitions
  PointerType *ptrTy_int8 = PointerType::get(IntegerType::get(M.getContext(), 8), 0);
 
  // Import
  StructType *struct_Import = M.getTypeByName("struct.Import");
  if (!struct_Import) {
    struct_Import = StructType::create(M.getContext(), "struct.Import");
  }
  vector<Type*> struct_Import_fields;
  struct_Import_fields.push_back(ptrTy_int8);
  struct_Import_fields.push_back(ptrTy_int8);
  if (struct_Import->isOpaque()) {
    struct_Import->setBody(struct_Import_fields, false);
  }

  // Import_funcs
  StructType *struct_Import_funcs = M.getTypeByName("struct.Import_funcs");
  if (!struct_Import_funcs) {
    struct_Import_funcs = StructType::create(M.getContext(), "struct.Import_funcs");
  }

  PointerType *ptrTy_struct_Import_funcs = PointerType::get(struct_Import_funcs, 0);
  
  // Export
  StructType *struct_Export = M.getTypeByName("struct.Export");
  if (!struct_Export) {
    struct_Export = StructType::create(M.getContext(), "struct.Export");
  }
  vector<Type*> struct_Export_fields;
  struct_Export_fields.push_back(ptrTy_int8);
  struct_Export_fields.push_back(ptrTy_int8);
  if (struct_Export->isOpaque()) {
    struct_Export->setBody(struct_Export_fields, false);
  }

  // pso_root
  StructType *struct_Pso_root = M.getTypeByName("struct.Pso_root");
  if (!struct_Pso_root) {
    struct_Pso_root = StructType::create(M.getContext(), "struct.Pso_root");
  }
  vector<Type*> struct_Pso_root_fields;
  PointerType *ptrTy_Import = PointerType::get(struct_Import, 0);
  PointerType *ptrTy_Export = PointerType::get(struct_Export, 0);

  struct_Pso_root_fields.push_back(ptrTy_Import);
  struct_Pso_root_fields.push_back(ptrTy_struct_Import_funcs);
  struct_Pso_root_fields.push_back(ptrTy_Export);
  
  if(struct_Pso_root->isOpaque()) {
    struct_Pso_root->setBody(struct_Pso_root_fields, false);
  }
  
  // functions
  int i = 1;
  ConstantInt * const_int32_0 = ConstantInt::get(M.getContext(), APInt(32, StringRef("0"), 10));
  vector<Constant*> const_import_elems;
  vector<Constant*> const_export_elems;
  vector<Type*> struct_Import_funcs_fields;
  GlobalVariable *import_library;
  bool isInit = false;
  // Constant *const_import_library;
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    if (F->getName().startswith("llvm.") || F->getName().startswith("nacl_")) {
      continue;
    }
    // .str%d internal constant
    char buffer[50];
    sprintf(buffer, ".__str%d", i);
    int str_size = (F->getName()).size();  
    ArrayType *arrTy = ArrayType::get(IntegerType::get(M.getContext(), 8), str_size + 1);
    Constant *const_arr = ConstantDataArray::getString(M.getContext(), F->getName().data(), true);
    Twine twine(buffer);
    GlobalVariable *global_str = new GlobalVariable(M,
        /*Type=*/arrTy,
	/*isConstant=*/true,
	/*Linkage=*/GlobalValue::PrivateLinkage,
	/*Initializer=*/0,
	/*Name=*/twine);
    global_str->setAlignment(1);
    global_str->setInitializer(const_arr);

    vector<Constant*> const_name_indices;
    const_name_indices.push_back(const_int32_0);
    const_name_indices.push_back(const_int32_0);
    Constant *const_name = ConstantExpr::getGetElementPtr(arrTy, global_str, const_name_indices);
    if (F->isDeclaration()) {
      ArrayType *arrTy_import_library = ArrayType::get(IntegerType::get(M.getContext(), 8), 1);
      //GlobalVariable *import_library = M.getNamedGlobal("__.str");
      if (!isInit) {
        // initialize empty linking library name
        import_library = new GlobalVariable(M,
          /*Type=*/arrTy_import_library, 
          /*isConstant=*/false, 
          /*Linkage=*/GlobalValue::PrivateLinkage,
          /*Initializer=*/0,
          /*Name=*/".__str");
        import_library->setAlignment(1);
        ConstantAggregateZero *const_zero = ConstantAggregateZero::get(arrTy_import_library);
	import_library->setInitializer(const_zero);
        isInit = true;
      }
      vector<Constant*> const_import_library_indices;
      const_import_library_indices.push_back(const_int32_0);
      const_import_library_indices.push_back(const_int32_0);
      Constant *const_import_library = ConstantExpr::getGetElementPtr(arrTy_import_library, 
          import_library, 
	  const_import_library_indices);

      vector<Constant*> const_import_fields;
      const_import_fields.push_back(const_name);
      const_import_fields.push_back(const_import_library);
      Constant *const_import = ConstantStruct::get(struct_Import, const_import_fields);
      const_import_elems.push_back(const_import);

      struct_Import_funcs_fields.push_back(PointerType::get(F->getFunctionType(), 0));  
    } else {
      vector<Constant*> const_export_fields;
      const_export_fields.push_back(const_name);
      Constant *const_func = ConstantExpr::getCast(Instruction::BitCast, F, ptrTy_int8);
      const_export_fields.push_back(const_func);
      Constant *const_export = ConstantStruct::get(struct_Export, const_export_fields);
      const_export_elems.push_back(const_export);
    }
    i++;
  }

  // GlobalVariable
  ArrayType *arrTy_Import = ArrayType::get(struct_Import, const_import_elems.size());
  Constant* const_import_array = ConstantArray::get(arrTy_Import, const_import_elems);
  GlobalVariable *imports = new GlobalVariable(M,
      /*Type=*/arrTy_Import,
      /*isConstant=*/false,
      /*Linkage=*/GlobalValue::InternalLinkage,
      /*Initializer=*/0,
      /*Name=*/"__imports");
  imports->setAlignment(16);
  imports->setInitializer(const_import_array);
    
  if (struct_Import_funcs->isOpaque()) {
    struct_Import_funcs->setBody(struct_Import_funcs_fields);
  }  
    
  ConstantAggregateZero *const_struct_import = ConstantAggregateZero::get(struct_Import_funcs);
  GlobalVariable *imports_funcs = new GlobalVariable(M,
      /*Type=*/struct_Import_funcs,
      /*isConstant=*/false,
      /*Linkage=*/GlobalValue::InternalLinkage,
      /*Initializer=*/0,
      /*Name=*/"__import_funcs");
  imports_funcs->setAlignment(8);
  imports_funcs->setInitializer(const_struct_import);
  
  ArrayType *arrTy_Export = ArrayType::get(struct_Export, const_export_elems.size());
  Constant* const_export_array = ConstantArray::get(arrTy_Export, const_export_elems);
  GlobalVariable *exports = new GlobalVariable(M,
      /*Type=*/arrTy_Export,
      /*isConstant=*/true,
      /*Linkage=*/GlobalValue::InternalLinkage,
      /*Initializer=*/0,
      /*Name=*/"__exports");
  exports->setAlignment(16);
  exports->setInitializer(const_export_array);

  vector<Constant*> struct_pso_root_fields;
    
  vector<Constant*> const_ptr_indices_1;
  const_ptr_indices_1.push_back(const_int32_0);
  const_ptr_indices_1.push_back(const_int32_0);
  Constant *const_ptr_1 = ConstantExpr::getGetElementPtr(arrTy_Import, imports, const_ptr_indices_1);
  struct_pso_root_fields.push_back(const_ptr_1);
  struct_pso_root_fields.push_back(imports_funcs);

  vector<Constant*> const_ptr_indices_2;
  const_ptr_indices_2.push_back(const_int32_0);
  const_ptr_indices_2.push_back(const_int32_0);
  Constant *const_ptr_2 = ConstantExpr::getGetElementPtr(arrTy_Export, exports, const_ptr_indices_2);
  struct_pso_root_fields.push_back(const_ptr_2);

  Constant *const_pso_root = ConstantStruct::get(struct_Pso_root, struct_pso_root_fields);

  GlobalVariable *__pnacl_pso_root = new GlobalVariable(M,
      /*Type=*/struct_Pso_root,
      /*isConstant=*/false,
      /*Linkage=*/GlobalValue::ExternalLinkage,
      /*Initializer=*/0,
      /*Name=*/"__pnacl_pso_root");
  __pnacl_pso_root->setAlignment(8);
  __pnacl_pso_root->setInitializer(const_pso_root);

  return true;
  
}

ModulePass *llvm::createPNaClPsoRootPass() {
  return new PNaClPsoRoot();
}
