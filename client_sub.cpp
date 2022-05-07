#include <chrono>
#include <thread>

#include "clientlib.cpp"

int main() {
  using namespace std::chrono_literals;
  SkinnyClient skinny;
  int fh = skinny.OpenDir("/fruit", [&](int fh) {
    std::cout << "fruit updated: " << skinny.GetContent(fh) << std::endl;
  });
  int applefh = skinny.Open(
      "/fruit/apple", [](int) { std::cout << "apple updated" << std::endl; });
  skinny.SetContent(applefh, "fuji");
  std::this_thread::sleep_for(100ms);
  int bananafh = skinny.Open("/fruit/banana");
  std::this_thread::sleep_for(100ms);
  skinny.Open("/fruit/guava");
  std::this_thread::sleep_for(100ms);
  skinny.Delete(bananafh);
  std::this_thread::sleep_for(60s);
}
