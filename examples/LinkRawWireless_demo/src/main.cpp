#include <libgba-sprite-engine/gba_engine.h>
#include <tonc.h>
#include "../../../lib/LinkRawWireless.hpp"
#include "../../_lib/interrupt.h"
#include "scenes/DebugScene.h"
#include "utils/SceneUtils.h"

void setUpInterrupts();
void printTutorial();
static std::shared_ptr<GBAEngine> engine{new GBAEngine()};
static std::unique_ptr<DebugScene> debugScene{new DebugScene(engine)};

LinkRawWireless* linkRawWireless = new LinkRawWireless();

int main() {
  setUpInterrupts();

  engine->setScene(debugScene.get());

  while (true) {
    engine->update();

    VBlankIntrWait();
  }

  return 0;
}

inline void ISR_reset() {
  RegisterRamReset(RESET_REG | RESET_VRAM);
  SoftReset();
}

inline void setUpInterrupts() {
  interrupt_init();

  interrupt_set_handler(INTR_VBLANK, [] {});
  interrupt_enable(INTR_VBLANK);
  interrupt_set_handler(INTR_SERIAL, [] {});
  interrupt_enable(INTR_SERIAL);

  // A+B+START+SELECT
  REG_KEYCNT = 0b1100000000001111;
  interrupt_set_handler(INTR_KEYPAD, ISR_reset);
}
