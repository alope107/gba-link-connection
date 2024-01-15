#include "DebugScene.h"

#include <libgba-sprite-engine/background/text_stream.h>
#include <tonc.h>
#include <algorithm>
#include <functional>

#include "../../../../lib/LinkRawWireless.hpp"
#include "utils/InputHandler.h"
#include "utils/SceneUtils.h"

DebugScene::DebugScene(std::shared_ptr<GBAEngine> engine) : Scene(engine) {}

static std::unique_ptr<InputHandler> aHandler =
    std::unique_ptr<InputHandler>(new InputHandler());
static std::unique_ptr<InputHandler> bHandler =
    std::unique_ptr<InputHandler>(new InputHandler());
static std::unique_ptr<InputHandler> upHandler =
    std::unique_ptr<InputHandler>(new InputHandler());
static std::unique_ptr<InputHandler> downHandler =
    std::unique_ptr<InputHandler>(new InputHandler());
static std::unique_ptr<InputHandler> lHandler =
    std::unique_ptr<InputHandler>(new InputHandler());
static std::unique_ptr<InputHandler> rHandler =
    std::unique_ptr<InputHandler>(new InputHandler());
static std::unique_ptr<InputHandler> selectHandler =
    std::unique_ptr<InputHandler>(new InputHandler());
static std::unique_ptr<InputHandler> startHandler =
    std::unique_ptr<InputHandler>(new InputHandler());

static std::vector<std::string> logLines;
static u32 currentLogLine = 0;
static bool useVerboseLog = true;

