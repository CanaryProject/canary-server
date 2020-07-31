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

#include "otpch.h"
#include "tasks.h"

#include "configmanager.h"
#include "protocol.h"
#include "outputmessage.h"
#include "rsa.h"

Protocol::~Protocol(){}

void Protocol::onSendMessage(const Wrapper_ptr& wrapper)
{
	if (!rawMessages) {
		if (encryptionEnabled) {
      wrapper->encryptXTEA(xtea);
    }

    wrapper->serialize();
  }
}

bool Protocol::onRecvMessage(NetworkMessage& msg)
{
	using ProtocolWeak_ptr = std::weak_ptr<Protocol>;
	ProtocolWeak_ptr protocolWeak = std::weak_ptr<Protocol>(shared_from_this());

	std::function<void (void)> callback = [protocolWeak, &msg]() {
		if (auto protocol = protocolWeak.lock()) {
			if (auto connection = protocol->getConnection()) {
				protocol->parsePacket(msg);
				connection->recv();
			}
		}
	};
	g_dispatcher().addTask(callback);
	return true;
}

Wrapper_ptr Protocol::getOutputBuffer(int32_t size)
{
	//dispatcher thread
	if (!outputBuffer) {
		outputBuffer = OutputMessagePool::getOutputMessage();
	} else if ((outputBuffer->size() + size) > CanaryLib::MAX_PROTOCOL_BODY_LENGTH) {
		send(outputBuffer);
		outputBuffer = OutputMessagePool::getOutputMessage();
	}
	return outputBuffer;
}

bool Protocol::decryptRSA(NetworkMessage& msg)
{
	if ((msg.getLength() - msg.getBufferPosition()) < 128) {
		return false;
	}

	g_RSA().decrypt(reinterpret_cast<char*>(msg.getBuffer()) + msg.getBufferPosition()); //does not break strict aliasing
	return (msg.readByte() == 0);
}

uint32_t Protocol::getIP() const
{
	if (auto connection = getConnection()) {
		return connection->getIP();
	}

	return 0;
}
