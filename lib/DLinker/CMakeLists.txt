add_llvm_library(LLVMDLinker
    DLinker.cpp

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/DLinker
  )
