// Export the production discovery payloads for HA schema/template validation.
#include "../components/atlas_pool/control_discovery.h"
#include "../components/atlas_pool/reading_discovery.h"
#include <iostream>
int main(int argc, char **) {
  std::cout<<'[';
  if (argc>1) {
    for (uint8_t i=0;i<6;++i) {
      if (i) std::cout<<',';
      std::cout<<atlas_pool::reading_discovery(i,"atlas_pool","Atlas test","atlas-pool-kit/calibration");
    }
    std::cout<<"]\n";
    return 0;
  }
  for (uint8_t i=0;i<atlas_pool::CONTROL_COUNT;++i) {
    if (i) std::cout<<',';
    std::cout<<atlas_pool::control_discovery(i,"atlas_pool","Atlas test","atlas-pool-kit/calibration");
  }
  std::cout<<"]\n";
}
