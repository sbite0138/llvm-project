add_llvm_component_library(LLVMMtGAsmParser
  MtGAsmParser.cpp

  LINK_COMPONENTS
  CodeGenTypes
  MC
  MCParser
  MtGDesc
  MtGInfo
  Support

  ADD_TO_COMPONENT
  MtG
  )
