//===- pnacl-dlink.cpp - Low-level LLVM linker ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This utility may be invoked in the following manner:
//  llvm-link a.bc b.bc c.bc -o x.bc
//
//===----------------------------------------------------------------------===//

#include "llvm/DLinker/DLinker.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/StreamingMemoryObject.h"
#include "llvm/Support/ToolOutputFile.h"
#include <memory>
using namespace llvm;

static cl::opt<NaClFileFormat>
InputFileFormat(
        "bitcode-format",
        cl::desc("Define format of input file:"),
        cl::values(
            clEnumValN(LLVMFormat, "llvm", "LLVM file (default)"),
            clEnumValN(PNaClFormat, "pnacl", "PNaCl bitcode file"),
            clEnumValEnd), cl::init(PNaClFormat));

static cl::list<std::string>
InputFilenames(cl::Positional, cl::OneOrMore,
               cl::desc("<input bitcode files>"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Override output filename"), cl::init("-"),
               cl::value_desc("filename"));

static cl::opt<bool>
Force("f", cl::desc("Enable binary output on terminals"));

static cl::opt<bool>
OutputAssembly("S",
         cl::desc("Write output as LLVM assembly"), cl::Hidden);

static cl::opt<bool>
Verbose("v", cl::desc("Print information about actions taken"));

static cl::opt<bool>
DumpAsm("d", cl::desc("Print assembly as linked"), cl::Hidden);

static cl::opt<bool>
SuppressWarnings("suppress-warnings", cl::desc("Suppress all linking warnings"),
                 cl::init(false));

static cl::opt<bool> PreserveBitcodeUseListOrder(
    "preserve-bc-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM bitcode."),
    cl::init(true), cl::Hidden);

static cl::opt<bool> PreserveAssemblyUseListOrder(
    "preserve-ll-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM assembly."),
    cl::init(false), cl::Hidden);

static std::unique_ptr<Module> getModule(StringRef InputFilename, LLVMContext &Context,
    StreamingMemoryObject *StreamingObject) {

    std::unique_ptr<Module> M;
    SMDiagnostic Err;
    std::string VerboseBuffer;
    raw_string_ostream VerboseStrm(VerboseBuffer);
    M = NaClParseIRFile(InputFilename, InputFileFormat, Err, &VerboseStrm, Context);
    return std::move(M);
}

// Read the specified bitcode file in and return it. This routine searches the
// link path for the specified file to try to find it...
//
static std::unique_ptr<Module>
loadFile(const char *argv0, const std::string &FN, LLVMContext &Context) {
    SMDiagnostic Err;
    if (Verbose) errs() << "Loading '" << FN << "'\n";
 
    std::unique_ptr<StreamingMemoryObject> StreamingObject;

    std::unique_ptr<Module> Result = getModule(FN, Context, StreamingObject.get());
  
    if (!Result)
        Err.print(argv0, errs());

    Result->materializeMetadata();
    UpgradeDebugInfo(*Result);

    return Result;
}

static void diagnosticHandler(const DiagnosticInfo &DI) {
    unsigned Severity = DI.getSeverity();
    switch (Severity) {
        case DS_Error:
            errs() << "ERROR: ";
            break;
        case DS_Warning:
            if (SuppressWarnings)
                return;
            errs() << "WARNING: ";
            break;
        case DS_Remark:
        case DS_Note:
        llvm_unreachable("Only expecting warnings and errors");
    } 

    DiagnosticPrinterRawOStream DP(errs());
    DI.print(DP);
    errs() << '\n';
}

#define DEBUG

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);

  LLVMContext &Context = getGlobalContext();
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  cl::ParseCommandLineOptions(argc, argv, "pnacl dlink\n");

  // auto Composite = make_unique<Module>("pnacl-dlink", Context);
  auto Composite = loadFile(argv[0], InputFilenames[0], Context);
  DLinker dlinker(Composite.get(), Context, diagnosticHandler);

  for (unsigned i = 0; i < InputFilenames.size(); ++i) {
    std::unique_ptr<Module> M = loadFile(argv[0], InputFilenames[i], Context);
    if (!M.get()) {
      errs() << argv[0] << ": error loading file '" <<InputFilenames[i]<< "'\n";
      return 1;
    }

    if (verifyModule(*M, &errs())) {
      errs() << argv[0] << ": " << InputFilenames[i]
             << ": error: input module is broken!\n";
      return 1;
    }

    if (Verbose) errs() << "Linking in '" << InputFilenames[i] << "'\n";

    if (dlinker.linkPsoRoot(M.get()))
        return 1;
  }

  if (DumpAsm) errs() << "Here's the assembly:\n" << *Composite;

  std::error_code EC;
  tool_output_file Out(OutputFilename, EC, sys::fs::F_None);
  if (EC) {
    errs() << EC.message() << '\n';
    return 1;
  }

  if (verifyModule(*Composite, &errs())) {
    errs() << argv[0] << ": error: linked module is broken!\n";
    return 1;
  }

  if (Verbose) errs() << "Writing bitcode...\n";
  if (OutputAssembly) {
    Composite->print(Out.os(), nullptr, PreserveAssemblyUseListOrder);
  } else if (Force || !CheckBitcodeOutputToConsole(Out.os(), true))
    WriteBitcodeToFile(Composite.get(), Out.os(), PreserveBitcodeUseListOrder);

  // Declare success.
  Out.keep();
    

  return 0;
}
