#ifndef LINK_WIRELESS_MULTIBOOT_H
#define LINK_WIRELESS_MULTIBOOT_H

#pragma GCC push_options
#pragma GCC optimize("O2")

// --------------------------------------------------------------------------
// A Wireless Multiboot tool to send small ROMs from a GBA to up to 4 slaves.
// --------------------------------------------------------------------------
// Usage:
// - 1) Include this header in your main.cpp file and add:
//       LinkWirelessMultiboot* linkWirelessMultiboot =
//         new LinkWirelessMultiboot();
// - 2) Send the ROM:
//       LinkWirelessMultiboot::Result result = linkWirelessMultiboot->sendRom(
//         romBytes, // for current ROM, use: ((const u8*)MEM_EWRAM)
//         romLength, // in bytes
//         Multiboot", // game name
//         "Test", // user name
//         0xffff, // game id
//         2, // number of players
//         [](LinkWirelessMultiboot::MultibootProgress progress) {
//           // check progress.[state,connectedClients,percentage]
//
//           u16 keys = ~REG_KEYS & KEY_ANY;
//           return keys & KEY_START;
//           // (when this returns true, the transfer will be canceled)
//         }
//       );
//       // `result` should be LinkWirelessMultiboot::Result::SUCCESS
// --------------------------------------------------------------------------

#include <tonc_core.h>
#include "LinkRawWireless.hpp"
#include "LinkWirelessOpenSDK.hpp"

// Enable logging (set `linkWirelessMultiboot->logger` and uncomment to enable)
// #define LINK_WIRELESS_MULTIBOOT_ENABLE_LOGGING

#ifdef LINK_WIRELESS_MULTIBOOT_ENABLE_LOGGING
#include <string>
#define LWMLOG(str) logger(str)
#else
#define LWMLOG(str)
#endif

#define LINK_WIRELESS_MULTIBOOT_MIN_ROM_SIZE (0x100 + 0xc0)
#define LINK_WIRELESS_MULTIBOOT_MAX_ROM_SIZE (256 * 1024)
#define LINK_WIRELESS_MULTIBOOT_MIN_PLAYERS 2
#define LINK_WIRELESS_MULTIBOOT_MAX_PLAYERS 5
#define LINK_WIRELESS_MULTIBOOT_HEADER_SIZE 0xC0
#define LINK_WIRELESS_MULTIBOOT_SETUP_MAGIC 0x003c0000
#define LINK_WIRELESS_MULTIBOOT_SETUP_TX 1
#define LINK_WIRELESS_MULTIBOOT_SETUP_WAIT_TIMEOUT 32
#define LINK_WIRELESS_MULTIBOOT_GAME_ID_MULTIBOOT_FLAG (1 << 15)
#define LINK_WIRELESS_MULTIBOOT_FRAME_LINES 228
#define LINK_WIRELESS_MULTIBOOT_MAX_INFLIGHT_PACKETS 4
#define LINK_WIRELESS_MULTIBOOT_TRY(CALL) \
  if ((lastResult = CALL) != SUCCESS) {   \
    return finish(lastResult);            \
  }

const u8 LINK_WIRELESS_MULTIBOOT_CMD_START[] = {0x00, 0x54, 0x00, 0x00,
                                                0x00, 0x02, 0x00};
const u8 LINK_WIRELESS_MULTIBOOT_CMD_START_SIZE = 7;
const u8 LINK_WIRELESS_MULTIBOOT_BOOTLOADER_HANDSHAKE[][6] = {
    {0x00, 0x00, 0x52, 0x46, 0x55, 0x2d},
    {0x4d, 0x42, 0x2d, 0x44, 0x4c, 0x00}};
const u8 LINK_WIRELESS_MULTIBOOT_BOOTLOADER_HANDSHAKE_SIZE = 6;
const u8 LINK_WIRELESS_MULTIBOOT_ROM_HEADER_PATCH[] = {
    0x52, 0x46, 0x55, 0x2d, 0x4d, 0x42, 0x4f, 0x4f, 0x54, 0x00, 0x00, 0x00};
