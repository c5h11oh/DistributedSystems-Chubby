#include <iostream>
#include <thread>

#include "clientlib.cpp"
using namespace std::chrono_literals;

int main(int argc, char *argv[]) {
  using namespace std::chrono_literals;
  SkinnyClient skinny;
  // int fh = skinny.Open("apple");
  // skinny.SetContent(fh, "hello world");
  // std::cout << skinny.GetContent(fh) << std::endl;

  // int fh2 = skinny.Open("banana");
  // skinny.SetContent(fh2, "hello banana");
  // std::cout << skinny.GetContent(fh2) << std::endl;

  // if (skinny.Acquire(fh2, true))
  //   std::cout << "Got fh2 ex lock\n";
  // else
  //   std::cout << "Fail to get fh2 ex lock\n";

  // std::this_thread::sleep_for(3s);
  // skinny.Release(fh2);

  std::this_thread::sleep_for(50s);
  return 0;
}
