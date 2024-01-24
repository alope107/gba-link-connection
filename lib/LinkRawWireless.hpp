#ifndef LINK_RAW_WIRELESS_H
#define LINK_RAW_WIRELESS_H

// --------------------------------------------------------------------------
// A low level driver for the GBA Wireless Adapter.
// --------------------------------------------------------------------------
// - Advanced usage only!
// - If you're building a game, use `LinkWireless`.
// --------------------------------------------------------------------------

#include <tonc_core.h>
#include <tonc_math.h>
#include "LinkGPIO.hpp"
#include "LinkSPI.hpp"

#include <string>
#include <vector>

// Enable logging (set `linkRawWireless->logger`)
#define LINK_RAW_WIRELESS_ENABLE_LOGGING true

#define LINK_RAW_WIRELESS_MAX_PLAYERS 5
#define LINK_RAW_WIRELESS_PING_WAIT 50
#define LINK_RAW_WIRELESS_TRANSFER_WAIT 15
#define LINK_RAW_WIRELESS_CMD_TIMEOUT 100
#define LINK_RAW_WIRELESS_MAX_CLIENT_TRANSFER_LENGTH 4
#define LINK_RAW_WIRELESS_MAX_GAME_ID 0x7fff
#define LINK_RAW_WIRELESS_MAX_GAME_NAME_LENGTH 14
#define LINK_RAW_WIRELESS_MAX_USER_NAME_LENGTH 8
#define LINK_RAW_WIRELESS_LOGIN_STEPS 9
#define LINK_RAW_WIRELESS_COMMAND_HEADER 0x9966
#define LINK_RAW_WIRELESS_RESPONSE_ACK 0x80
#define LINK_RAW_WIRELESS_DATA_REQUEST 0x80000000
#define LINK_RAW_WIRELESS_SETUP_MAGIC 0x003c0420
#define LINK_RAW_WIRELESS_SETUP_MAX_PLAYERS_BIT 16
#define LINK_RAW_WIRELESS_STILL_CONNECTING 0x01000000
#define LINK_RAW_WIRELESS_BROADCAST_LENGTH 6
#define LINK_RAW_WIRELESS_BROADCAST_RESPONSE_LENGTH \
  (1 + LINK_RAW_WIRELESS_BROADCAST_LENGTH)
#define LINK_RAW_WIRELESS_MAX_COMMAND_TRANSFER_LENGTH 22
#define LINK_RAW_WIRELESS_COMMAND_HELLO 0x10
#define LINK_RAW_WIRELESS_COMMAND_SETUP 0x17
#define LINK_RAW_WIRELESS_COMMAND_BROADCAST 0x16
#define LINK_RAW_WIRELESS_COMMAND_START_HOST 0x19
#define LINK_RAW_WIRELESS_COMMAND_SLOT_STATUS 0x14
#define LINK_RAW_WIRELESS_COMMAND_ACCEPT_CONNECTIONS 0x1a
#define LINK_RAW_WIRELESS_COMMAND_END_HOST 0x1b
#define LINK_RAW_WIRELESS_COMMAND_BROADCAST_READ_START 0x1c
#define LINK_RAW_WIRELESS_COMMAND_BROADCAST_READ_POLL 0x1d
#define LINK_RAW_WIRELESS_COMMAND_BROADCAST_READ_END 0x1e
#define LINK_RAW_WIRELESS_COMMAND_CONNECT 0x1f
#define LINK_RAW_WIRELESS_COMMAND_IS_FINISHED_CONNECT 0x20
#define LINK_RAW_WIRELESS_COMMAND_FINISH_CONNECTION 0x21
#define LINK_RAW_WIRELESS_COMMAND_SEND_DATA 0x24
#define LINK_RAW_WIRELESS_COMMAND_SEND_DATA_AND_WAIT 0x25
#define LINK_RAW_WIRELESS_COMMAND_RECEIVE_DATA 0x26
#define LINK_RAW_WIRELESS_COMMAND_WAIT 0x27
#define LINK_RAW_WIRELESS_COMMAND_BYE 0x3d

static volatile char LINK_RAW_WIRELESS_VERSION[] = "LinkRawWireless/v6.1.1";