const u8 LINK_WIRELESS_MULTIBOOT_ROM_HEADER_PATCH_OFFSET = 4;
const u8 LINK_WIRELESS_MULTIBOOT_ROM_HEADER_PATCH_SIZE = 12;

static volatile char LINK_WIRELESS_MULTIBOOT_VERSION[] =
    "LinkWirelessMultiboot/v6.3.0";

class LinkWirelessMultiboot {
 public:
#ifdef LINK_WIRELESS_MULTIBOOT_ENABLE_LOGGING
  typedef void (*Logger)(std::string);
  Logger logger = [](std::string str) {};
#endif

  enum Result {
    SUCCESS,
    INVALID_SIZE,
    INVALID_PLAYERS,
    CANCELED,
    ADAPTER_NOT_DETECTED,
    BAD_HANDSHAKE,
    FAILURE
  };
  enum State { STOPPED, INITIALIZING, WAITING, PREPARING, SENDING, CONFIRMING };

  struct MultibootProgress {
    State state = STOPPED;
    u32 connectedClients = 0;
    u32 percentage = 0;
  };

  template <typename C>
  Result sendRom(const u8* rom,
                 u32 romSize,
                 const char* gameName,
                 const char* userName,
                 const u16 gameId,
                 u8 players,
                 C cancel) {
    if (romSize < LINK_WIRELESS_MULTIBOOT_MIN_ROM_SIZE)
      return INVALID_SIZE;
    if (romSize > LINK_WIRELESS_MULTIBOOT_MAX_ROM_SIZE)
      return INVALID_SIZE;
    if (players < LINK_WIRELESS_MULTIBOOT_MIN_PLAYERS ||
        players > LINK_WIRELESS_MULTIBOOT_MAX_PLAYERS)
      return INVALID_PLAYERS;

    LWMLOG("starting...");
    LINK_WIRELESS_MULTIBOOT_TRY(activate())
    progress.state = INITIALIZING;
    LINK_WIRELESS_MULTIBOOT_TRY(initialize(gameName, userName, gameId, players))

    LWMLOG("waiting for connections...");
    progress.state = WAITING;
    LINK_WIRELESS_MULTIBOOT_TRY(waitForClients(players, cancel))

    LWMLOG("all players are connected");
    progress.state = PREPARING;
    linkRawWireless->wait(LINK_WIRELESS_MULTIBOOT_FRAME_LINES);

    LWMLOG("rom start command...");
    LINK_WIRELESS_MULTIBOOT_TRY(sendRomStartCommand(cancel))

    LWMLOG("SENDING ROM!");
    progress.state = SENDING;
    LINK_WIRELESS_MULTIBOOT_TRY(sendRomBytes(rom, romSize, cancel))

    progress.state = CONFIRMING;
    LINK_WIRELESS_MULTIBOOT_TRY(confirm(cancel))

    LWMLOG("SUCCESS!");
    return finish(SUCCESS);
  }

  ~LinkWirelessMultiboot() {
    delete linkRawWireless;
    delete linkWirelessOpenSDK;
  }

  LinkRawWireless* linkRawWireless = new LinkRawWireless();
  LinkWirelessOpenSDK* linkWirelessOpenSDK = new LinkWirelessOpenSDK();

 private:
  struct PendingTransfer {
    u32 cursor;
    bool ack;
    bool isActive = false;
  };

  struct PendingTransferList {
    std::array<PendingTransfer, LINK_WIRELESS_MULTIBOOT_MAX_INFLIGHT_PACKETS>
        transfers;

    PendingTransfer* max(bool ack = false) {
      int maxCursor = -1;
      int maxI = -1;
      for (u32 i = 0; i < LINK_WIRELESS_MULTIBOOT_MAX_INFLIGHT_PACKETS; i++) {
        if (transfers[i].isActive && (int)transfers[i].cursor > maxCursor &&
            (!ack || transfers[i].ack)) {
          maxCursor = transfers[i].cursor;
          maxI = i;
        }
      }
      return maxI > -1 ? &transfers[maxI] : NULL;
    }

