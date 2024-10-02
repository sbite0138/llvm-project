//===-- MtGRegisterInfo.td - MtG Register defs -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//  Declarations that describe the MtG register file
//===----------------------------------------------------------------------===//

class MtGReg<bits<4> num, string n, list<string> alt = []> : Register<n> {
  field bits<4> Num = num;
  let Namespace = "MtG";
  let HWEncoding{3-0} = num;
  let AltNames = alt;
  let DwarfNumbers = [num];
}

class MtGRegWithSubregs<bits<4> num, string n, list<Register> subregs,
                           list<string> alt = []> 
  : RegisterWithSubRegs<n, subregs> {
  field bits<4> Num = num;
  let Namespace = "MtG";
  let HWEncoding{3-0} = num;
  let AltNames = alt;
  let DwarfNumbers = [num];
}

//===----------------------------------------------------------------------===//
//  Registers
//===----------------------------------------------------------------------===//

def PCB  : MtGReg<0,  "r0", ["pc"]>, DwarfRegNum<[16]>;
def SPB  : MtGReg<1,  "r1", ["sp"]>, DwarfRegNum<[17]>;
def SRB  : MtGReg<2,  "r2", ["sr"]>, DwarfRegNum<[18]>;
def CGB  : MtGReg<3,  "r3", ["cg"]>, DwarfRegNum<[19]>;
def R4B  : MtGReg<4,  "r4", ["fp"]>, DwarfRegNum<[20]>;
def R5B  : MtGReg<5,  "r5">,  DwarfRegNum<[21]>;
def R6B  : MtGReg<6,  "r6">,  DwarfRegNum<[22]>;
def R7B  : MtGReg<7,  "r7">,  DwarfRegNum<[23]>;
def R8B  : MtGReg<8,  "r8">,  DwarfRegNum<[24]>;
def R9B  : MtGReg<9,  "r9">,  DwarfRegNum<[25]>;
def R10B : MtGReg<10, "r10">, DwarfRegNum<[26]>;
def R11B : MtGReg<11, "r11">, DwarfRegNum<[27]>;
def R12B : MtGReg<12, "r12">, DwarfRegNum<[28]>;
def R13B : MtGReg<13, "r13">, DwarfRegNum<[29]>;
def R14B : MtGReg<14, "r14">, DwarfRegNum<[30]>;
def R15B : MtGReg<15, "r15">, DwarfRegNum<[31]>;

def subreg_8bit : SubRegIndex<8> { let Namespace = "MtG"; }

let SubRegIndices = [subreg_8bit] in {
def PC  : MtGRegWithSubregs<0,  "r0",  [PCB], ["pc"]>, DwarfRegNum<[0]>;
def SP  : MtGRegWithSubregs<1,  "r1",  [SPB], ["sp"]>, DwarfRegNum<[1]>;
def SR  : MtGRegWithSubregs<2,  "r2",  [SRB], ["sr"]>, DwarfRegNum<[2]>;
def CG  : MtGRegWithSubregs<3,  "r3",  [CGB], ["cg"]>, DwarfRegNum<[3]>;
def R4  : MtGRegWithSubregs<4,  "r4",  [R4B], ["fp"]>, DwarfRegNum<[4]>;
def R5  : MtGRegWithSubregs<5,  "r5",  [R5B]>,  DwarfRegNum<[5]>;
def R6  : MtGRegWithSubregs<6,  "r6",  [R6B]>,  DwarfRegNum<[6]>;
def R7  : MtGRegWithSubregs<7,  "r7",  [R7B]>,  DwarfRegNum<[7]>;
def R8  : MtGRegWithSubregs<8,  "r8",  [R8B]>,  DwarfRegNum<[8]>;
def R9  : MtGRegWithSubregs<9,  "r9",  [R9B]>,  DwarfRegNum<[9]>;
def R10 : MtGRegWithSubregs<10, "r10", [R10B]>, DwarfRegNum<[10]>;
def R11 : MtGRegWithSubregs<11, "r11", [R11B]>, DwarfRegNum<[11]>;
def R12 : MtGRegWithSubregs<12, "r12", [R12B]>, DwarfRegNum<[12]>;
def R13 : MtGRegWithSubregs<13, "r13", [R13B]>, DwarfRegNum<[13]>;
def R14 : MtGRegWithSubregs<14, "r14", [R14B]>, DwarfRegNum<[14]>;
def R15 : MtGRegWithSubregs<15, "r15", [R15B]>, DwarfRegNum<[15]>;
}

def GR8 : RegisterClass<"MtG", [i8], 8,
   // Volatile registers
  (add R12B, R13B, R14B, R15B, R11B, R10B, R9B, R8B, R7B, R6B, R5B,
   // Frame pointer, sometimes allocable
   R4B,
   // Volatile, but not allocable
   PCB, SPB, SRB, CGB)>;

def GR16 : RegisterClass<"MtG", [i16], 16,
   // Volatile registers
  (add R12, R13, R14, R15, R11, R10, R9, R8, R7, R6, R5,
   // Frame pointer, sometimes allocable
   R4,
   // Volatile, but not allocable
   PC, SP, SR, CG)>;
