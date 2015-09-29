#ifndef LLVM_DLINKER_DLINKER_H
#define LLVM_DLINKER_DLINKER_H

#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Constants.h"
#include <vector>


namespace llvm {

    /* @2 = internal constant [4 x i8] c"add\00", align 1
     *
     * ImportName: i32 ptrtoint ([4 x i8]* @2 to i32)
     * FunctionName: add
     * ImportFunction: declare i32 @add(i32)
     * 
     */

    typedef struct {
        Constant *ImportName;
        std::string FunctionName;
        Function *ImportFunction;
    } ImportEntry;

    class DLinkDiagnosticInfo : public DiagnosticInfo {
        public:
            DLinkDiagnosticInfo(DiagnosticSeverity Severity, const Twine &Msg);
            void print(DiagnosticPrinter &DP) const override;
        private:
            const Twine &Msg;
     };

    class DLinker {
        public:
            DLinker(Module *M,LLVMContext &Context, DiagnosticHandlerFunction DiagnosticHandler);
            ~DLinker();

            bool linkPsoRoot(Module* M);
            bool Linking(ConstantStruct *CS);
        private:
            Module *Composite;
            LLVMContext &Context;
            DiagnosticHandlerFunction DiagnosticHandler;
            std::vector<ImportEntry> ImportTable;
            std::string ExportFileName;

            GlobalVariable *PsoRoot;
            std::vector<Constant*> Constants;
            std::vector<Type*> Types;
            GlobalVariable *ZeroInitializer;

            bool areTypesIsomorphic(Type *DstTy, Type *SrcTy);        
            bool areFunctionsIsomorphic(Function *Dst, Function *Src);
            void setImportTable();

            void emitWarning(const Twine &Message) {
                DiagnosticHandler(DLinkDiagnosticInfo(DS_Warning, Message));
            }
            void emitError(const Twine &Message) {
                DiagnosticHandler(DLinkDiagnosticInfo(DS_Error, Message));
            }
    };
}

#endif