    PendingTransfer* minWithoutAck() {
      u32 minCursor = 0xffffffff;
      int minI = -1;
      for (u32 i = 0; i < LINK_WIRELESS_MULTIBOOT_MAX_INFLIGHT_PACKETS; i++) {
        if (transfers[i].isActive && transfers[i].cursor < minCursor &&
            !transfers[i].ack) {
          minCursor = transfers[i].cursor;
          minI = i;
        }
      }
      return minI > -1 ? &transfers[minI] : NULL;
    }

    void addIfNeeded(u32 newCursor) {
      auto maxTransfer = max();
      if (maxTransfer != NULL && newCursor <= maxTransfer->cursor)
        return;

      for (u32 i = 0; i < LINK_WIRELESS_MULTIBOOT_MAX_INFLIGHT_PACKETS; i++) {
        if (!transfers[i].isActive) {
          transfers[i].cursor = newCursor;
          transfers[i].ack = false;
          transfers[i].isActive = true;
          break;
        }
      }
    }

    int ack(LinkWirelessOpenSDK::SequenceNumber sequence) {
      int index = findIndex(sequence);
      if (index == -1)
        return -1;

      transfers[index].ack = true;

      auto maxAckTransfer = max(true);
      bool canUpdateCursor =
          maxAckTransfer != NULL && isAckCompleteUpTo(maxAckTransfer->cursor);

      if (canUpdateCursor)
        cleanup();

      return canUpdateCursor ? maxAckTransfer->cursor + 1 : -1;
    }

    void cleanup() {
      for (u32 i = 0; i < LINK_WIRELESS_MULTIBOOT_MAX_INFLIGHT_PACKETS; i++) {
        if (transfers[i].isActive && transfers[i].ack)
          transfers[i].isActive = false;
      }
    }

    bool isFull() {
      return size() == LINK_WIRELESS_MULTIBOOT_MAX_INFLIGHT_PACKETS;
    }

    u32 size() {
      u32 size = 0;
      for (u32 i = 0; i < LINK_WIRELESS_MULTIBOOT_MAX_INFLIGHT_PACKETS; i++)
        if (transfers[i].isActive)
          size++;
      return size;
    }

   private:
    bool isAckCompleteUpTo(u32 cursor) {
      for (u32 i = 0; i < LINK_WIRELESS_MULTIBOOT_MAX_INFLIGHT_PACKETS; i++)
        if (transfers[i].isActive && !transfers[i].ack &&
            transfers[i].cursor < cursor)
          return false;
      return true;
    }

    int findIndex(LinkWirelessOpenSDK::SequenceNumber sequence) {
      for (u32 i = 0; i < LINK_WIRELESS_MULTIBOOT_MAX_INFLIGHT_PACKETS; i++) {
        if (transfers[i].isActive &&
            LinkWirelessOpenSDK::SequenceNumber::fromPacketId(
                transfers[i].cursor) == sequence) {
          return i;
        }
      }

      return -1;
    }
  };

  struct Transfer {
    u32 cursor = 0;
    PendingTransferList pendingTransferList;

    u32 nextCursor(bool canSendInflightPackets) {
      u32 pendingCount = pendingTransferList.size();

      if (canSendInflightPackets && pendingCount > 0 &&
          pendingCount < LINK_WIRELESS_MULTIBOOT_MAX_INFLIGHT_PACKETS) {
        return pendingTransferList.max()->cursor + 1;
      } else {
        auto minWithoutAck = pendingTransferList.minWithoutAck();
        return minWithoutAck != NULL ? minWithoutAck->cursor : cursor;
      }
    }

    void addIfNeeded(u32 newCursor) {
      if (newCursor >= cursor)
        pendingTransferList.addIfNeeded(newCursor);
    }

    u32 transferred() {
      return cursor * LINK_WIRELESS_OPEN_SDK_MAX_PAYLOAD_SERVER;
    }

    LinkWirelessOpenSDK::SequenceNumber sequence() {
      return LinkWirelessOpenSDK::SequenceNumber::fromPacketId(cursor);
    }
  };

  MultibootProgress progress;
  Result lastResult;
  LinkWirelessOpenSDK::ClientSDKHeader lastValidHeader;

  __attribute__((noinline)) Result activate() {
    if (!linkRawWireless->activate()) {
      LWMLOG("! adapter not detected");
      return ADAPTER_NOT_DETECTED;
    }
    LWMLOG("activated");

    return SUCCESS;
  }