static const std::string CHARACTERS[] = {
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c",
    "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p",
    "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", "A", "B", "C",
    "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P",
    "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
static const u32 CHARACTERS_LEN = 62;

#define MAX_LINES 18
#define DRAW_LINE 2

void printScrollableText(u32 currentLine,
                         std::vector<std::string> lines,
                         bool withCursor = false) {
  for (u32 i = 0; i < MAX_LINES; i++) {
    u32 lastLineIndex = MAX_LINES - 1;
    u32 index = max(currentLine, lastLineIndex) - lastLineIndex + i;
    if (index < lines.size()) {
      std::string cursor = currentLine == index ? "> " : "  ";
      TextStream::instance().setText((withCursor ? cursor : "") + lines[index],
                                     DRAW_LINE + i, -3);
    } else {
      TextStream::instance().setText("                              ",
                                     DRAW_LINE + i, -3);
    }
  }
}

void print() {
  printScrollableText(currentLogLine, logLines);
}

void scrollBack() {
  if (currentLogLine <= 0)
    return;
  currentLogLine--;
  print();
}

void scrollForward() {
  if (currentLogLine < MAX_LINES - 1)
    currentLogLine = min(MAX_LINES - 1, logLines.size() - 1);
  if (currentLogLine == logLines.size() - 1)
    return;
  currentLogLine++;
  print();
}

void scrollPageUp() {
  currentLogLine = max(currentLogLine - MAX_LINES, 0);
  print();
}

void scrollPageDown() {
  currentLogLine = min(currentLogLine + MAX_LINES, logLines.size() - 1);
  print();
}

void scrollToTop() {
  currentLogLine = 0;
  print();
}

void scrollToBottom() {
  currentLogLine = logLines.size() - 1;
  print();
}

void clear() {
  logLines.clear();
  currentLogLine = 0;
  print();
}

void log(std::string string) {
  logLines.push_back(string);
  scrollPageDown();
}

std::vector<Background*> DebugScene::backgrounds() {
  return {};
}

std::vector<Sprite*> DebugScene::sprites() {
  std::vector<Sprite*> sprites;
  return sprites;
}

void DebugScene::load() {
  SCENE_init();
  BACKGROUND_enable(true, false, false, false);

  linkRawWireless->logger = [](std::string string) {
    if (useVerboseLog)
      log(string);
  };

  log("---");
  log("LinkRawWireless demo");
  log("");
  log("START: reset wireless adapter");
  log("A: send command");
  log("B: toggle log level");
  log("UP/DOWN: scroll up/down");
  log("L/R: scroll page up/down");
  log("UP+L/DOWN+R: scroll to top/bottom");
  log("SELECT: clear");
  log("---");
  log("");
  toggleLogLevel();

  addCommandMenuOptions();
  for (u32 i = 0; i < 4; i++)
    serverIds[i] = 0;
}

void DebugScene::tick(u16 keys) {
  if (engine->isTransitioning())
    return;

  std::string state =
      linkRawWireless->getState() == LinkRawWireless::State::NEEDS_RESET
          ? "NEEDS_RESET       "
      : linkRawWireless->getState() == LinkRawWireless::State::AUTHENTICATED
          ? "AUTHENTICATED     "
      : linkRawWireless->getState() == LinkRawWireless::State::SEARCHING
          ? "SEARCHING         "
      : linkRawWireless->getState() == LinkRawWireless::State::SERVING
          ? "SERVING           "
      : linkRawWireless->getState() == LinkRawWireless::State::CONNECTING
          ? "CONNECTING        "
      : linkRawWireless->getState() == LinkRawWireless::State::CONNECTED
          ? "CONNECTED         "
          : "?                 ";
  TextStream::instance().setText(
      "state = " + state + "p" +
          std::to_string(linkRawWireless->sessionState.currentPlayerId) +
          (linkRawWireless->getState() == LinkRawWireless::State::SERVING
               ? "/" + std::to_string(linkRawWireless->sessionState.playerCount)
               : ""),
      0, -3);

  processKeys(keys);
  processButtons();

  __qran_seed += keys;
  __qran_seed += REG_RCNT;
  __qran_seed += REG_SIOCNT;
}

void DebugScene::addCommandMenuOptions() {
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x10 (Hello)", .command = 0x10});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x11 (SignalLevel)", .command = 0x11});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x12 (VersionStatus)", .command = 0x12});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x13 (SystemStatus)", .command = 0x13});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x14 (SlotStatus)", .command = 0x14});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x15 (ConfigStatus)", .command = 0x15});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x16 (Broadcast)", .command = 0x16});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x17 (Setup)", .command = 0x17});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x18 (?)", .command = 0x18});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x19 (StartHost)", .command = 0x19});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x1a (AcceptConnections)", .command = 0x1a});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x1b (EndHost)", .command = 0x1b});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x1c (BroadcastRead1)", .command = 0x1c});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x1d (BroadcastRead2)", .command = 0x1d});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x1e (BroadcastRead3)", .command = 0x1e});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x1f (Connect)", .command = 0x1f});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x20 (IsFinishedConnect)", .command = 0x20});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x21 (FinishConnection)", .command = 0x21});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x24 (SendData)", .command = 0x24});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x25 (SendDataAndWait)", .command = 0x25});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x26 (ReceiveData)", .command = 0x26});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x27 (Wait)", .command = 0x27});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x30 (DisconnectClient)", .command = 0x30});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x32 (?)", .command = 0x32});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x33 (?)", .command = 0x33});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x34 (?)", .command = 0x34});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x35 (?!)", .command = 0x35});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x37 (RetransmitAndWait)", .command = 0x37});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x38 (?)", .command = 0x38});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x39 (?)", .command = 0x39});
  commandMenuOptions.push_back(
      CommandMenuOption{.name = "0x3d (Bye)", .command = 0x3d});
}

void DebugScene::processKeys(u16 keys) {
  aHandler->setIsPressed(keys & KEY_A);
  bHandler->setIsPressed(keys & KEY_B);
  upHandler->setIsPressed(keys & KEY_UP);
  downHandler->setIsPressed(keys & KEY_DOWN);
  lHandler->setIsPressed(keys & KEY_L);
  rHandler->setIsPressed(keys & KEY_R);
  selectHandler->setIsPressed(keys & KEY_SELECT);
  startHandler->setIsPressed(keys & KEY_START);
}

void DebugScene::processButtons() {
  if (aHandler->hasBeenPressedNow()) {
    std::vector<std::string> commandNames;
    commandNames.resize(commandMenuOptions.size());
    std::transform(commandMenuOptions.begin(), commandMenuOptions.end(),
                   commandNames.begin(),
                   [](CommandMenuOption x) { return x.name; });

    int selectedCommandIndex =
        selectOption("Which command?", commandNames, lastSelectedCommandIndex);
    if (selectedCommandIndex > -1) {
      lastSelectedCommandIndex = selectedCommandIndex;
      processCommand((u32)selectedCommandIndex);
    }

    print();
  }

  if (bHandler->hasBeenPressedNow())
    toggleLogLevel();

  if (lHandler->hasBeenPressedNow()) {
    if (upHandler->getIsPressed())
      scrollToTop();
    else
      scrollPageUp();
  }

  if (rHandler->hasBeenPressedNow()) {
    if (downHandler->getIsPressed())
      scrollToBottom();
    else
      scrollPageDown();
  }

  if (upHandler->getIsPressed())
    scrollBack();

  if (downHandler->getIsPressed())
    scrollForward();

  if (selectHandler->hasBeenPressedNow())
    clear();

  if (startHandler->hasBeenPressedNow())
    resetAdapter();
}

