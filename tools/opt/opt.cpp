//===- opt.cpp - The LLVM Modular Optimizer -------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Optimizations may be specified an arbitrary number of times on the command
// line, They are run in the order specified.
//
//===----------------------------------------------------------------------===//

#include "BreakpointPrinter.h"
#include "NewPMDriver.h"
#include "PassPrinters.h"
#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/RegionPass.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/Bitcode/NaCl/NaClBitcodeWriterPass.h" // @LOCALMOD
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassNameParser.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/LinkAllIR.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/MinSFI.h"  // @LOCALMOD
#include "llvm/Transforms/NaCl.h"  // @LOCALMOD
#include <algorithm>
#include <memory>
using namespace llvm;
using namespace opt_tool;

// The OptimizationList is automatically populated with registered Passes by the
// PassNameParser.
//
static cl::list<const PassInfo*, bool, PassNameParser>
PassList(cl::desc("Optimizations available:"));

// This flag specifies a textual description of the optimization pass pipeline
// to run over the module. This flag switches opt to use the new pass manager
// infrastructure, completely disabling all of the flags specific to the old
// pass management.
static cl::opt<std::string> PassPipeline(
    "passes",
    cl::desc("A textual description of the pass pipeline for optimizing"),
    cl::Hidden);

// Other command line options...
//
static cl::opt<std::string>
InputFilename(cl::Positional, cl::desc("<input bitcode file>"),
    cl::init("-"), cl::value_desc("filename"));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Override output filename"),
               cl::value_desc("filename"));

static cl::opt<bool>
Force("f", cl::desc("Enable binary output on terminals"));

static cl::opt<bool>
PrintEachXForm("p", cl::desc("Print module after each transformation"));

static cl::opt<bool>
NoOutput("disable-output",
         cl::desc("Do not write result bitcode file"), cl::Hidden);

static cl::opt<bool>
OutputAssembly("S", cl::desc("Write output as LLVM assembly"));

static cl::opt<bool>
NoVerify("disable-verify", cl::desc("Do not verify result module"), cl::Hidden);

static cl::opt<bool>
VerifyEach("verify-each", cl::desc("Verify after each transform"));

static cl::opt<bool>
StripDebug("strip-debug",
           cl::desc("Strip debugger symbol info from translation unit"));

static cl::opt<bool>
DisableInline("disable-inlining", cl::desc("Do not run the inliner pass"));

static cl::opt<bool>
DisableOptimizations("disable-opt",
                     cl::desc("Do not run any optimization passes"));

static cl::opt<bool>
StandardLinkOpts("std-link-opts",
                 cl::desc("Include the standard link time optimizations"));

static cl::opt<bool>
OptLevelO1("O1",
           cl::desc("Optimization level 1. Similar to clang -O1"));

static cl::opt<bool>
OptLevelO2("O2",
           cl::desc("Optimization level 2. Similar to clang -O2"));

static cl::opt<bool>
OptLevelOs("Os",
           cl::desc("Like -O2 with extra optimizations for size. Similar to clang -Os"));

static cl::opt<bool>
OptLevelOz("Oz",
           cl::desc("Like -Os but reduces code size further. Similar to clang -Oz"));

static cl::opt<bool>
OptLevelO3("O3",
           cl::desc("Optimization level 3. Similar to clang -O3"));

// @LOCALMOD-BEGIN
static cl::opt<bool>
PNaClABISimplifyPreOpt(
    "pnacl-abi-simplify-preopt",
    cl::desc("PNaCl ABI simplifications for before optimizations"));

static cl::opt<bool>
PNaClABISimplifyPostOpt(
    "pnacl-abi-simplify-postopt",
    cl::desc("PNaCl ABI simplifications for after optimizations"));

static cl::opt<bool>
MinSFI("minsfi",
       cl::desc("MinSFI sandboxing"));
// @LOCALMOD-END