  __attribute__((noinline)) Result initialize(const char* gameName,
                                              const char* userName,
                                              const u16 gameId,
                                              u8 players) {
    if (!linkRawWireless->setup(players, LINK_WIRELESS_MULTIBOOT_SETUP_TX,
                                LINK_WIRELESS_MULTIBOOT_SETUP_WAIT_TIMEOUT,
                                LINK_WIRELESS_MULTIBOOT_SETUP_MAGIC)) {
      LWMLOG("! setup failed");
      return FAILURE;
    }
    LWMLOG("setup ok");

    if (!linkRawWireless->broadcast(
            gameName, userName,
            gameId | LINK_WIRELESS_MULTIBOOT_GAME_ID_MULTIBOOT_FLAG)) {
      LWMLOG("! broadcast failed");
      return FAILURE;
    }
    LWMLOG("broadcast data set");

    if (!linkRawWireless->startHost()) {
      LWMLOG("! start host failed");
      return FAILURE;
    }
    LWMLOG("host started");

    return SUCCESS;
  }

  template <typename C>
  __attribute__((noinline)) Result waitForClients(u8 players, C cancel) {
    LinkRawWireless::AcceptConnectionsResponse acceptResponse;

    u32 currentPlayers = 1;
    while (linkRawWireless->playerCount() < players) {
      if (cancel(progress))
        return finish(CANCELED);

      linkRawWireless->acceptConnections(acceptResponse);

      if (linkRawWireless->playerCount() > currentPlayers) {
        currentPlayers = linkRawWireless->playerCount();
        progress.connectedClients = currentPlayers - 1;

        u8 lastClientNumber =
            acceptResponse
                .connectedClients[acceptResponse.connectedClientsSize - 1]
                .clientNumber;
        LINK_WIRELESS_MULTIBOOT_TRY(handshakeClient(lastClientNumber, cancel))
      }
    }

    return SUCCESS;
  }

  template <typename C>
  Result handshakeClient(u8 clientNumber, C cancel) {
    LinkWirelessOpenSDK::ClientPacket handshakePackets[2];
    bool hasReceivedName = false;

    LWMLOG("new client: " + std::to_string(clientNumber));
    LINK_WIRELESS_MULTIBOOT_TRY(exchangeData(
        clientNumber,
        [this](LinkRawWireless::ReceiveDataResponse& response) {
          return sendAndExpectData(toArray(), 0, 1, response);
        },
        [](LinkWirelessOpenSDK::ClientPacket packet) { return true; }, cancel))
    // (initial client packet received)

    LWMLOG("handshake (1/2)...");
    LINK_WIRELESS_MULTIBOOT_TRY(exchangeACKData(
        clientNumber,
        [](LinkWirelessOpenSDK::ClientPacket packet) {
          auto header = packet.header;
          return header.n == 2 &&
                 header.commState == LinkWirelessOpenSDK::CommState::STARTING;
        },
        cancel))
    // (n = 2, commState = 1)

    LWMLOG("handshake (2/2)...");
    LINK_WIRELESS_MULTIBOOT_TRY(exchangeACKData(
        clientNumber,
        [&handshakePackets](LinkWirelessOpenSDK::ClientPacket packet) {
          auto header = packet.header;
          bool isValid =
              header.n == 1 && header.phase == 0 &&
              header.commState == LinkWirelessOpenSDK::CommState::COMMUNICATING;
          if (isValid)
            handshakePackets[0] = packet;
          return isValid;
        },
        cancel))
    // (n = 1, commState = 2)

    LWMLOG("receiving name...");
    LINK_WIRELESS_MULTIBOOT_TRY(exchangeACKData(
        clientNumber,
        [this, &handshakePackets,
         &hasReceivedName](LinkWirelessOpenSDK::ClientPacket packet) {
          auto header = packet.header;
          lastValidHeader = header;
          if (header.n == 1 && header.phase == 1 &&
              header.commState ==
                  LinkWirelessOpenSDK::CommState::COMMUNICATING) {
            handshakePackets[1] = packet;
            hasReceivedName = true;
          }
          return header.commState == LinkWirelessOpenSDK::CommState::OFF;
        },
        cancel))
    // (commState = 0)

    LWMLOG("validating name...");
    for (u32 i = 0; i < 2; i++) {
      auto receivedPayload = handshakePackets[i].payload;
      auto expectedPayload = LINK_WIRELESS_MULTIBOOT_BOOTLOADER_HANDSHAKE[i];

      for (u32 j = 0; j < LINK_WIRELESS_MULTIBOOT_BOOTLOADER_HANDSHAKE_SIZE;
           j++) {
        if (!hasReceivedName || receivedPayload[j] != expectedPayload[j]) {
          LWMLOG("! bad payload");
          return finish(BAD_HANDSHAKE);
        }
      }
    }

    LWMLOG("draining queue...");
    bool hasFinished = false;
    while (!hasFinished) {
      if (cancel(progress))
        return finish(CANCELED);

      LinkRawWireless::ReceiveDataResponse response;
      LINK_WIRELESS_MULTIBOOT_TRY(sendAndExpectData(toArray(), 0, 1, response))
      auto childrenData = linkWirelessOpenSDK->getChildrenData(response);
      hasFinished = childrenData.responses[clientNumber].packetsSize == 0;
    }
    // (no more client packets)

    LWMLOG("client " + std::to_string(clientNumber) + " accepted");

    return SUCCESS;
  }

