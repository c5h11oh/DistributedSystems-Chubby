#include <unistd.h>

#include <iostream>
#include <thread>

#include "clientlib.h"

int main() {
  char hostname[1024];
  gethostname(hostname, 1023);
  std::cout << "I am " << hostname << std::endl;

  SkinnyClient a = SkinnyClient();
  int fh = a.OpenDir("/service", [&a](int fh) {
    std::cout << "New membership status";
    for (char c : a.GetContent(fh)) {
      std::cout << (c == '\0' ? '\n' : c);
    }
    std::cout << "\n=========\n";
  });

  a.Open("/service/" + std::string(hostname), std::nullopt, true);

  std::this_thread::sleep_for(std::chrono::hours(1));  // do stuff
}