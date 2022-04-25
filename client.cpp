#include "clientlib.cpp"
#include <iostream>
#include <thread>

int main(int argc, char *argv[]) {
  using namespace std::chrono_literals;
  SkinnyClient skinny;
  int fh = skinny.Open("apple");
  skinny.SetContent(fh, "hello world");
  std::cout << skinny.GetContent(fh) << std::endl;

  int fh2 = skinny.Open("banana");
  skinny.SetContent(fh2, "hello banana");
  std::cout << skinny.GetContent(fh2) << std::endl;

  std::this_thread::sleep_for(50s);
  return 0;
}