  template <typename C>
  __attribute__((noinline)) Result sendRomStartCommand(C cancel) {
    for (u32 i = 0; i < progress.connectedClients; i++) {
      LINK_WIRELESS_MULTIBOOT_TRY(exchangeNewData(
          i,
          linkWirelessOpenSDK->createServerBuffer(
              LINK_WIRELESS_MULTIBOOT_CMD_START,
              LINK_WIRELESS_MULTIBOOT_CMD_START_SIZE,
              {1, 0, LinkWirelessOpenSDK::CommState::STARTING}, 1 << i),
          cancel))
    }

    return SUCCESS;
  }

  template <typename C>
  __attribute__((noinline)) Result sendRomBytes(const u8* rom,
                                                u32 romSize,
                                                C cancel) {
    LinkWirelessOpenSDK::ChildrenData childrenData;
    std::array<Transfer, LINK_WIRELESS_MULTIBOOT_MAX_PLAYERS - 1> transfers;
    u8 firstPagePatch[LINK_WIRELESS_OPEN_SDK_MAX_PAYLOAD_SERVER];
    for (u32 i = 0; i < LINK_WIRELESS_OPEN_SDK_MAX_PAYLOAD_SERVER; i++) {
      firstPagePatch[i] =
          i >= LINK_WIRELESS_MULTIBOOT_ROM_HEADER_PATCH_OFFSET &&
                  i < LINK_WIRELESS_MULTIBOOT_ROM_HEADER_PATCH_OFFSET +
                          LINK_WIRELESS_MULTIBOOT_ROM_HEADER_PATCH_SIZE
              ? LINK_WIRELESS_MULTIBOOT_ROM_HEADER_PATCH
                    [i - LINK_WIRELESS_MULTIBOOT_ROM_HEADER_PATCH_OFFSET]
              : rom[i];
    }

    progress.percentage = 0;
    u32 minClient = 0;

    while (transfers[minClient = findMinClient(transfers)].transferred() <
           romSize) {
      if (cancel(progress))
        return finish(CANCELED);

      u32 cursor = findMinCursor(transfers);
      u32 offset = cursor * LINK_WIRELESS_OPEN_SDK_MAX_PAYLOAD_SERVER;
      auto sequence = LinkWirelessOpenSDK::SequenceNumber::fromPacketId(cursor);
      const u8* bufferToSend = cursor == 0 ? (const u8*)firstPagePatch : rom;

      auto sendBuffer = linkWirelessOpenSDK->createServerBuffer(
          bufferToSend, romSize, sequence, 0b1111, offset);

      for (u32 i = 0; i < progress.connectedClients; i++)
        transfers[i].addIfNeeded(cursor);

      LinkRawWireless::ReceiveDataResponse response;
      LINK_WIRELESS_MULTIBOOT_TRY(sendAndExpectData(sendBuffer, response))
      childrenData = linkWirelessOpenSDK->getChildrenData(response);

      for (u32 i = 0; i < progress.connectedClients; i++) {
        for (u32 j = 0; j < childrenData.responses[i].packetsSize; j++) {
          auto header = childrenData.responses[i].packets[j].header;

          if (header.isACK) {
            int newAckCursor =
                transfers[i].pendingTransferList.ack(header.sequence());
            if (newAckCursor > -1)
              transfers[i].cursor = newAckCursor;
          }
        }
      }

      u32 newPercentage =
          min(transfers[findMinClient(transfers)].transferred() * 100 / romSize,
              100);
      if (newPercentage != progress.percentage) {
        progress.percentage = newPercentage;
        LWMLOG("-> " + std::to_string(newPercentage));
      }
    }

    return SUCCESS;
  }

