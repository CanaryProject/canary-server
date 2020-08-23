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

#ifndef FS_PROTOCOL_H_D71405071ACF4137A4B1203899DE80E1
#define FS_PROTOCOL_H_D71405071ACF4137A4B1203899DE80E1

#include <zlib.h>

#include "connection.h"

class Protocol : public std::enable_shared_from_this<Protocol>, public CanaryLib::FlatbuffersParser
{
	public:
		explicit Protocol(){}
		virtual ~Protocol();

		// non-copyable
		Protocol(const Protocol&) = delete;
		Protocol& operator=(const Protocol&) = delete;

    void setConnection(ConnectionWeak_ptr c) {
      connection = c;
    }

		virtual void parsePacket(NetworkMessage&) {}

		virtual void onConnect() {}

		bool isConnectionExpired() const {
			return connection.expired();
		}

		Connection_ptr getConnection() const {
			return connection.lock();
		}

		uint32_t getIP() const;

		//Use this function for autosend messages only
		Wrapper_ptr getOutputBuffer(int32_t size);

		Wrapper_ptr& getCurrentBuffer() {
			return outputBuffer;
		}

		void send(Wrapper_ptr wrapper) const {
			if (auto connection = getConnection()) {
				connection->send(wrapper);
			}
		}

	protected:
		virtual void disconnectClient(const std::string& message) const;

		void disconnect() const {
			if (auto connection = getConnection()) {
				connection->close();
			}
		}
		void setupXTEA(const uint32_t* key) {
      xtea.setKey(key);
		}
		static bool decryptRSA(NetworkMessage& msg);

		void setRawMessages(bool value) {
			rawMessages = value;
		}

		virtual void release(){}

    // Flatbuffer parsers
		void parseLoginData(const CanaryLib::LoginData *login_data);
		void onRecvMessage(CanaryLib::NetworkMessage& msg) override;
		virtual void parseLoginInfo(const CanaryLib::LoginInfo * login_info){}
    virtual bool validateLoginChallenge(const CanaryLib::Challenge *challenge){
      return true;
    }

	private:
		friend class Connection;

		Wrapper_ptr outputBuffer;
		std::unique_ptr<z_stream> defStream;

		ConnectionWeak_ptr connection;
		uint32_t serverSequenceNumber = 0;
		uint32_t clientSequenceNumber = 0;
		bool rawMessages = false;
};

#endif
