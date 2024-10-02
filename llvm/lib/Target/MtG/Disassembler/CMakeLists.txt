add_llvm_component_library(LLVMMtGDisassembler
  MtGDisassembler.cpp

  LINK_COMPONENTS
  MCDisassembler
  MC
  MtGInfo
  Support

  ADD_TO_COMPONENT
  MtG
  )
