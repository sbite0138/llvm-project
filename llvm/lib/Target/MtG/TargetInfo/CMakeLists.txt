add_llvm_component_library(LLVMMtGInfo
  MtGTargetInfo.cpp

  LINK_COMPONENTS
  MC
  Support

  ADD_TO_COMPONENT
  MtG
  )