const u16 LINK_RAW_WIRELESS_LOGIN_PARTS[] = {
    0x494e, 0x494e, 0x544e, 0x544e, 0x4e45, 0x4e45, 0x4f44, 0x4f44, 0x8001};

class LinkRawWireless {
  typedef void (*Logger)(std::string);

 public:
  Logger logger = [](std::string str) {};

  enum State {
    NEEDS_RESET,
    AUTHENTICATED,
    SEARCHING,
    SERVING,
    CONNECTING,
    CONNECTED
  };

  struct CommandResult {
    bool success = false;
    std::vector<u32> responses = std::vector<u32>{};
  };

  struct RemoteCommand {
    bool success = false;
    u8 commandId = 0;
    std::vector<u32> params = std::vector<u32>{};
  };

  struct Server {
    u16 id = 0;
    u16 gameId;
    std::string gameName;
    std::string userName;
    u8 nextClientNumber;

    bool isFull() { return nextClientNumber == 0xff; }
  };

  typedef struct {
    u16 deviceId = 0;
    u8 clientNumber = 0;
  } ConnectedClient;

  typedef struct {
    u8 nextClientNumber = 0;
    std::vector<ConnectedClient> connectedClients = {};
  } SlotStatusResponse;

  typedef struct {
    std::vector<ConnectedClient> connectedClients = {};
  } AcceptConnectionsResponse;

  enum ConnectionPhase { STILL_CONNECTING, ERROR, SUCCESS };

  typedef struct {
    ConnectionPhase phase = STILL_CONNECTING;
    u8 assignedClientNumber = 0;
  } ConnectionStatus;

  typedef struct {
    u32 sentBytes[LINK_RAW_WIRELESS_MAX_PLAYERS];
    std::vector<u32> data = {};
  } ReceiveDataResponse;

  bool isActive() { return isEnabled; }

  bool activate() {
    isEnabled = false;

    bool success = reset(true);

    isEnabled = true;
    return success;
  }

  bool deactivate() {
    bool success = sendCommand(LINK_RAW_WIRELESS_COMMAND_BYE).success;

    isEnabled = false;
    resetState();
    stop();

    return success;
  }

  bool setup(u8 maxPlayers = LINK_RAW_WIRELESS_MAX_PLAYERS) {
    return sendCommand(
               LINK_RAW_WIRELESS_COMMAND_SETUP,
               std::vector<u32>{
                   (u32)(LINK_RAW_WIRELESS_SETUP_MAGIC |
                         (((LINK_RAW_WIRELESS_MAX_PLAYERS - maxPlayers) & 0b11)
                          << LINK_RAW_WIRELESS_SETUP_MAX_PLAYERS_BIT))})
        .success;
  }

  bool broadcast(std::string gameName = "",
                 std::string userName = "",
                 u16 gameId = LINK_RAW_WIRELESS_MAX_GAME_ID) {
    if (gameName.length() > LINK_RAW_WIRELESS_MAX_GAME_NAME_LENGTH) {
      log("! game name too long");
      return false;
    }
    if (userName.length() > LINK_RAW_WIRELESS_MAX_GAME_NAME_LENGTH) {
      log("! user name too long");
      return false;
    }
    gameName.append(LINK_RAW_WIRELESS_MAX_GAME_NAME_LENGTH - gameName.length(),
                    0);
    userName.append(LINK_RAW_WIRELESS_MAX_USER_NAME_LENGTH - userName.length(),
                    0);

    auto broadcastData =
        std::vector<u32>{buildU32(buildU16(gameName[1], gameName[0]), gameId),
                         buildU32(buildU16(gameName[5], gameName[4]),
                                  buildU16(gameName[3], gameName[2])),
                         buildU32(buildU16(gameName[9], gameName[8]),
                                  buildU16(gameName[7], gameName[6])),
                         buildU32(buildU16(gameName[13], gameName[12]),
                                  buildU16(gameName[11], gameName[10])),
                         buildU32(buildU16(userName[3], userName[2]),
                                  buildU16(userName[1], userName[0])),
                         buildU32(buildU16(userName[7], userName[6]),
                                  buildU16(userName[5], userName[4]))};

    bool success =
        sendCommand(LINK_RAW_WIRELESS_COMMAND_BROADCAST, broadcastData).success;

    if (!success) {
      reset();
      return false;
    }

    return true;
  }

