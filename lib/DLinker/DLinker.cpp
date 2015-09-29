#include "llvm/DLinker/DLinker.h"
#include "llvm/Linker/Linker.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"

#include <vector>
#include <string>

using namespace llvm;

DLinkDiagnosticInfo::DLinkDiagnosticInfo(DiagnosticSeverity Severity, const Twine &Msg)
                : DiagnosticInfo(DK_Linker, Severity), Msg(Msg) {
}

void DLinkDiagnosticInfo::print(DiagnosticPrinter &DP) const { 
    DP << Msg; 
}

static void ParsingImports(ConstantStruct *Imports_CS, std::vector<ImportEntry> *ImportTable) {
    std::string FunctionName;
    Constant *ImportName;
    for (unsigned i = 0; i < Imports_CS->getNumOperands(); ++i) {
        Constant *Import_ptr = Imports_CS->getOperand(i);
        if (ConstantExpr *Import_CE = dyn_cast<ConstantExpr>(Import_ptr)) {
            if (Import_CE->getOpcode() == Instruction::PtrToInt) {
                if (GlobalVariable *Import_GV = dyn_cast<GlobalVariable>(Import_CE->getOperand(0))) {
                    Constant *Import = Import_GV->getInitializer();
                    if (ConstantDataArray *Import_name = dyn_cast<ConstantDataArray>(Import)) {
                        if (Import_name->isString()) {
                            FunctionName = Import_name->getAsString();
                            ImportName = Import_ptr;
                        }
                    }
                    if (Import->isNullValue()) {
                        Module *M = Import_GV->getParent();
                        for (Module::iterator F = M->begin(), E = M->end(); F != E; F++) {
                            if(F->isDeclaration()) {
                                if (strcmp(F->getName().data(), FunctionName.c_str()) == 0) {
                                    ImportEntry Entry = {ImportName, FunctionName, F};
                                    ImportTable->push_back(Entry);
                                    Import_GV->dump();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

static ConstantStruct *ParsingPsoRoot(GlobalVariable *PsoRoot, const int index) {
    Constant *Val = PsoRoot->getInitializer();
    if (ConstantStruct *CS = dyn_cast<ConstantStruct>(Val)) {
        Constant *PTR = CS->getOperand(index);
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(PTR)) {
            if (CE->getOpcode() == Instruction::PtrToInt) {
                if (GlobalVariable *GV = dyn_cast<GlobalVariable>(CE->getOperand(0))) {
                    Constant *Constants = GV->getInitializer();
                    return dyn_cast<ConstantStruct>(Constants);
                }
            }
        }
    }
    return NULL;
}

DLinker::DLinker(Module *M, LLVMContext &Context, DiagnosticHandlerFunction DiagnosticHandler) 
    : Composite(M), Context(Context), DiagnosticHandler(DiagnosticHandler) {
    setImportTable();
} 

DLinker::~DLinker() {
}

void DLinker::setImportTable() {
    for (Module::global_iterator F = Composite->global_begin(), E = Composite->global_end(); F != E; F++) {
        if (strcmp(F->getName().data(), "__pnacl_pso_root") == 0) {
            PsoRoot = F;
            ConstantStruct *Imports = ParsingPsoRoot(F, 0);                  
            ParsingImports(Imports, &ImportTable);
        }
    }
    Constants.resize(ImportTable.size() * 2);
    Types.resize(ImportTable.size() * 2);
}

bool DLinker::areTypesIsomorphic(Type *DstTy, Type *SrcTy) {
    if (DstTy->getTypeID() != SrcTy->getTypeID()) {
        return false;
    }

    return true;
}


bool DLinker::areFunctionsIsomorphic(Function *Dst, Function *Src) {
    FunctionType *DstTy = Dst->getFunctionType();
    FunctionType *SrcTy = Src->getFunctionType();

    if (!areTypesIsomorphic(DstTy, SrcTy)) {
        return false;
    }
        
    return true;
}

bool DLinker::linkPsoRoot(Module *M) {
    if (Composite->getDataLayout().isDefault()) {
        Composite->setDataLayout(M->getDataLayout());
    }

    if (Composite->getDataLayout() != M->getDataLayout()) {
        //TODO: check whether the datalayout is the same
        emitWarning("Linking tow modules of different data layouts: " +  
                Composite->getModuleIdentifier() + "' is '" +  
                Composite->getDataLayoutStr() + "' whereas '" + 
                M->getModuleIdentifier() + "' is  '" +
                M->getDataLayoutStr() + "'\n");
    }

    ExportFileName = M->getModuleIdentifier();
    for (Module::global_iterator F = M->global_begin(), E = M->global_end(); F != E; F++) {
        if(strcmp(F->getName().data(),"__pnacl_pso_root") == 0) {
            ConstantStruct *Exports = ParsingPsoRoot(F, 2);
            Linking(Exports);
        }
    }

    if (Constants[0] != NULL) {
        StructType *SType = StructType::get(Context, Types, true);
        Constant *CS = ConstantStruct::get(SType, Constants);

        GlobalVariable *GV = new GlobalVariable(*Composite,
            SType,
            true,
            GlobalVariable::InternalLinkage,
            0,
            "");

        GV->setInitializer(CS);
        GV->setAlignment(16);

        Constant *CE = ConstantExpr::getPtrToInt(GV, 
            IntegerType::get(Context, 32));

        Constant *Val = PsoRoot->getInitializer();
        if (ConstantStruct *CS = dyn_cast<ConstantStruct>(Val)) {
            Constant *PTR = CS->getOperand(0);
            PTR->replaceAllUsesWith(CE);
            if (GlobalVariable *G = dyn_cast<GlobalVariable>(PTR->getOperand(0))) {
                G->eraseFromParent();
            }
        }
       
        Composite->dump();
    }
    return 0;
}

bool DLinker::Linking(ConstantStruct *Exports_CS) {
    std::string FunctionName;

    for (unsigned i = 0; i < Exports_CS->getNumOperands(); i = i + 2) {
    Constant *Export_ptr = Exports_CS->getOperand(i);
        if (ConstantExpr *Export_CE = dyn_cast<ConstantExpr>(Export_ptr)) {
            if (Export_CE->getOpcode() == Instruction::PtrToInt) {
                //  Export function name
                if (GlobalVariable *Export_GV = dyn_cast<GlobalVariable>(Export_CE->getOperand(0))) {
                    Constant *Export = Export_GV->getInitializer();
                    if (ConstantDataArray *Export_name = dyn_cast<ConstantDataArray>(Export)) {
                        if (Export_name->isString()) {
                            for (unsigned i = 0; i < ImportTable.size(); ++i) {
                                if (strcmp(ImportTable[i].FunctionName.c_str(), 
                                    Export_name->getAsString().data()) == 0) {

                                    errs() << "Linking Symbol '" << ImportTable[i].FunctionName << "'\n";
                                    
                                    ArrayType *arrTy = ArrayType::get(IntegerType::get(Context,
                                        8), ExportFileName.length() + 1);
                                    Constant *const_arr = ConstantDataArray::getString(Context,
                                    ExportFileName.c_str(), true);

                                    GlobalVariable *global_str = new GlobalVariable(*Composite,
                                        arrTy,
                                        true,
                                        GlobalVariable::InternalLinkage,
                                        0,
                                        "");
                                    global_str->setAlignment(1);
                                    global_str->setInitializer(const_arr);

                                    Constant *CE = ConstantExpr::getPtrToInt(global_str, 
                                        IntegerType::get(Context, 32));

                                    Constants[i * 2] = ImportTable[i].ImportName;
                                    Constants[i * 2 + 1] = CE;
                                    Types[i * 2] = ImportTable[i].ImportName->getType();
                                    Types[i * 2 + 1] = CE->getType(); 
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return true;
}
