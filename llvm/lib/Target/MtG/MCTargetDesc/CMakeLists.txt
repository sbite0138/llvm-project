add_llvm_component_library(LLVMMtGDesc
  MtGAsmBackend.cpp
  MtGELFObjectWriter.cpp
  MtGELFStreamer.cpp
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
