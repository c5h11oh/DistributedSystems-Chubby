#include <iostream>
#include <thread>

#include "clientlib.cpp"
using namespace std::chrono_literals;

std::shared_ptr<SkinnyClient> skinny_ptr;
int main(int argc, char *argv[]) {
  skinny_ptr = std::make_shared<SkinnyClient>();
  int fh = skinny_ptr->Open("apple");
  skinny_ptr->SetContent(fh, "hello world");
  std::cout << skinny_ptr->GetContent(fh) << std::endl;

  int fh2 = skinny_ptr->Open("banana");
  skinny_ptr->SetContent(fh2, "hello banana");
  std::cout << skinny_ptr->GetContent(fh2) << std::endl;

  if (skinny_ptr->Acquire(fh2, true))
    std::cout << "Got fh2 ex lock\n";
  else
    std::cout << "Fail to get fh2 ex lock\n";

  std::this_thread::sleep_for(3s);
  skinny_ptr->Release(fh2);

  return 0;
}
