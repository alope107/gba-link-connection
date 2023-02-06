#include <tonc.h>
#include <string>
#include "../../_lib/interrupt.h"

// (0) Include the header
#include "../../../lib/LinkUniversal.h"

void log(std::string text);

// (1) Create a LinkUniversal instance
LinkUniversal* linkUniversal = new LinkUniversal();

void init() {
  REG_DISPCNT = DCNT_MODE0 | DCNT_BG0;
  tte_init_se_default(0, BG_CBB(0) | BG_SBB(31));

  // (2) Add the interrupt service routines
  interrupt_init();
  interrupt_set_handler(INTR_VBLANK, LINK_UNIVERSAL_ISR_VBLANK);
  interrupt_enable(INTR_VBLANK);
  interrupt_set_handler(INTR_SERIAL, LINK_UNIVERSAL_ISR_SERIAL);
  interrupt_enable(INTR_SERIAL);
  interrupt_set_handler(INTR_TIMER3, LINK_UNIVERSAL_ISR_TIMER);
  interrupt_enable(INTR_TIMER3);

  // (3) Initialize the library
  linkUniversal->activate();
}

int main() {
  init();

  u16 data[LINK_UNIVERSAL_MAX_PLAYERS];
  for (u32 i = 0; i < LINK_UNIVERSAL_MAX_PLAYERS; i++)
    data[i] = 0;

  while (true) {
    // (4) Sync
    linkUniversal->sync();

    // (5) Send/read messages
    u16 keys = ~REG_KEYS & KEY_ANY;
    linkUniversal->send(keys + 1);  // (avoid using 0)

    std::string output = "";
    if (linkUniversal->isConnected()) {
      u8 playerCount = linkUniversal->playerCount();
      u8 currentPlayerId = linkUniversal->currentPlayerId();

      output += "Players: " + std::to_string(playerCount) + "\n";

      output += "(";
      for (u32 i = 0; i < playerCount; i++) {
        while (linkUniversal->canRead(i))
          data[i] = linkUniversal->read(i) - 1;  // (avoid using 0)

        output += std::to_string(data[i]) + (i + 1 == playerCount ? ")" : ", ");
      }
      output += "\n";
      output += "_keys: " + std::to_string(keys) + "\n";
      output += "_pID: " + std::to_string(currentPlayerId);
    } else {
      output += "Waiting... [" + std::to_string(linkUniversal->getMode()) + "]";
      if (linkUniversal->getMode() == LinkUniversal::Mode::LINK_WIRELESS)
        output += "[" + std::to_string(linkUniversal->getWirelessState()) + "]";
    }

    VBlankIntrWait();
    log(output);
  }

  return 0;
}

void log(std::string text) {
  tte_erase_screen();
  tte_write("#{P:0,0}");
  tte_write(text.c_str());
}