void DebugScene::toggleLogLevel() {
  if (useVerboseLog) {
    useVerboseLog = false;
    log("! setting log level to NORMAL");
  } else {
    useVerboseLog = true;
    log("! setting log level to VERBOSE");
  }
  log("");
}

int DebugScene::selectOption(std::string title,
                             std::vector<std::string> options,
                             u32 cursor = 0) {
  u32 selectedOption = cursor;
  bool firstTime = true;

  while (true) {
    u16 keys = ~REG_KEYS & KEY_ANY;
    processKeys(keys);

    u32 oldOption = selectedOption;

    if (lHandler->hasBeenPressedNow()) {
      if (upHandler->getIsPressed())
        selectedOption = 0;
      else
        selectedOption = max(selectedOption - MAX_LINES, 0);
    }
    if (rHandler->hasBeenPressedNow()) {
      if (downHandler->getIsPressed())
        selectedOption = options.size() - 1;
      else
        selectedOption = min(selectedOption + MAX_LINES, options.size() - 1);
    }
    if (downHandler->hasBeenPressedNow() && selectedOption < options.size() - 1)
      selectedOption++;
    if (upHandler->hasBeenPressedNow() && selectedOption > 0)
      selectedOption--;

    if (firstTime || selectedOption != oldOption) {
      TextStream::instance().setText(title, 0, -3);
      printScrollableText(selectedOption, options, true);
    }

    if (bHandler->hasBeenPressedNow())
      return -1;

    if (aHandler->hasBeenPressedNow())
      return selectedOption;

    VBlankIntrWait();
    firstTime = false;
  }
}

std::string DebugScene::selectString(u32 maxCharacters) {
  std::vector<std::string> options = {"<end>"};
  for (u32 i = 0; i < CHARACTERS_LEN; i++)
    options.push_back(CHARACTERS[i]);

again:
  std::string str;
  for (u32 i = 0; i < maxCharacters; i++) {
    int characterIndex = -1;
    if ((characterIndex =
             selectOption("Next character? (" + str + ")", options)) == -1) {
      if (i == 0)
        return "";
      else
        goto again;
    }
    if (characterIndex == 0)
      break;
    str += CHARACTERS[characterIndex - 1];
  }

  if (str == "")
    goto again;

  if (selectOption(">> " + str + "?", std::vector<std::string>{"yes", "no"}) ==
      1)
    goto again;

  return str;
}

int DebugScene::selectU32(std::string title) {
again0:
  int byte0 = selectU8(title + " - Byte 0 (0x000000XX)");
  if (byte0 == -1)
    return -1;
again1:
  int byte1 = selectU8(title + " - Byte 1 (0x0000XX" +
                       linkRawWireless->toHex(byte0, 2) + ")");
  if (byte1 == -1)
    goto again0;
again2:
  int byte2 =
      selectU8(title + " - Byte 2 (0x00XX" + linkRawWireless->toHex(byte1, 2) +
               linkRawWireless->toHex(byte0, 2) + ")");
  if (byte2 == -1)
    goto again1;
  int byte3 =
      selectU8(title + " - Byte 3 (0xXX" + linkRawWireless->toHex(byte2, 2) +
               linkRawWireless->toHex(byte1, 2) +
               linkRawWireless->toHex(byte0, 2) + ")");
  if (byte3 == -1)
    goto again2;

  u16 numberLow = linkRawWireless->buildU16((u8)byte1, (u8)byte0);
  u16 numberHigh = linkRawWireless->buildU16((u8)byte3, (u8)byte2);
  u32 number = linkRawWireless->buildU32(numberHigh, numberLow);
  if (selectOption(">> 0x" + linkRawWireless->toHex(number, 8) + "?",
                   std::vector<std::string>{"yes", "no"}) == 1)
    goto again0;

  return number;
}

