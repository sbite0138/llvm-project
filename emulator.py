
class Instruction:
    def __init__(self,opcode,op1,op2):
        self.opcode=opcode
        self.op1=op1
        self.op2=op2
    def __str__(self):
        return f"{self.opcode} {self.op1} {self.op2}"
    def __repr__(self):
        return str(self)

def parseReg(reg:str):
    assert(reg[0]=='r')
    if reg[-1]==',':
        return reg[:-1]
    return reg

def parseImm(imm:str):
    assert(imm[0]=='#')
    if imm[-1]==',':
        return int(imm[1:-1])
    return int(imm[1:])
    


OPCODES=["NUMBUILD","SUB","ADD","MOV","LOAD","STORE","ret"]
def parse(line:str):
    split=line.split()
    if split[0] not in OPCODES:
        print("skip ",     split[0])
        return None
    op1=None
    op2=None
    if split[0]=="NUMBUILD":
        op1=parseReg(split[1])
        op2=parseImm(split[2])
    elif split[0]!='ret':
        op1=parseReg(split[1])
        op2=parseReg(split[2])
    return Instruction(split[0],op1,op2)

def execute(insts, memoryMap, registerMap):
    pc=0
    while pc<len(insts):
        print("--------------------")
        print(registerMap)
        print(memoryMap)

        inst=insts[pc]
        print(f"line {pc}: {inst.opcode} {inst.op1} {inst.op2}",end=" ")
        if inst.op1!=None and isinstance(inst.op1,str):
            print(f"register {inst.op1}={registerMap[inst.op1]}",end=" ")
        if inst.op2!=None and isinstance(inst.op2,str):
            print(f"register {inst.op2}={registerMap[inst.op2]}",end=" ")
        print()
        
        if inst.opcode=="NUMBUILD":
            assert(inst.op1=="r0")
            isContinue=False
            if pc>0 and insts[pc-1].opcode=="NUMBUILD":
                isContinue=True
            if isContinue:
                assert(registerMap["r0"]!=None)
                registerMap["r0"]*=12
                registerMap["r0"]+=inst.op2
            else:
                registerMap["r0"]=inst.op2
        elif inst.opcode=="ADD":
            assert(registerMap[inst.op1]!=None)
            assert(registerMap[inst.op2]!=None)
            assert(isinstance(inst.op1,str))
            assert(isinstance(inst.op2,str))
            registerMap[inst.op1]+=registerMap[inst.op2]
        elif inst.opcode=="SUB":
            assert(registerMap[inst.op1]!=None)
            assert(registerMap[inst.op2]!=None)
            assert(isinstance(inst.op1,str))
            assert(isinstance(inst.op2,str))
            registerMap[inst.op1]-=registerMap[inst.op2]
            assert(registerMap[inst.op1]>=0)
        elif inst.opcode=="MOV":
            assert(registerMap[inst.op2]!=None)
            assert(isinstance(inst.op1,str))
            assert(isinstance(inst.op2,str))
            registerMap[inst.op1]=registerMap[inst.op2]
        elif inst.opcode=="LOAD":
            assert(registerMap[inst.op2]!=None)
            assert(memoryMap[registerMap[inst.op2]]!=None)
            assert(isinstance(inst.op1,str))
            assert(isinstance(inst.op2,str))
            registerMap[inst.op1]=memoryMap[registerMap[inst.op2]]
        elif inst.opcode=="STORE":
            assert(registerMap[inst.op1]!=None)
            assert(registerMap[inst.op2]!=None)
            assert(isinstance(inst.op1,str))
            assert(isinstance(inst.op2,str))
            memoryMap[registerMap[inst.op2]]=registerMap[inst.op1]
        elif inst.opcode=="ret":
            break
        pc+=1

import sys
from collections import defaultdict
def main():
    insts=[]
    # read from stdin
    for line in sys.stdin:
        inst=parse(line)
        if inst!=None:
            insts.append(inst)
    print(insts)
    memoryMap=defaultdict(lambda:None)
    registerMap=defaultdict(lambda:None)
    registerMap['r1']=10**0
    registerMap['r2']=10**1
    registerMap['r3']=10**2
    registerMap['r4']=10**3
    registerMap['r5']=10**4
    registerMap['r6']=10**5
    registerMap['r7']=10**6
    registerMap['r8']=12
    registerMap['r11']=0
    memoryMap[12]=10**7
    memoryMap[16]=10**8
    # memoryMap[28]=10**9
    execute(insts,memoryMap,registerMap)
    print(registerMap)
    print(memoryMap)
main() 