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
foreach I = 0-11 in
  def R#I  : MtGReg<!add(1,I),  "r"#I>;

def FLAG : MtGReg<12,"flag">;

// general register
def GR : RegisterClass<"MtG", [i32], 32,
   (sequence "R%u", 8, 11)>;

// work register
def WR : RegisterClass<"MtG", [i32], 32,
   (sequence "R%u", 3, 7)>;

// stack pointer
def SP : RegisterClass<"MtG", [i32], 32,
   (add R2) >;

// frame register
def FR : RegisterClass<"MtG", [i32], 32,
   (add R1) >;