// PNaCl Dynamic Linking
static cl::opt<bool>
PNaClPsoRoot(
    "pnacl-pso-root",
    cl::desc("PNaCl PSO ROOT"));

static cl::opt<std::string>
TargetTriple("mtriple", cl::desc("Override target triple for module"));

static cl::opt<bool>
UnitAtATime("funit-at-a-time",
            cl::desc("Enable IPO. This corresponds to gcc's -funit-at-a-time"),
            cl::init(true));

static cl::opt<bool>
DisableLoopUnrolling("disable-loop-unrolling",
                     cl::desc("Disable loop unrolling in all relevant passes"),
                     cl::init(false));
static cl::opt<bool>
DisableLoopVectorization("disable-loop-vectorization",
                     cl::desc("Disable the loop vectorization pass"),
                     cl::init(false));

static cl::opt<bool>
DisableSLPVectorization("disable-slp-vectorization",
                        cl::desc("Disable the slp vectorization pass"),
                        cl::init(false));


static cl::opt<bool>
DisableSimplifyLibCalls("disable-simplify-libcalls",
                        cl::desc("Disable simplify-libcalls"));

static cl::opt<bool>
Quiet("q", cl::desc("Obsolete option"), cl::Hidden);

static cl::alias
QuietA("quiet", cl::desc("Alias for -q"), cl::aliasopt(Quiet));

static cl::opt<bool>
AnalyzeOnly("analyze", cl::desc("Only perform analysis, no optimization"));

static cl::opt<bool>
PrintBreakpoints("print-breakpoints-for-testing",
                 cl::desc("Print select breakpoints location for testing"));

static cl::opt<std::string>
DefaultDataLayout("default-data-layout",
          cl::desc("data layout string to use if not specified by module"),
          cl::value_desc("layout-string"), cl::init(""));

static cl::opt<bool> PreserveBitcodeUseListOrder(
    "preserve-bc-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM bitcode."),
    cl::init(true), cl::Hidden);
// @LOCALMOD-BEGIN
static cl::opt<NaClFileFormat>
OutputFileFormat(
    "bitcode-format",
    cl::desc("Define format of generated bitcode file:"),
    cl::values(
        clEnumValN(LLVMFormat, "llvm", "LLVM bitcode file (default)"),
        clEnumValN(PNaClFormat, "pnacl", "PNaCl bitcode file"),
        clEnumValEnd),
    cl::init(LLVMFormat));
extern bool OutputFileFormatIsPNaCl;
// @LOCALMOD-END

static cl::opt<bool> PreserveAssemblyUseListOrder(
    "preserve-ll-uselistorder",
    cl::desc("Preserve use-list order when writing LLVM assembly."),
    cl::init(false), cl::Hidden);

static inline void addPass(legacy::PassManagerBase &PM, Pass *P) {
  // Add the pass to the pass manager...
  PM.add(P);

  // If we are verifying all of the intermediate steps, add the verifier...
  if (VerifyEach)
    PM.add(createVerifierPass());
}

/// This routine adds optimization passes based on selected optimization level,
/// OptLevel.
///
/// OptLevel - Optimization Level
static void AddOptimizationPasses(legacy::PassManagerBase &MPM,
                                  legacy::FunctionPassManager &FPM,
                                  unsigned OptLevel, unsigned SizeLevel) {
  FPM.add(createVerifierPass()); // Verify that input is correct

  PassManagerBuilder Builder;
  Builder.OptLevel = OptLevel;
  Builder.SizeLevel = SizeLevel;

  if (DisableInline) {
    // No inlining pass
  } else if (OptLevel > 1) {
    Builder.Inliner = createFunctionInliningPass(OptLevel, SizeLevel);
  } else {
    Builder.Inliner = createAlwaysInlinerPass();
  }
  Builder.DisableUnitAtATime = !UnitAtATime;
  Builder.DisableUnrollLoops = (DisableLoopUnrolling.getNumOccurrences() > 0) ?
                               DisableLoopUnrolling : OptLevel == 0;

  // This is final, unless there is a #pragma vectorize enable
  if (DisableLoopVectorization)
    Builder.LoopVectorize = false;
  // If option wasn't forced via cmd line (-vectorize-loops, -loop-vectorize)
  else if (!Builder.LoopVectorize)
    Builder.LoopVectorize = OptLevel > 1 && SizeLevel < 2;

  // When #pragma vectorize is on for SLP, do the same as above
  Builder.SLPVectorize =
      DisableSLPVectorization ? false : OptLevel > 1 && SizeLevel < 2;

  Builder.populateFunctionPassManager(FPM);
  Builder.populateModulePassManager(MPM);
}

