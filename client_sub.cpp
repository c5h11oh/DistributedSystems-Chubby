#include <chrono>
#include <thread>

#include "clientlib.cpp"

int main() {
  SkinnyClient skinny;
  int fh = skinny.Open(
      "fruit", []() { std::cout << "file fruit updated" << std::endl; });
  skinny.SetContent(fh, "banana");
  std::cout << skinny.GetContent(fh) << std::endl;
  skinny.SetContent(fh, "apple");
  std::this_thread::sleep_for(std::chrono::seconds(5));
  skinny.SetContent(fh, "guava");
  std::this_thread::sleep_for(std::chrono::minutes(1));
}