  bool startHost() {
    bool success = sendCommand(LINK_RAW_WIRELESS_COMMAND_START_HOST).success;

    if (!success) {
      reset();
      return false;
    }

    wait(LINK_RAW_WIRELESS_TRANSFER_WAIT);
    log("state = SERVING");
    state = SERVING;

    return true;
  }

  bool getSlotStatus(SlotStatusResponse& response) {
    auto result = sendCommand(LINK_RAW_WIRELESS_COMMAND_SLOT_STATUS);

    if (!result.success) {
      reset();
      return false;
    }

    for (u32 i = 0; i < result.responses.size(); i++) {
      if (i == 0) {
        response.nextClientNumber = (u8)lsB32(result.responses[i]);
      } else {
        response.connectedClients.push_back(
            ConnectedClient{.deviceId = lsB32(result.responses[i]),
                            .clientNumber = (u8)msB32(result.responses[i])});
      }
    }

    return true;
  }

  bool acceptConnections(AcceptConnectionsResponse& response) {
    auto result = sendCommand(LINK_RAW_WIRELESS_COMMAND_ACCEPT_CONNECTIONS);

    if (!result.success) {
      reset();
      return false;
    }

    for (u32 i = 0; i < result.responses.size(); i++) {
      response.connectedClients.push_back(
          ConnectedClient{.deviceId = lsB32(result.responses[i]),
                          .clientNumber = (u8)msB32(result.responses[i])});
    }

    u8 oldPlayerCount = sessionState.playerCount;
    sessionState.playerCount = 1 + result.responses.size();
    if (sessionState.playerCount != oldPlayerCount)
      log("now: " + std::to_string(sessionState.playerCount) + " players");

    return true;
  }

  bool endHost(AcceptConnectionsResponse& response) {
    auto result = sendCommand(LINK_RAW_WIRELESS_COMMAND_END_HOST);

    if (!result.success) {
      reset();
      return false;
    }

    for (u32 i = 0; i < result.responses.size(); i++) {
      response.connectedClients.push_back(
          ConnectedClient{.deviceId = lsB32(result.responses[i]),
                          .clientNumber = (u8)msB32(result.responses[i])});
    }

    u8 oldPlayerCount = sessionState.playerCount;
    sessionState.playerCount = 1 + result.responses.size();
    if (sessionState.playerCount != oldPlayerCount)
      log("now: " + std::to_string(sessionState.playerCount) + " players");

    return true;
  }

  bool broadcastReadStart() {
    bool success =
        sendCommand(LINK_RAW_WIRELESS_COMMAND_BROADCAST_READ_START).success;

    if (!success) {
      reset();
      return false;
    }

    log("state = SEARCHING");
    state = SEARCHING;

    return true;
  }

  bool broadcastReadPoll(std::vector<Server>& servers) {
    auto result = sendCommand(LINK_RAW_WIRELESS_COMMAND_BROADCAST_READ_POLL);
    bool success =
        result.success &&
        result.responses.size() % LINK_RAW_WIRELESS_BROADCAST_RESPONSE_LENGTH ==
            0;

    if (!success) {
      reset();
      return false;
    }

    u32 totalBroadcasts =
        result.responses.size() / LINK_RAW_WIRELESS_BROADCAST_RESPONSE_LENGTH;

    for (u32 i = 0; i < totalBroadcasts; i++) {
      u32 start = LINK_RAW_WIRELESS_BROADCAST_RESPONSE_LENGTH * i;

      Server server;
      server.id = (u16)result.responses[start];
      server.gameId =
          result.responses[start + 1] & LINK_RAW_WIRELESS_MAX_GAME_ID;
      recoverName(server.gameName, result.responses[start + 1], false);
      recoverName(server.gameName, result.responses[start + 2]);
      recoverName(server.gameName, result.responses[start + 3]);
      recoverName(server.gameName, result.responses[start + 4]);
      recoverName(server.userName, result.responses[start + 5]);
      recoverName(server.userName, result.responses[start + 6]);
      server.nextClientNumber = (result.responses[start] >> 16) & 0xff;

      servers.push_back(server);
    }

    log("state = AUTHENTICATED");
    state = AUTHENTICATED;

    return true;
  }