  __attribute__((noinline)) u32 findMinClient(
      std::array<Transfer, LINK_WIRELESS_MULTIBOOT_MAX_PLAYERS - 1>&
          transfers) {
    u32 minTransferredBytes = 0xffffffff;
    u32 minClient = 0;

    for (u32 i = 0; i < progress.connectedClients; i++) {
      u32 transferred = transfers[i].transferred();
      if (transferred < minTransferredBytes) {
        minTransferredBytes = transferred;
        minClient = i;
      }
    }

    return minClient;
  }

  __attribute__((noinline)) u32 findMinCursor(
      std::array<Transfer, LINK_WIRELESS_MULTIBOOT_MAX_PLAYERS - 1>&
          transfers) {
    u32 minNextCursor = 0xffffffff;

    bool canSendInflightPackets = true;
    for (u32 i = 0; i < progress.connectedClients; i++) {
      if (transfers[i].pendingTransferList.isFull())
        canSendInflightPackets = false;
    }

    for (u32 i = 0; i < progress.connectedClients; i++) {
      u32 nextCursor = transfers[i].nextCursor(canSendInflightPackets);
      if (nextCursor < minNextCursor)
        minNextCursor = nextCursor;
    }

    return minNextCursor;
  }

  template <typename C>
  __attribute__((noinline)) Result confirm(C cancel) {
    LWMLOG("confirming (1/2)...");
    for (u32 i = 0; i < progress.connectedClients; i++) {
      LINK_WIRELESS_MULTIBOOT_TRY(exchangeNewData(
          i,
          linkWirelessOpenSDK->createServerBuffer(
              {}, 0, {0, 0, LinkWirelessOpenSDK::CommState::ENDING}, 1 << i),
          cancel))
    }

    LWMLOG("confirming (2/2)...");
    for (u32 i = 0; i < progress.connectedClients; i++) {
      LinkRawWireless::ReceiveDataResponse response;
      LINK_WIRELESS_MULTIBOOT_TRY(sendAndExpectData(
          linkWirelessOpenSDK->createServerBuffer(
              {}, 0, {1, 0, LinkWirelessOpenSDK::CommState::OFF}, 1 << i),
          response))
    }

    return SUCCESS;
  }

  template <typename C>
  __attribute__((noinline)) Result exchangeNewData(
      u8 clientNumber,
      LinkWirelessOpenSDK::SendBuffer<LinkWirelessOpenSDK::ServerSDKHeader>
          sendBuffer,
      C cancel) {
    LINK_WIRELESS_MULTIBOOT_TRY(exchangeData(
        clientNumber,
        [this, &sendBuffer](LinkRawWireless::ReceiveDataResponse& response) {
          return sendAndExpectData(sendBuffer, response);
        },
        [&sendBuffer](LinkWirelessOpenSDK::ClientPacket packet) {
          auto header = packet.header;
          return header.isACK == 1 &&
                 header.sequence() == sendBuffer.header.sequence();
        },
        cancel))

    return SUCCESS;
  }

  template <typename V, typename C>
  __attribute__((noinline)) Result exchangeACKData(u8 clientNumber,
                                                   V validatePacket,
                                                   C cancel) {
    LINK_WIRELESS_MULTIBOOT_TRY(exchangeData(
        clientNumber,
        [this, clientNumber](LinkRawWireless::ReceiveDataResponse& response) {
          return sendAndExpectData(linkWirelessOpenSDK->createServerACKBuffer(
                                       lastValidHeader, clientNumber),
                                   response);
        },
        validatePacket, cancel))

    return SUCCESS;
  }