int DebugScene::selectU16() {
again:
  int lsB = selectU8("Choose lsB (0x00XX)");
  if (lsB == -1)
    return -1;
  int msB = selectU8("Choose msB (0xXX" + linkRawWireless->toHex(lsB, 2) + ")");
  if (msB == -1)
    goto again;

  u16 number = linkRawWireless->buildU16((u8)msB, (u8)lsB);
  if (selectOption(">> 0x" + linkRawWireless->toHex(number, 4) + "?",
                   std::vector<std::string>{"yes", "no"}) == 1)
    goto again;

  return number;
}

int DebugScene::selectU8(std::string title) {
  std::vector<std::string> options;
  for (u32 i = 0; i < 1 << 8; i++)
    options.push_back(linkRawWireless->toHex(i, 2));
  return selectOption(title, options);
}

void DebugScene::processCommand(u32 selectedCommandIndex) {
  CommandMenuOption selectedOption = commandMenuOptions[selectedCommandIndex];
  auto name = selectedOption.name;
  auto command = selectedOption.command;

  if (selectHandler->getIsPressed() && command < 0xfa)
    goto generic;

  switch (command) {
    case 0x10:
    case 0x11:
    case 0x12:
    case 0x13:
      goto simple;
    case 0x14: {
      return logOperation("sending " + name, []() {
        LinkRawWireless::SlotStatusResponse response;
        bool success = linkRawWireless->getSlotStatus(response);
        log("< [next slot] " +
            linkRawWireless->toHex(response.nextClientNumber, 2));
        for (u32 i = 0; i < response.connectedClients.size(); i++) {
          log("< [client" +
              std::to_string(response.connectedClients[i].clientNumber) + "] " +
              linkRawWireless->toHex(response.connectedClients[i].deviceId, 4));
        }
        return success;
      });
    }
    case 0x15:
      goto simple;
    case 0x16: {
      int gameId = selectGameId();
      if (gameId == -1)
        return;
      std::string gameName = selectGameName();
      if (gameName == "")
        return;
      std::string userName = selectUserName();
      if (userName == "")
        return;

      return logOperation("sending " + name, [gameName, userName, gameId]() {
        return linkRawWireless->broadcast(gameName, userName, (u16)gameId);
      });
    }
    case 0x17: {
      int maxPlayers = -1;
      while ((maxPlayers = selectOption(
                  "Max players?",
                  std::vector<std::string>{"5", "4", "3", "2"})) == -1)
        ;

      return logOperation("sending " + name, [maxPlayers]() {
        return linkRawWireless->setup(5 - maxPlayers);
      });
    }
    case 0x18:
      goto generic;
    case 0x19:
      return logOperation("sending " + name,
                          []() { return linkRawWireless->startHost(); });
    case 0x1a: {
      return logOperation("sending " + name, []() {
        LinkRawWireless::AcceptConnectionsResponse response;
        bool success = linkRawWireless->acceptConnections(response);
        for (u32 i = 0; i < response.connectedClients.size(); i++) {
          log("< [client" +
              std::to_string(response.connectedClients[i].clientNumber) + "] " +
              linkRawWireless->toHex(response.connectedClients[i].deviceId, 4));
        }
        return success;
      });
    }
    case 0x1b: {
      return logOperation("sending " + name, []() {
        LinkRawWireless::AcceptConnectionsResponse response;
        bool success = linkRawWireless->endHost(response);
        for (u32 i = 0; i < response.connectedClients.size(); i++) {
          log("< [client" +
              std::to_string(response.connectedClients[i].clientNumber) + "] " +
              linkRawWireless->toHex(response.connectedClients[i].deviceId, 4));
        }
        return success;
      });
    }
    case 0x1c: {
      return logOperation("sending " + name, []() {
        return linkRawWireless->broadcastReadStart();
      });
    }
    case 0x1d: {
      return logOperation("sending " + name, [this]() {
        std::vector<LinkRawWireless::Server> servers;
        bool success = linkRawWireless->broadcastReadPoll(servers);

        for (u32 i = 0; i < servers.size(); i++) {
          serverIds[i] = servers[i].id;

          log("< [room" + std::to_string(i) + ".id] " +
              linkRawWireless->toHex(servers[i].id, 4));
          log("< [room" + std::to_string(i) + ".gameid] " +
              linkRawWireless->toHex(servers[i].gameId, 4));
          log("< [room" + std::to_string(i) + ".game] " + servers[i].gameName);
          log("< [room" + std::to_string(i) + ".user] " + servers[i].userName);
          log("< [room" + std::to_string(i) + ".nextSlot] " +
              linkRawWireless->toHex(servers[i].nextClientNumber, 2));
        }

        log("NOW CALL 0x1e!");

        return success;
      });
    }
    case 0x1e: {
      return logOperation("sending " + name,
                          []() { return linkRawWireless->broadcastReadEnd(); });
    }
    case 0x1f: {
      u16 serverId = selectServerId();
      if (serverId == -1)
        return;

      return logOperation("sending " + name, [serverId]() {
        return linkRawWireless->connect(serverId);
      });
    }
    case 0x20:
      return logOperation("sending " + name, []() {
        LinkRawWireless::ConnectionStatus response;
        bool success = linkRawWireless->keepConnecting(response);
        log(std::string("< [phase] ") + (response.phase == 0   ? "CONNECTING"
                                         : response.phase == 1 ? "ERROR"
                                                               : "SUCCESS"));
        if (response.phase == LinkRawWireless::ConnectionPhase::SUCCESS)
          log(std::string("< [slot] ") +
              std::to_string(response.assignedClientNumber));

        log("NOW CALL 0x21!");

        return success;
      });
    case 0x21:
      return logOperation("sending " + name,
                          []() { return linkRawWireless->finishConnection(); });
    default:
      return;
  }

simple:
  return logSimpleCommand(name, command);
generic:
  auto data = selectData();
  return logSimpleCommand(name, command, data);
}