  bool broadcastReadEnd() {
    bool success =
        sendCommand(LINK_RAW_WIRELESS_COMMAND_BROADCAST_READ_END).success;

    if (!success) {
      reset();
      return false;
    }

    return true;
  }

  bool connect(u16 serverId) {
    bool success = sendCommand(LINK_RAW_WIRELESS_COMMAND_CONNECT,
                               std::vector<u32>{serverId})
                       .success;

    if (!success) {
      reset();
      return false;
    }

    log("state = CONNECTING");
    state = CONNECTING;

    return true;
  }

  bool keepConnecting(ConnectionStatus& response) {
    auto result = sendCommand(LINK_RAW_WIRELESS_COMMAND_IS_FINISHED_CONNECT);
    if (!result.success || result.responses.size() == 0) {
      if (result.responses.size() == 0)
        log("! empty response");
      reset();
      return false;
    }

    if (result.responses[0] == LINK_RAW_WIRELESS_STILL_CONNECTING) {
      response.phase = STILL_CONNECTING;
      return true;
    }

    u8 assignedPlayerId = 1 + (u8)msB32(result.responses[0]);
    if (assignedPlayerId >= LINK_RAW_WIRELESS_MAX_PLAYERS) {
      log("! connection failed (1)");
      reset();
      response.phase = ERROR;
      return false;
    }

    response.phase = SUCCESS;
    response.assignedClientNumber = (u8)msB32(result.responses[0]);

    return true;
  }

  bool finishConnection() {
    auto result = sendCommand(LINK_RAW_WIRELESS_COMMAND_FINISH_CONNECTION);
    if (!result.success || result.responses.size() == 0) {
      if (result.responses.size() == 0)
        log("! empty response");
      reset();
      return false;
    }

    u16 status = msB32(result.responses[0]);
    if ((msB16(status) & 1) == 1) {
      log("! connection failed (2)");
      reset();
      return false;
    }

    u8 assignedPlayerId = 1 + (u8)status;
    sessionState.currentPlayerId = assignedPlayerId;
    log("state = CONNECTED");
    state = CONNECTED;

    return true;
  }

  bool sendData(std::vector<u32> data, u32 _bytes = 0) {
    u32 bytes = _bytes == 0 ? data.size() * 4 : _bytes;
    u32 header = sessionState.currentPlayerId == 0
                     ? bytes
                     : (bytes << (3 + sessionState.currentPlayerId * 5));
    data.insert(data.begin(), header);
    log("using header " + toHex(header));

    bool success =
        sendCommand(LINK_RAW_WIRELESS_COMMAND_SEND_DATA, data).success;

    if (!success) {
      reset();
      return false;
    }

    return true;
  }

  bool sendDataAndWait(std::vector<u32> data,
                       RemoteCommand& remoteCommand,
                       u32 _bytes = 0) {
    u32 bytes = _bytes == 0 ? data.size() * 4 : _bytes;
    u32 header = sessionState.currentPlayerId == 0
                     ? bytes
                     : (bytes << (3 + sessionState.currentPlayerId * 5));
    data.insert(data.begin(), header);
    log("using header " + toHex(header));

    if (!sendCommand(LINK_RAW_WIRELESS_COMMAND_SEND_DATA_AND_WAIT, data)
             .success) {
      reset();
      return false;
    }

    remoteCommand = receiveCommandFromAdapter();

    return remoteCommand.success;
  }

  bool receiveData(ReceiveDataResponse& response) {
    auto result = sendCommand(LINK_RAW_WIRELESS_COMMAND_RECEIVE_DATA);
    response.data = result.responses;

    if (!result.success) {
      reset();
      return false;
    }

    for (u32 i = 0; i < LINK_RAW_WIRELESS_MAX_PLAYERS; i++)
      response.sentBytes[i] = 0;

    if (response.data.size() > 0) {
      u32 header = response.data[0];
      response.data.erase(response.data.begin());
      response.sentBytes[0] = header & 0b1111111;
      response.sentBytes[1] = (header >> 8) & 0b11111;
      response.sentBytes[2] = (header >> 13) & 0b11111;
      response.sentBytes[3] = (header >> 18) & 0b11111;
      response.sentBytes[4] = (header >> 23) & 0b11111;
    }

    return true;
  }

