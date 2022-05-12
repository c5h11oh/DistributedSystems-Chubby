#include <unistd.h>

#include <iostream>
#include <thread>

#include "clientlib.h"

int main() {
  char hostname[1024];
  gethostname(hostname, 1023);
  std::cout << "I am " << hostname << std::endl;

  SkinnyClient a = SkinnyClient();
  int fh = a.Open("/primary", [&a](int fh) {
    std::cout << "The primary server is " << a.GetContent(fh) << std::endl;
  });

  std::string primary = a.GetContent(fh);

  if (primary.empty()) {
    bool acq_success = a.TryAcquire(fh, true);
    if (acq_success) {
      a.SetContent(fh, hostname);
    }
  } else {
    std::cout << "The primary server is " << primary << std::endl;
  }

  std::this_thread::sleep_for(std::chrono::seconds(15));  // do stuff
}