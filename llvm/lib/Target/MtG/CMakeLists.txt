add_llvm_component_group(MtG)

set(LLVM_TARGET_DEFINITIONS MtG.td)

tablegen(LLVM MtGGenAsmMatcher.inc -gen-asm-matcher)
tablegen(LLVM MtGGenAsmWriter.inc -gen-asm-writer)
tablegen(LLVM MtGGenCallingConv.inc -gen-callingconv)
tablegen(LLVM MtGGenDAGISel.inc -gen-dag-isel)
tablegen(LLVM MtGGenDisassemblerTables.inc -gen-disassembler)
tablegen(LLVM MtGGenInstrInfo.inc -gen-instr-info)
tablegen(LLVM MtGGenMCCodeEmitter.inc -gen-emitter)
tablegen(LLVM MtGGenRegisterInfo.inc -gen-register-info)
tablegen(LLVM MtGGenSubtargetInfo.inc -gen-subtarget)

add_public_tablegen_target(MtGCommonTableGen)

add_llvm_target(MtGCodeGen
  MtGBranchSelector.cpp
  MtGISelDAGToDAG.cpp
  MtGISelLowering.cpp
  MtGInstrInfo.cpp
  MtGFrameLowering.cpp
  MtGMachineFunctionInfo.cpp
  MtGRegisterInfo.cpp
  MtGSubtarget.cpp
  MtGTargetMachine.cpp
  MtGAsmPrinter.cpp
  MtGMCInstLower.cpp

  LINK_COMPONENTS
  AsmPrinter
  CodeGen
  CodeGenTypes
  Core
  MC
  MtGDesc
  MtGInfo
  SelectionDAG
  Support
  Target

  ADD_TO_COMPONENT
  MtG
  )

add_subdirectory(MCTargetDesc)
add_subdirectory(TargetInfo)
add_subdirectory(AsmParser)
add_subdirectory(Disassembler)