  bool wait(RemoteCommand& remoteCommand) {
    if (!sendCommand(LINK_RAW_WIRELESS_COMMAND_WAIT).success) {
      reset();
      return false;
    }

    remoteCommand = receiveCommandFromAdapter();

    return remoteCommand.success;
  }

  CommandResult sendCommand(u8 type,
                            std::vector<u32> params = std::vector<u32>{}) {
    CommandResult result;
    u16 length = params.size();
    u32 command = buildCommand(type, length);
    u32 r;

    log("sending command 0x" + toHex(command));
    if ((r = transfer(command)) != LINK_RAW_WIRELESS_DATA_REQUEST) {
      logExpectedButReceived(LINK_RAW_WIRELESS_DATA_REQUEST, r);
      return result;
    }

    u32 parameterCount = 0;
    for (auto& param : params) {
      log("sending param" + std::to_string(parameterCount) + ": 0x" +
          toHex(param));
      if ((r = transfer(param)) != LINK_RAW_WIRELESS_DATA_REQUEST) {
        logExpectedButReceived(LINK_RAW_WIRELESS_DATA_REQUEST, r);
        return result;
      }
      parameterCount++;
    }

    log("sending response request");
    u32 response = transfer(LINK_RAW_WIRELESS_DATA_REQUEST);
    u16 header = msB32(response);
    u16 data = lsB32(response);
    u8 responses = msB16(data);
    u8 ack = lsB16(data);

    if (header != LINK_RAW_WIRELESS_COMMAND_HEADER) {
      log("! expected HEADER 0x9966");
      log("! but received 0x" + toHex(header));
      return result;
    }
    if (ack != type + LINK_RAW_WIRELESS_RESPONSE_ACK) {
      if (ack == 0xee && responses == 1) {
        u8 code = (u8)transfer(LINK_RAW_WIRELESS_DATA_REQUEST);
        log("! error received");
        log(code == 1 ? "! invalid state" : "! unknown cmd");
      } else {
        log("! expected ACK 0x" + toHex(type + LINK_RAW_WIRELESS_RESPONSE_ACK));
        log("! but received 0x" + toHex(ack));
      }
      return result;
    }
    log("ack ok! " + std::to_string(responses) + " responses");

    for (u32 i = 0; i < responses; i++) {
      log("response " + std::to_string(i + 1) + "/" +
          std::to_string(responses) + ":");
      u32 responseData = transfer(LINK_RAW_WIRELESS_DATA_REQUEST);
      result.responses.push_back(responseData);
      log("<< " + toHex(responseData));
    }

    result.success = true;
    return result;
  }

  RemoteCommand receiveCommandFromAdapter() {
    RemoteCommand remoteCommand;

    log("setting SPI to SLAVE");
    linkSPI->activate(LinkSPI::Mode::SLAVE);

    log("WAITING for adapter cmd");
    u32 command = linkSPI->transfer(LINK_RAW_WIRELESS_DATA_REQUEST);
    if (!reverseAcknowledge()) {
      linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);
      reset();
      return remoteCommand;
    }

