set(LLVM_LINK_COMPONENTS
  BitWriter
  Core
  IRReader
  DLinker
  NaClBitWriter # @LOCALMOD
  Support
  )

add_llvm_tool(pnacl-dlink
  pnacl-dlink.cpp
  )