  template <typename F, typename V, typename C>
  __attribute__((noinline)) Result exchangeData(u8 clientNumber,
                                                F sendAction,
                                                V validatePacket,
                                                C cancel) {
    bool hasFinished = false;
    while (!hasFinished) {
      if (cancel(progress))
        return finish(CANCELED);

      LinkRawWireless::ReceiveDataResponse response;
      LINK_WIRELESS_MULTIBOOT_TRY(sendAction(response))
      auto childrenData = linkWirelessOpenSDK->getChildrenData(response);

      for (u32 i = 0; i < childrenData.responses[clientNumber].packetsSize;
           i++) {
        auto packet = childrenData.responses[clientNumber].packets[i];
        auto header = packet.header;
        if (validatePacket(packet)) {
          hasFinished = true;
          lastValidHeader = header;
          break;
        }
      }
    }

    return SUCCESS;
  }

  __attribute__((noinline)) Result sendAndExpectData(
      LinkWirelessOpenSDK::SendBuffer<LinkWirelessOpenSDK::ServerSDKHeader>
          sendBuffer,
      LinkRawWireless::ReceiveDataResponse& response) {
    return sendAndExpectData(sendBuffer.data, sendBuffer.dataSize,
                             sendBuffer.totalByteCount, response);
  }

  __attribute__((noinline)) Result sendAndExpectData(
      std::array<u32, LINK_RAW_WIRELESS_MAX_COMMAND_TRANSFER_LENGTH> data,
      u32 dataSize,
      u32 _bytes,
      LinkRawWireless::ReceiveDataResponse& response) {
    LinkRawWireless::RemoteCommand remoteCommand;
    bool success = false;

    success =
        linkRawWireless->sendDataAndWait(data, dataSize, remoteCommand, _bytes);
    if (!success) {
      LWMLOG("! sendDataAndWait failed");
      return FAILURE;
    }

    if (remoteCommand.commandId != 0x28) {
      LWMLOG("! expected EVENT 0x28");
      LWMLOG("! but got " + toHex(remoteCommand.commandId));
      return FAILURE;
    }

    if (remoteCommand.paramsSize > 0) {
      u8 expectedActiveChildren = 0;
      for (u32 i = 0; i < progress.connectedClients; i++)
        expectedActiveChildren |= 1 << i;
      u8 activeChildren =
          (remoteCommand.params[0] >> 8) & expectedActiveChildren;

      if (activeChildren != expectedActiveChildren) {
        LWMLOG("! client timeout [" + std::to_string(activeChildren) + "]");
        LWMLOG("! vs expected: [" + std::to_string(expectedActiveChildren) +
               "]");
        return FAILURE;
      }
    }

    success = linkRawWireless->receiveData(response);
    if (!success) {
      LWMLOG("! receiveData failed");
      return FAILURE;
    }

    return SUCCESS;
  }

  __attribute__((noinline)) Result finish(Result result) {
    linkRawWireless->deactivate();
    progress.state = STOPPED;
    progress.connectedClients = 0;
    progress.percentage = 0;
    return result;
  }

#ifdef LINK_WIRELESS_MULTIBOOT_ENABLE_LOGGING
  template <typename I>
  std::string toHex(I w, size_t hex_len = sizeof(I) << 1) {
    static const char* digits = "0123456789ABCDEF";
    std::string rc(hex_len, '0');
    for (size_t i = 0, j = (hex_len - 1) * 4; i < hex_len; ++i, j -= 4)
      rc[i] = digits[(w >> j) & 0x0f];
    return rc;
  }
#endif

  template <typename... Args>
  std::array<u32, LINK_RAW_WIRELESS_MAX_COMMAND_TRANSFER_LENGTH> toArray(
      Args... args) {
    return {static_cast<u32>(args)...};
  }
};

extern LinkWirelessMultiboot* linkWirelessMultiboot;

#undef LWMLOG

#pragma GCC pop_options

#endif  // LINK_WIRELESS_MULTIBOOT_H