static void AddStandardLinkPasses(legacy::PassManagerBase &PM) {
  PassManagerBuilder Builder;
  Builder.VerifyInput = true;
  if (DisableOptimizations)
    Builder.OptLevel = 0;

  if (!DisableInline)
    Builder.Inliner = createFunctionInliningPass();
  Builder.populateLTOPassManager(PM);
}

//===----------------------------------------------------------------------===//
// CodeGen-related helper functions.
//

static CodeGenOpt::Level GetCodeGenOptLevel() {
  if (OptLevelO1)
    return CodeGenOpt::Less;
  if (OptLevelO2)
    return CodeGenOpt::Default;
  if (OptLevelO3)
    return CodeGenOpt::Aggressive;
  return CodeGenOpt::None;
}

// Returns the TargetMachine instance or zero if no triple is provided.
static TargetMachine* GetTargetMachine(Triple TheTriple) {
  std::string Error;
  // @LOCALMOD-BEGIN: Some optimization passes like SimplifyCFG do nice
  // things for code size, but only do it if the TTI says it is okay.
  // For now, use the ARM TTI for LE32 until we have an LE32 TTI.
  // https://code.google.com/p/nativeclient/issues/detail?id=2554
  if (TheTriple.getArch() == Triple::le32) {
    TheTriple.setArchName("armv7a");
  }
  // @LOCALMOD-END
  const Target *TheTarget = TargetRegistry::lookupTarget(MArch, TheTriple,
                                                         Error);
  // Some modules don't specify a triple, and this is okay.
  if (!TheTarget) {
    return nullptr;
  }

  // Package up features to be passed to target/subtarget
  std::string FeaturesStr;
  if (MAttrs.size() || MCPU == "native") {
    SubtargetFeatures Features;

    // If user asked for the 'native' CPU, we need to autodetect features.
    // This is necessary for x86 where the CPU might not support all the
    // features the autodetected CPU name lists in the target. For example,
    // not all Sandybridge processors support AVX.
    if (MCPU == "native") {
      StringMap<bool> HostFeatures;
      if (sys::getHostCPUFeatures(HostFeatures))
        for (auto &F : HostFeatures)
          Features.AddFeature(F.first(), F.second);
    }

    for (unsigned i = 0; i != MAttrs.size(); ++i)
      Features.AddFeature(MAttrs[i]);
    FeaturesStr = Features.getString();
  }

  if (MCPU == "native")
    MCPU = sys::getHostCPUName();

  return TheTarget->createTargetMachine(TheTriple.getTriple(),
                                        MCPU, FeaturesStr,
                                        InitTargetOptionsFromCodeGenFlags(),
                                        RelocModel, CMModel,
                                        GetCodeGenOptLevel());
}

#ifdef LINK_POLLY_INTO_TOOLS
namespace polly {
void initializePollyPasses(llvm::PassRegistry &Registry);
}
#endif

