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

class MtGReg<bits<32> num, string n> : Register<n> {
  field bits<32> Num = num;
  let Namespace = "MtG";
}

//===----------------------------------------------------------------------===//
//  Registers
//===----------------------------------------------------------------------===//


// General Register
foreach I = 0-12 in
  def R#I  : MtGReg<!add(1,I),  "r"#I>;

def FLAG : MtGReg<13,"flag">;

// special register
// def SR : RegisterClass<"MtG", [i32], 32,
//    (add R0) >;

// general register
def GR : RegisterClass<"MtG", [i32], 32,
   (sequence "R%u", 8, 11)>;

// work register
// def WR : RegisterClass<"MtG", [i32], 32,
//    (add R9, R10) >;

// stack pointer
def SP : RegisterClass<"MtG", [i32], 32,
   (add R7) >;


// frame register
def FR : RegisterClass<"MtG", [i32], 32,
   (add R6) >;

// def FLAGR :  RegisterClass<"MtG", [i1], 1,
//    (add FLAG) >;
