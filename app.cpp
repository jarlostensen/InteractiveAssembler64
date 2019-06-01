#include <iostream>
#include "inasm64/runtime.h"

void PrintContext() {
  const auto ctx = inasm64::Context();
  std::cout << "rax = 0x" << std::hex << ctx->Rax << "\trip = 0x" << std::hex
            << ctx->Rip << "\n";
}

int main(int argc, char* argv[]) {
  std::cout << "SjlDbg\n";

  if (inasm64::Start()) {
    // this is
    //  xor rax,rax
    //  inc rax
    //  inc rax
    //  inc rax
    static const unsigned char code[] = {0x48, 0x31, 0xC0, 0x48, 0xFF, 0xC0,
                                         0x48, 0xFF, 0xC0, 0x48, 0xFF, 0xC0};
    inasm64::AddCode(code, sizeof(code));
    const auto l1 = inasm64::InstructionPointer();
    inasm64::Step();
    PrintContext();
    inasm64::SetReg(inasm64::ByteReg::AH, 0x42);
    inasm64::Step();
    PrintContext();
    inasm64::Step();
    PrintContext();
    // at this point rax = 2
    inasm64::SetNextInstruction(l1);
    inasm64::Step();
    PrintContext();

    inasm64::Shutdown();
  }

  return 0;
}