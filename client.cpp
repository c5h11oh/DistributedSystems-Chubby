#include "clientlib.cpp"
#include <iostream>

int main(int argc, char *argv[]) {
  SkinnyClient skinny;
  skinny.StartSessionOrDie();
  int fh = skinny.Open("apple");
  skinny.SetContent(fh, "hello world");
  std::cout << skinny.GetContent(fh) << std::endl;

  int fh2 = skinny.Open("banana");
  skinny.SetContent(fh2, "hello banana");
  std::cout << skinny.GetContent(fh2);

  return 0;
}
