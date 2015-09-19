#include "llvm/DLinker/DLinker.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/DiagnosticInfo.h"

#include <vector>
#include <string>

using namespace llvm;

namespace {
    class FunctionEntry {
        public:
            FunctionEntry(std::string functionName, std::string fileName,Function *function);
            ~FunctionEntry();
           
            std::string getFunctionName();
            Function *getFunction();
            std::string getFileName();
        private:
            std::string FunctionName;
            std::string FileName;
            Function *F;
    };

    class FunctionTable {
        public:
            FunctionTable();
            ~FunctionTable();

            void insert(FunctionEntry Entry);
            int getSize();
            FunctionEntry get(int index);
        private:
            std::vector<FunctionEntry> Entrys;

    
    };
}

FunctionEntry::FunctionEntry(std::string functionName, std::string fileName, Function *function) : FunctionName(functionName), FileName(fileName), F(function) {
}

FunctionEntry::~FunctionEntry() {

}

std::string FunctionEntry::getFunctionName() {
    return FunctionName;
}

Function *FunctionEntry::getFunction() {
    return F;
}

std::string FunctionEntry::getFileName() {
    return FileName;
}

FunctionTable::FunctionTable() {
    Entrys.clear();
}

FunctionTable::~FunctionTable() {

}

void FunctionTable::insert(FunctionEntry Entry) {
    Entrys.push_back(Entry);
}

int FunctionTable::getSize() {
    return Entrys.size();
}

FunctionEntry FunctionTable::get(int index) {
    return Entrys[index];
}

static void parseExports(FunctionTable *FT, ConstantStruct *Exports_CS, const std::string &InputFilename) {
    std::string functionName;
    for (unsigned i = 0; i < Exports_CS->getNumOperands(); ++i) {
    Constant *Export_ptr = Exports_CS->getOperand(i);
        if (ConstantExpr *Export_CE = dyn_cast<ConstantExpr>(Export_ptr)) {
            if (Export_CE->getOpcode() == Instruction::PtrToInt) {
                // get export function name
                if (GlobalVariable *Export_GV = dyn_cast<GlobalVariable>(Export_CE->getOperand(0))) {
                    Constant *Export = Export_GV->getInitializer();
                    if (ConstantDataArray *Export_name = dyn_cast<ConstantDataArray>(Export)) {
                        if (Export_name->isString()) {
                            errs() << "Export_name\n";
                            errs() << Export_name->getAsString();
                            errs() << "\n";
                            functionName = Export_name->getAsString();
                        }
                    }
                }

                // get export function
                if (Function *Export_F = dyn_cast<Function>(Export_CE->getOperand(0))) {
                    FunctionEntry Entry(functionName, InputFilename, Export_F);
                    FT->insert(Entry);
                }   
            }
        }
    }
}

static void parsePsoRoot(FunctionTable *FT, GlobalVariable *GV, const std::string &InputFilename) {
    Constant *Val = GV->getInitializer();
    if (ConstantStruct *CS = dyn_cast<ConstantStruct>(Val)) {
        
        // imports
        Constant *Imports_ptr = CS->getOperand(0);
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Imports_ptr)) {
            if (CE->getOpcode() == Instruction::PtrToInt) {
                if (GlobalVariable *Imports_GV = dyn_cast<GlobalVariable>(CE->getOperand(0))) {
                    Constant *Imports = Imports_GV->getInitializer();
                    errs() << "imports->getNumOperands()\n";
                    errs() << Imports->getNumOperands();
                    errs() << "\n";
                    for (unsigned i = 0; i < Imports->getNumOperands(); ++i) {
                        
                    }
                }
            }
        }

        // imports_funcs
        Constant *Imports_funcs_ptr = CS->getOperand(1);
        if (ConstantExpr *CE = dyn_cast<ConstantExpr>(Imports_funcs_ptr)) {
            
        }

        // exports
        Constant *Exports_ptr = CS->getOperand(2);
        if (ConstantExpr *Exports_CE = dyn_cast<ConstantExpr>(Exports_ptr)) {
            if (Exports_CE->getOpcode() == Instruction::PtrToInt) {
                if (GlobalVariable *Exports_GV = dyn_cast<GlobalVariable>(Exports_CE->getOperand(0))) {
                    Constant *Exports = Exports_GV->getInitializer();
                    errs() << "exports->getNumOperands()\n";
                    errs() << Exports->getNumOperands();
                    errs() << "\n";
                    if (ConstantStruct *Exports_CS = dyn_cast<ConstantStruct>(Exports)) {
                        parseExports(FT, Exports_CS, InputFilename);    
                    }
                }
            }
        }
    }
}

DLinker::DLinker(DiagnosticHandlerFunction DiagnosticHandler) : DH(DiagnosticHandler),
        DL(""){

} 

DLinker::~DLinker() {

}

bool DLinker::linkPsoRoot(Module *M) {
    if (DL.isDefault()) {
        DL = M->getDataLayout();
    }

    if (DL != M->getDataLayout()) {
        //TODO: check whether the datalayout is the same
    }

    FunctionTable FT;

    for (Module::global_iterator F = M->global_begin(), E = M->global_end(); F != E; F++) {
        if(strcmp(F->getName().data(),"__pnacl_pso_root") == 0) {
            parsePsoRoot(&FT, F, M->getModuleIdentifier());                  
        }
    }

    errs() << FT.getSize();
    errs() << "\n";

    errs() << FT.get(0).getFunctionName();
    errs() << "\n";
    errs() << FT.get(1).getFunctionName();
    for (int i = 0; i < FT.getSize(); i++) {
        FT.get(i).getFunction()->dump();
    }

    return 0;
}

