#include "boot_message.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace c3::logging {

namespace {
static constexpr std::string_view kBannerArt = R"(
          ##  ##  ######################
      ##    ##  ######  ################
##  ####  ####    ######################  ##  ## ###### ##  ## #####
  ##    ##    ########                    ##  ## ##     ##  ## ##  ##
      ########  ######                    ##  ## #####  ###### #####
    ##    ##  ######################      ##  ## ##     ##  ## ##  ##
            ##      ####  ##############   ####  ###### ##  ## ##  ##
        ##    ##  ######################
                                  ######  ###### ##  ## #####  ## ###### ######
                                  ######    ##   ##  ## ##  ## ## ##  ## ##
    ##  ####  ##########################    ##   ##  ## ##  ## ## ##  ## ######
                  ##  ##################    ##   ##  ## ##  ## ## ##  ##     ##
        ##  ##  ####################        ##    ####  #####  ## ###### ######
        
[C3 CORVETTE CONSOLE] [0.0.1+1] - © 2026 Tyler R. Suehr
)";
}  // namespace

void boot_message() noexcept { static_cast<void>(std::fwrite(kBannerArt.data(), 1, kBannerArt.size(), stderr)); }

}  // namespace c3::logging