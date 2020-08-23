/**
 * The Forgotten Server - a free and open-source MMORPG server emulator
 * Copyright (C) 2020  Mark Samman <mark.samman@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "flatbuffers_wrapper_pool.h"
#include "game.h"
#include "protocol.h"
#include "otpch.h"
#include "tasks.h"
#include "rsa.h"

Protocol::~Protocol(){}

void Protocol::onRecvMessage(CanaryLib::NetworkMessage& msg)
{
	using ProtocolWeak_ptr = std::weak_ptr<Protocol>;
	ProtocolWeak_ptr protocolWeak = std::weak_ptr<Protocol>(shared_from_this());

	std::function<void (void)> callback = [protocolWeak, &msg]() {
		if (auto protocol = protocolWeak.lock()) {
			if (auto connection = protocol->getConnection()) {
				protocol->parsePacket((NetworkMessage&) msg);
			}
		}
	};
	g_dispatcher().addTask(callback);
}

Wrapper_ptr Protocol::getOutputBuffer(int32_t size)
{
  if (!outputBuffer) {
		outputBuffer = FlatbuffersWrapperPool::getOutputWrapper();
    return outputBuffer;
  }
  bool overflow = (outputBuffer->Size() + size) > CanaryLib::WRAPPER_MAX_SIZE_TO_CONCAT;
  bool makeNewOutput = outputBuffer->isWriteLocked() || overflow;

	//dispatcher thread
	if (makeNewOutput) {
		send(outputBuffer);
		outputBuffer = FlatbuffersWrapperPool::getOutputWrapper();
	}
	return outputBuffer;
}

uint32_t Protocol::getIP() const
{
	if (auto connection = getConnection()) {
		return connection->getIP();
	}

	return 0;
}

void Protocol::disconnectClient(const std::string& message) const
{
  Wrapper_ptr wrapper = FlatbuffersWrapperPool::getOutputWrapper();
  flatbuffers::FlatBufferBuilder &fbb = wrapper->Builder();

  auto error_message = fbb.CreateString(message);
  auto error = CanaryLib::CreateErrorData(fbb, error_message);
  wrapper->add(error.Union(), CanaryLib::DataType_ErrorData);
  
  send(wrapper);

	disconnect();
}

void Protocol::parseLoginData(const CanaryLib::LoginData *login_data) {
  if (!login_data) {
    disconnectClient("Malformed login data");
    return; 
  }

  switch (g_game().getGameState()) {
    case GAME_STATE_SHUTDOWN:
      disconnect();
      return;
    case GAME_STATE_STARTUP:
      disconnectClient("Gameworld is starting up.\nPlease wait.");
      return; 
    case GAME_STATE_MAINTAIN:
      disconnectClient("Gameworld is under maintenance..\nPlease re-connect in a while.");
      return;
    default:
      break;
  }

  if (!validateLoginChallenge(login_data->challenge())) {
    disconnectClient("Invalid connection request.");
    return;
  }

  if (login_data->client() != CanaryLib::Client_t_CANARY) {
    char msg[] = {
      0x4d, 0x79, 0x20, 0x79, 0x65, 0x6c, 0x6c, 0x6f, 0x77, 0x20, 0x6c, 0x69, 0x74, 0x74, 0x6c, 0x65,
      0x20, 0x63, 0x68, 0x69, 0x63, 0x6b, 0x65, 0x6e, 0x2c, 0x20, 0x79, 0x6f, 0x75, 0x20, 0x64, 0x6f,
      0x6e, 0x27, 0x74, 0x20, 0x66, 0x69, 0x74, 0x20, 0x69, 0x6e, 0x20, 0x74, 0x68, 0x69, 0x73, 0x20,
      0x68, 0x61, 0x6e, 0x64, 0x21, 0x20, 0x4f, 0x6e, 0x6c, 0x79, 0x20, 0x63, 0x61, 0x6e, 0x61, 0x72,
      0x69, 0x65, 0x73, 0x20, 0x63, 0x61, 0x6e, 0x20, 0x66, 0x6c, 0x79, 0x20, 0x77, 0x69, 0x74, 0x68,
      0x20, 0x75, 0x73, 0x21, 0x0a
    };
    disconnectClient(std::string(msg));
    return;
  }

  auto enc_login_info = login_data->login_info();
  if (!enc_login_info) {
    disconnectClient("Malformed login data");
    return; 
  }

  size_t buffer_size = enc_login_info->size();

  if (enc_login_info && buffer_size == CanaryLib::RSA_SIZE) {
    uint8_t *login_info_buffer = (uint8_t *) enc_login_info->Data();
    g_RSA().decrypt((char *) login_info_buffer);

    // First RSA byte must be 0
    if (login_info_buffer[0]) {
      disconnectClient("Invalid RSA encryption.");
      return;
    }
    
    auto login_info = CanaryLib::GetLoginInfo(login_info_buffer + sizeof(uint8_t));

    parseLoginInfo(login_info);
  }
}