//===----------------------------------------------------------------------===//
// main for opt
//
int main(int argc, char **argv) {
  sys::PrintStackTraceOnErrorSignal();
  llvm::PrettyStackTraceProgram X(argc, argv);

  // Enable debug stream buffering.
  EnableDebugBuffering = true;

  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  LLVMContext &Context = getGlobalContext();

  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();

  // Initialize passes
  PassRegistry &Registry = *PassRegistry::getPassRegistry();
  initializeCore(Registry);
  initializeScalarOpts(Registry);
  initializeObjCARCOpts(Registry);
  initializeVectorization(Registry);
  initializeIPO(Registry);
  initializeAnalysis(Registry);
  initializeIPA(Registry);
  initializeTransformUtils(Registry);
  initializeInstCombine(Registry);
  initializeInstrumentation(Registry);
  initializeTarget(Registry);
  // For codegen passes, only passes that do IR to IR transformation are
  // supported.
  initializeCodeGenPreparePass(Registry);
  initializeAtomicExpandPass(Registry);
  initializeRewriteSymbolsPass(Registry);
  initializeWinEHPreparePass(Registry);
  initializeDwarfEHPreparePass(Registry);

#ifdef LINK_POLLY_INTO_TOOLS
  polly::initializePollyPasses(Registry);
#endif

  // @LOCALMOD-BEGIN
  initializeAddPNaClExternalDeclsPass(Registry);
  initializeAllocateDataSegmentPass(Registry);
  initializeBackendCanonicalizePass(Registry);
  initializeCanonicalizeMemIntrinsicsPass(Registry);
  initializeCleanupUsedGlobalsMetadataPass(Registry);
  initializeConstantInsertExtractElementIndexPass(Registry);
  initializeExpandAllocasPass(Registry);
  initializeExpandArithWithOverflowPass(Registry);
  initializeExpandByValPass(Registry);
  initializeExpandConstantExprPass(Registry);
  initializeExpandCtorsPass(Registry);
  initializeExpandGetElementPtrPass(Registry);
  initializeExpandIndirectBrPass(Registry);
  initializeExpandLargeIntegersPass(Registry);
  initializeExpandShuffleVectorPass(Registry);
  initializeExpandSmallArgumentsPass(Registry);
  initializeExpandStructRegsPass(Registry);
  initializeExpandTlsConstantExprPass(Registry);
  initializeExpandTlsPass(Registry);
  initializeExpandVarArgsPass(Registry);
  initializeFixVectorLoadStoreAlignmentPass(Registry);
  initializeFlattenGlobalsPass(Registry);
  initializeGlobalCleanupPass(Registry);
  initializeGlobalizeConstantVectorsPass(Registry);
  initializeInsertDivideCheckPass(Registry);
  initializeInternalizeUsedGlobalsPass(Registry);
  initializeNormalizeAlignmentPass(Registry);
  initializePNaClABIVerifyFunctionsPass(Registry);
  initializePNaClABIVerifyModulePass(Registry);
  initializePNaClSjLjEHPass(Registry);
  initializePromoteI1OpsPass(Registry);
  initializePromoteIntegersPass(Registry);
  initializeRemoveAsmMemoryPass(Registry);
  initializeRenameEntryPointPass(Registry);
  initializeReplacePtrsWithIntsPass(Registry);
  initializeResolveAliasesPass(Registry);
  initializeResolvePNaClIntrinsicsPass(Registry);
  initializeRewriteAtomicsPass(Registry);
  initializeRewriteLLVMIntrinsicsPass(Registry);
  initializeRewritePNaClLibraryCallsPass(Registry);
  initializeSandboxIndirectCallsPass(Registry);
  initializeSandboxMemoryAccessesPass(Registry);
  initializeSimplifyAllocasPass(Registry);
  initializeSimplifyStructRegSignaturesPass(Registry);
  initializeStripAttributesPass(Registry);
  initializeStripMetadataPass(Registry);
  initializeStripModuleFlagsPass(Registry);
  initializeStripTlsPass(Registry);
  initializeSubstituteUndefsPass(Registry);
  // Emscripten passes:
  initializeExpandI64Pass(Registry);
  initializeExpandInsertExtractElementPass(Registry);
  initializeLowerEmAsyncifyPass(Registry);
  initializeLowerEmExceptionsPass(Registry);
  initializeLowerEmSetjmpPass(Registry);
  initializeNoExitRuntimePass(Registry);
  // Emscripten passes end.
  // @LOCALMOD-END
  
  // PNaCl Dynamic Linking
  initializePNaClPsoRootPass(Registry);


  cl::ParseCommandLineOptions(argc, argv,
    "llvm .bc -> .bc modular optimizer and analysis printer\n");

  if (AnalyzeOnly && NoOutput) {
    errs() << argv[0] << ": analyze mode conflicts with no-output mode.\n";
    return 1;
  }

  SMDiagnostic Err;

  // Load the input module...
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);

  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  // Strip debug info before running the verifier.
  if (StripDebug)
    StripDebugInfo(*M);

  // Immediately run the verifier to catch any problems before starting up the
  // pass pipelines.  Otherwise we can crash on broken code during
  // doInitialization().
  if (!NoVerify && verifyModule(*M, &errs())) {
    errs() << argv[0] << ": " << InputFilename
           << ": error: input module is broken!\n";
    return 1;
  }

  // If we are supposed to override the target triple, do so now.
  if (!TargetTriple.empty())
    M->setTargetTriple(Triple::normalize(TargetTriple));

  // Figure out what stream we are supposed to write to...
  std::unique_ptr<tool_output_file> Out;
  if (NoOutput) {
    if (!OutputFilename.empty())
      errs() << "WARNING: The -o (output filename) option is ignored when\n"
                "the --disable-output option is used.\n";
  } else {
    // Default to standard output.
    if (OutputFilename.empty())
      OutputFilename = "-";

    std::error_code EC;
    Out.reset(new tool_output_file(OutputFilename, EC, sys::fs::F_None));
    if (EC) {
      errs() << EC.message() << '\n';
      return 1;
    }
  }

  Triple ModuleTriple(M->getTargetTriple());
  TargetMachine *Machine = nullptr;
  if (ModuleTriple.getArch())
    Machine = GetTargetMachine(ModuleTriple);
  std::unique_ptr<TargetMachine> TM(Machine);

  // If the output is set to be emitted to standard out, and standard out is a
  // console, print out a warning message and refuse to do it.  We don't
  // impress anyone by spewing tons of binary goo to a terminal.
  if (!Force && !NoOutput && !AnalyzeOnly && !OutputAssembly)
    if (CheckBitcodeOutputToConsole(Out->os(), !Quiet))
      NoOutput = true;

  if (PassPipeline.getNumOccurrences() > 0) {
    OutputKind OK = OK_NoOutput;
    if (!NoOutput)
      OK = OutputAssembly ? OK_OutputAssembly : OK_OutputBitcode;

    VerifierKind VK = VK_VerifyInAndOut;
    if (NoVerify)
      VK = VK_NoVerifier;
    else if (VerifyEach)
      VK = VK_VerifyEachPass;

    // The user has asked to use the new pass manager and provided a pipeline
    // string. Hand off the rest of the functionality to the new code for that
    // layer.
    return runPassPipeline(argv[0], Context, *M, TM.get(), Out.get(),
                           PassPipeline, OK, VK, PreserveAssemblyUseListOrder,
                           PreserveBitcodeUseListOrder)
               ? 0
               : 1;
  }

  // Create a PassManager to hold and optimize the collection of passes we are
  // about to build.
  //
  legacy::PassManager Passes;

  // Add an appropriate TargetLibraryInfo pass for the module's triple.
  TargetLibraryInfoImpl TLII(ModuleTriple);

  // The -disable-simplify-libcalls flag actually disables all builtin optzns.
  if (DisableSimplifyLibCalls)
    TLII.disableAllFunctions();
  Passes.add(new TargetLibraryInfoWrapperPass(TLII));

  // Add an appropriate DataLayout instance for this module.
  const DataLayout &DL = M->getDataLayout();
  if (DL.isDefault() && !DefaultDataLayout.empty()) {
    M->setDataLayout(DefaultDataLayout);
  }

  // Add internal analysis passes from the target machine.
  Passes.add(createTargetTransformInfoWrapperPass(TM ? TM->getTargetIRAnalysis()
                                                     : TargetIRAnalysis()));

  std::unique_ptr<legacy::FunctionPassManager> FPasses;
  if (OptLevelO1 || OptLevelO2 || OptLevelOs || OptLevelOz || OptLevelO3) {
    FPasses.reset(new legacy::FunctionPassManager(M.get()));
    FPasses->add(createTargetTransformInfoWrapperPass(
        TM ? TM->getTargetIRAnalysis() : TargetIRAnalysis()));
  }

  if (PrintBreakpoints) {
    // Default to standard output.
    if (!Out) {
      if (OutputFilename.empty())
        OutputFilename = "-";

      std::error_code EC;
      Out = llvm::make_unique<tool_output_file>(OutputFilename, EC,
                                                sys::fs::F_None);
      if (EC) {
        errs() << EC.message() << '\n';
        return 1;
      }
    }
    Passes.add(createBreakpointPrinter(Out->os()));
    NoOutput = true;
  }

  // Create a new optimization pass for each one specified on the command line
  for (unsigned i = 0; i < PassList.size(); ++i) {
    // PNaCl Dynamic Linking
    if (PNaClPsoRoot &&
        PNaClPsoRoot.getPosition() < PassList.getPosition(i)) {
      PNaClDynamicLinkingPasses(&ModuleTriple, Passes);
      PNaClPsoRoot = false;
      PNaClABISimplifyPostOpt = true;
    }

    // @LOCALMOD-BEGIN
    if (PNaClABISimplifyPreOpt &&
        PNaClABISimplifyPreOpt.getPosition() < PassList.getPosition(i)) {
      PNaClABISimplifyAddPreOptPasses(&ModuleTriple, Passes);
      PNaClABISimplifyPreOpt = false;
    }
    // @LOCALMOD-END

    if (StandardLinkOpts &&
        StandardLinkOpts.getPosition() < PassList.getPosition(i)) {
      AddStandardLinkPasses(Passes);
      StandardLinkOpts = false;
    }

    if (OptLevelO1 && OptLevelO1.getPosition() < PassList.getPosition(i)) {
      AddOptimizationPasses(Passes, *FPasses, 1, 0);
      OptLevelO1 = false;
    }

    if (OptLevelO2 && OptLevelO2.getPosition() < PassList.getPosition(i)) {
      AddOptimizationPasses(Passes, *FPasses, 2, 0);
      OptLevelO2 = false;
    }

    if (OptLevelOs && OptLevelOs.getPosition() < PassList.getPosition(i)) {
      AddOptimizationPasses(Passes, *FPasses, 2, 1);
      OptLevelOs = false;
    }

    if (OptLevelOz && OptLevelOz.getPosition() < PassList.getPosition(i)) {
      AddOptimizationPasses(Passes, *FPasses, 2, 2);
      OptLevelOz = false;
    }

    if (OptLevelO3 && OptLevelO3.getPosition() < PassList.getPosition(i)) {
      AddOptimizationPasses(Passes, *FPasses, 3, 0);
      OptLevelO3 = false;
    }
  
    // @LOCALMOD-BEGIN
    if (PNaClABISimplifyPostOpt &&
        PNaClABISimplifyPostOpt.getPosition() < PassList.getPosition(i)) {
      PNaClABISimplifyAddPostOptPasses(&ModuleTriple, Passes);
      PNaClABISimplifyPostOpt = false;
    }

    if (MinSFI && MinSFI.getPosition() < PassList.getPosition(i)) {
      MinSFIPasses(Passes);
      MinSFI = false;
    }
    // @LOCALMOD-END

    const PassInfo *PassInf = PassList[i];
    Pass *P = nullptr;
    if (PassInf->getTargetMachineCtor())
      P = PassInf->getTargetMachineCtor()(TM.get());
    else if (PassInf->getNormalCtor())
      P = PassInf->getNormalCtor()();
    else
      errs() << argv[0] << ": cannot create pass: "
             << PassInf->getPassName() << "\n";
    if (P) {
      PassKind Kind = P->getPassKind();
      addPass(Passes, P);

      if (AnalyzeOnly) {
        switch (Kind) {
        case PT_BasicBlock:
          Passes.add(createBasicBlockPassPrinter(PassInf, Out->os(), Quiet));
          break;
        case PT_Region:
          Passes.add(createRegionPassPrinter(PassInf, Out->os(), Quiet));
          break;
        case PT_Loop:
          Passes.add(createLoopPassPrinter(PassInf, Out->os(), Quiet));
          break;
        case PT_Function:
          Passes.add(createFunctionPassPrinter(PassInf, Out->os(), Quiet));
          break;
        case PT_CallGraphSCC:
          Passes.add(createCallGraphPassPrinter(PassInf, Out->os(), Quiet));
          break;
        default:
          Passes.add(createModulePassPrinter(PassInf, Out->os(), Quiet));
          break;
        }
      }
    }

    if (PrintEachXForm)
      Passes.add(
          createPrintModulePass(errs(), "", PreserveAssemblyUseListOrder));
  }

  //PNaCl Dynamic Linking
  if (PNaClPsoRoot)
    PNaClDynamicLinkingPasses(&ModuleTriple, Passes);

  // @LOCALMOD-BEGIN
  if (PNaClABISimplifyPreOpt)
    PNaClABISimplifyAddPreOptPasses(&ModuleTriple, Passes);
  // @LOCALMOD-END

  if (StandardLinkOpts) {
    AddStandardLinkPasses(Passes);
    StandardLinkOpts = false;
  }

  if (OptLevelO1)
    AddOptimizationPasses(Passes, *FPasses, 1, 0);

  if (OptLevelO2)
    AddOptimizationPasses(Passes, *FPasses, 2, 0);

  if (OptLevelOs)
    AddOptimizationPasses(Passes, *FPasses, 2, 1);

  if (OptLevelOz)
    AddOptimizationPasses(Passes, *FPasses, 2, 2);

  if (OptLevelO3)
    AddOptimizationPasses(Passes, *FPasses, 3, 0);

  if (OptLevelO1 || OptLevelO2 || OptLevelOs || OptLevelOz || OptLevelO3) {
    FPasses->doInitialization();
    for (Function &F : *M)
      FPasses->run(F);
    FPasses->doFinalization();
  }

  // @LOCALMOD-BEGIN
  if (PNaClABISimplifyPostOpt)
    PNaClABISimplifyAddPostOptPasses(&ModuleTriple, Passes);

  if (MinSFI)
     MinSFIPasses(Passes);
  // @LOCALMOD-END

  // Check that the module is well formed on completion of optimization
  if (!NoVerify && !VerifyEach)
    Passes.add(createVerifierPass());

  // Write bitcode or assembly to the output as the last step...
  if (!NoOutput && !AnalyzeOnly) {
    if (OutputAssembly)
      Passes.add(
          createPrintModulePass(Out->os(), "", PreserveAssemblyUseListOrder));
    else
      // @LOCALMOD-START
      switch (OutputFileFormat) {
      case LLVMFormat:
        Passes.add(createBitcodeWriterPass(Out->os(), PreserveBitcodeUseListOrder));
        break;
      case PNaClFormat:
        Passes.add(createNaClBitcodeWriterPass(Out->os()));
        break;
      case AutodetectFileFormat:
        report_fatal_error("Command can't autodetect file format!");
        break;
      }
      // @LOCALMOD-END
  }

  // Before executing passes, print the final values of the LLVM options.
  cl::PrintOptionValues();

  // Now that we have all of the passes ready, run them.
  Passes.run(*M);

  // Declare success.
  if (!NoOutput || PrintBreakpoints)
    Out->keep();

  return 0;
}