    u16 header = msB32(command);
    u16 data = lsB32(command);
    u8 params = msB16(data);
    u8 commandId = lsB16(data);
    if (header != LINK_RAW_WIRELESS_COMMAND_HEADER) {
      log("! expected HEADER 0x9966");
      log("! but received 0x" + toHex(header));
      linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);
      reset();
      return remoteCommand;
    }
    log("received cmd: " + toHex(commandId) + " (" + std::to_string(params) +
        " params)");

    for (u32 i = 0; i < params; i++) {
      log("param " + std::to_string(i + 1) + "/" + std::to_string(params) +
          ":");
      u32 paramData = linkSPI->transfer(LINK_RAW_WIRELESS_DATA_REQUEST);
      if (!reverseAcknowledge()) {
        linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);
        reset();
        return remoteCommand;
      }
      remoteCommand.params.push_back(paramData);
      log("<< " + toHex(paramData));
    }

    log("sending ack");
    command = linkSPI->transfer(0x99660000 | ((commandId + 0x80) & 0xff));
    if (!reverseAcknowledge()) {
      linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);
      reset();
      return remoteCommand;
    }

    if (command != LINK_RAW_WIRELESS_DATA_REQUEST) {
      log("! expected cmd request");
      log("! but received 0x" + toHex(command));
      linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);
      reset();
      return remoteCommand;
    }

    log("setting SPI to 2Mbps");
    linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);

    remoteCommand.success = true;
    remoteCommand.commandId = commandId;

    return remoteCommand;
  }

  u32 getDeviceTransferLength() {
    return state == SERVING ? LINK_RAW_WIRELESS_MAX_COMMAND_TRANSFER_LENGTH
                            : LINK_RAW_WIRELESS_MAX_CLIENT_TRANSFER_LENGTH;
  }

  State getState() { return state; }
  bool isConnected() { return sessionState.playerCount > 1; }
  bool isSessionActive() { return state == SERVING || state == CONNECTED; }
  u8 playerCount() { return sessionState.playerCount; }
  u8 currentPlayerId() { return sessionState.currentPlayerId; }

  ~LinkRawWireless() {
    delete linkSPI;
    delete linkGPIO;
  }

  struct SessionState {
    u8 playerCount = 1;
    u8 currentPlayerId = 0;
  };

  struct LoginMemory {
    u16 previousGBAData = 0xffff;
    u16 previousAdapterData = 0xffff;
  };

  SessionState sessionState;
  LinkSPI* linkSPI = new LinkSPI();
  LinkGPIO* linkGPIO = new LinkGPIO();
  State state = NEEDS_RESET;
  volatile bool isEnabled = false;

  void recoverName(std::string& name,
                   u32 word,
                   bool includeFirstTwoBytes = true) {
    u32 character = 0;
    if (includeFirstTwoBytes) {
      character = lsB16(lsB32(word));
      if (character > 0)
        name.push_back(character);
      character = msB16(lsB32(word));
      if (character > 0)
        name.push_back(character);
    }
    character = lsB16(msB32(word));
    if (character > 0)
      name.push_back(character);
    character = msB16(msB32(word));
    if (character > 0)
      name.push_back(character);
  }

  bool reset(bool initialize = false) {
    resetState();
    if (initialize)
      stop();
    return initialize && start();
  }

  void resetState() {
    log("state = NEEDS_RESET");
    this->state = NEEDS_RESET;
    this->sessionState.playerCount = 1;
    this->sessionState.currentPlayerId = 0;
  }

  void stop() { linkSPI->deactivate(); }

  bool start() {
    pingAdapter();
    log("setting SPI to 256Kbps");
    linkSPI->activate(LinkSPI::Mode::MASTER_256KBPS);

    if (!login())
      return false;

    wait(LINK_RAW_WIRELESS_TRANSFER_WAIT);

    log("sending HELLO command");
    if (!sendCommand(LINK_RAW_WIRELESS_COMMAND_HELLO).success)
      return false;

    log("setting SPI to 2Mbps");
    linkSPI->activate(LinkSPI::Mode::MASTER_2MBPS);
    log("state = AUTHENTICATED");
    state = AUTHENTICATED;

    return true;
  }

  void pingAdapter() {
    log("setting SO as OUTPUT");
    linkGPIO->setMode(LinkGPIO::Pin::SO, LinkGPIO::Direction::OUTPUT);
    log("setting SD as OUTPUT");
    linkGPIO->setMode(LinkGPIO::Pin::SD, LinkGPIO::Direction::OUTPUT);
    log("setting SD = HIGH");
    linkGPIO->writePin(LinkGPIO::SD, true);
    wait(LINK_RAW_WIRELESS_PING_WAIT);
    log("setting SD = LOW");
    linkGPIO->writePin(LinkGPIO::SD, false);
  }

  bool login() {
    LoginMemory memory;

    log("sending initial login packet");
    if (!exchangeLoginPacket(LINK_RAW_WIRELESS_LOGIN_PARTS[0], 0, memory))
      return false;

    for (u32 i = 0; i < LINK_RAW_WIRELESS_LOGIN_STEPS; i++) {
      log("sending login packet " + std::to_string(i + 1) + "/" +
          std::to_string(LINK_RAW_WIRELESS_LOGIN_STEPS));
      if (!exchangeLoginPacket(LINK_RAW_WIRELESS_LOGIN_PARTS[i],
                               LINK_RAW_WIRELESS_LOGIN_PARTS[i], memory))
        return false;
    }

    return true;
  }

  bool exchangeLoginPacket(u16 data,
                           u16 expectedResponse,
                           LoginMemory& memory) {
    u32 packet = buildU32(~memory.previousAdapterData, data);
    u32 response = transfer(packet, false);

    if (msB32(response) != expectedResponse ||
        lsB32(response) != (u16)~memory.previousGBAData) {
      logExpectedButReceived(
          buildU32(expectedResponse, (u16)~memory.previousGBAData), response);
      return false;
    }

    memory.previousGBAData = data;
    memory.previousAdapterData = expectedResponse;

    return true;
  }

  u32 buildCommand(u8 type, u8 length = 0) {
    return buildU32(LINK_RAW_WIRELESS_COMMAND_HEADER, buildU16(length, type));
  }

  u32 transfer(u32 data, bool customAck = true) {
    if (!customAck)
      wait(LINK_RAW_WIRELESS_TRANSFER_WAIT);

    u32 lines = 0;
    u32 vCount = REG_VCOUNT;
    u32 receivedData = linkSPI->transfer(
        data, [this, &lines, &vCount]() { return cmdTimeout(lines, vCount); },
        false, customAck);

    if (customAck && !acknowledge())
      return LINK_SPI_NO_DATA;

    return receivedData;
  }

  bool acknowledge() {
    u32 lines = 0;
    u32 vCount = REG_VCOUNT;

    linkSPI->_setSOLow();
    while (!linkSPI->_isSIHigh()) {
      if (cmdTimeout(lines, vCount)) {
        log("! ACK 1 failed. I put SO=LOW,");
        log("! but SI didn't become HIGH.");
        return false;
      }
    }
    linkSPI->_setSOHigh();
    while (linkSPI->_isSIHigh()) {
      if (cmdTimeout(lines, vCount)) {
        log("! ACK 2 failed. I put SO=HIGH,");
        log("! but SI didn't become LOW.");
        return false;
      }
    }
    linkSPI->_setSOLow();

    return true;
  }

  bool reverseAcknowledge() {
    u32 lines = 0;
    u32 vCount = REG_VCOUNT;

    while (!linkSPI->_isSIHigh()) {
      if (cmdTimeout(lines, vCount)) {
        log("! REV_ACK failed. I put SO=HIGH,");
        log("! but SI didn't become HIGH.");
        return false;
      }
    }

    return true;
  }

  bool cmdTimeout(u32& lines, u32& vCount) {
    return timeout(LINK_RAW_WIRELESS_CMD_TIMEOUT, lines, vCount);
  }

  bool timeout(u32 limit, u32& lines, u32& vCount) {
    if (REG_VCOUNT != vCount) {
      lines += max((int)REG_VCOUNT - (int)vCount, 0);
      vCount = REG_VCOUNT;
    }

    return lines > limit;
  }

  void wait(u32 verticalLines) {
    u32 count = 0;
    u32 vCount = REG_VCOUNT;

    while (count < verticalLines) {
      if (REG_VCOUNT != vCount) {
        count++;
        vCount = REG_VCOUNT;
      }
    };
  }

  void logExpectedButReceived(u32 expected, u32 received) {
    log("! expected 0x" + toHex(expected));
    log("! but received 0x" + toHex(received));
  }

  template <typename I>
  std::string toHex(I w, size_t hex_len = sizeof(I) << 1) {
    static const char* digits = "0123456789ABCDEF";
    std::string rc(hex_len, '0');
    for (size_t i = 0, j = (hex_len - 1) * 4; i < hex_len; ++i, j -= 4)
      rc[i] = digits[(w >> j) & 0x0f];
    return rc;
  }

  inline __attribute__((always_inline)) void log(std::string str) {
    if (LINK_RAW_WIRELESS_ENABLE_LOGGING)
      logger(str);
  }

  u32 buildU32(u16 msB, u16 lsB) { return (msB << 16) | lsB; }
  u16 buildU16(u8 msB, u8 lsB) { return (msB << 8) | lsB; }
  u16 msB32(u32 value) { return value >> 16; }
  u16 lsB32(u32 value) { return value & 0xffff; }
  u8 msB16(u16 value) { return value >> 8; }
  u8 lsB16(u16 value) { return value & 0xff; }
};

extern LinkRawWireless* linkRawWireless;

#endif  // LINK_RAW_WIRELESS_H