int DebugScene::selectServerId() {
  switch (selectOption("Which server id?", std::vector<std::string>{
                                               "<first>", "<second>", "<third>",
                                               "<fourth>", "<pick>"})) {
    case 0: {
      if (serverIds[0] == 0)
        return -1;

      return serverIds[0];
    }
    case 1: {
      if (serverIds[1] == 0)
        return -1;

      return serverIds[1];
    }
    case 2: {
      if (serverIds[2] == 0)
        return -1;

      return serverIds[2];
    }
    case 3: {
      if (serverIds[3] == 0)
        return -1;

      return serverIds[3];
    }
    default: {
      return selectU16();
    }
  }
}

int DebugScene::selectGameId() {
  switch (selectOption(
      "GameID?",
      std::vector<std::string>{"0x7FFF", "0x1234", "<random>", "<pick>"})) {
    case 0: {
      return 0x7fff;
    }
    case 1: {
      return 0x1234;
    }
    case 2: {
      return linkRawWireless->buildU16(qran_range(0, 256), qran_range(0, 256));
    }
    default: {
      return selectU16();
    }
  }
}

std::string DebugScene::selectGameName() {
  switch (selectOption("Game name?",
                       std::vector<std::string>{"LinkConnection", "<pick>"})) {
    case 0: {
      return "LinkConnection";
    }
    default: {
      return selectString(LINK_RAW_WIRELESS_MAX_GAME_NAME_LENGTH);
    }
  }
}

std::vector<u32> DebugScene::selectData() {
  std::vector<u32> data;

  std::vector<std::string> options;
  for (u32 i = 0; i < LINK_RAW_WIRELESS_MAX_COMMAND_TRANSFER_LENGTH; i++)
    options.push_back(std::to_string(i));
  int words = selectOption("How many words?", options);
  if (words == -1)
    return data;

  for (u32 i = 0; i < (u32)words; i++) {
    int word = selectU32("Word " + std::to_string(i + 1) + "/" +
                         std::to_string(words));
    if (word == -1)
      return data;
    data.push_back(word);
  }

  return data;
}

std::string DebugScene::selectUserName() {
  switch (
      selectOption("User name?", std::vector<std::string>{"Demo", "<pick>"})) {
    case 0: {
      return "Demo";
    }
    default: {
      return selectString(LINK_RAW_WIRELESS_MAX_USER_NAME_LENGTH);
    }
  }
}

void DebugScene::logSimpleCommand(std::string name,
                                  u32 id,
                                  std::vector<u32> params) {
  logOperation("sending " + name, [id, &params]() {
    auto result = linkRawWireless->sendCommand(id, params);
    for (u32 i = 0; i < result.responses.size(); i++) {
      log("< [response" + std::to_string(i) + "] " +
          linkRawWireless->toHex(result.responses[i]));
    }
    return result.success;
  });
}

void DebugScene::logOperation(std::string name,
                              std::function<bool()> operation) {
  log("> " + name + "...");
  bool success = operation();
  log(success ? "< success :)" : "< failure :(");
  log("");
}

void DebugScene::resetAdapter() {
  logOperation("resetting adapter",
               []() { return linkRawWireless->activate(); });
}
