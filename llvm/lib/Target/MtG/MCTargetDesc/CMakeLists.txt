add_llvm_component_library(LLVMMtGDesc
  MtGInstPrinter.cpp
  MtGMCAsmInfo.cpp
  MtGMCCodeEmitter.cpp
  MtGMCTargetDesc.cpp

  LINK_COMPONENTS
  MC
  MtGInfo
  Support

  ADD_TO_COMPONENT
  MtG
  )
