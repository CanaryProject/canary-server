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

#ifndef FS_NETWORKMESSAGE_H_B853CFED58D1413A87ACED07B2926E03
#define FS_NETWORKMESSAGE_H_B853CFED58D1413A87ACED07B2926E03

#include "const.h"

class Item;
class Creature;
class Player;
struct Position;
class RSA;

class NetworkMessage
{
	public:
		NetworkMessage() = default;

		void reset() {
			m_info = {};
		}

		// simply read functions for incoming message
		uint8_t getByte() {
			if (!canRead(1)) {
				return 0;
			}

			return buffer[m_info.m_bufferPos++];
		}

		uint8_t getPreviousByte() {
			return buffer[--m_info.m_bufferPos];
		}

		template<typename T>
		T get() {
			if (!canRead(sizeof(T))) {
				return 0;
			}

			T v;
			memcpy(&v, buffer + m_info.m_bufferPos, sizeof(T));
			m_info.m_bufferPos += sizeof(T);
			return v;
		}

		std::string getString(uint16_t stringLen = 0);
		Position getPosition();

		// skips count unknown/unused bytes in an incoming message
		void skipBytes(int16_t count) {
			m_info.m_bufferPos += count;
		}

		// simply write functions for outgoing message
		void addByte(uint8_t value) {
			if (!canAdd(1)) {
				return;
			}

			buffer[m_info.m_bufferPos++] = value;
			m_info.m_messageSize++;
		}

		template<typename T>
		void add(T value) {
			if (!canAdd(sizeof(T))) {
				return;
			}

			memcpy(buffer + m_info.m_bufferPos, &value, sizeof(T));
			m_info.m_bufferPos += sizeof(T);
			m_info.m_messageSize += sizeof(T);
		}

		void addBytes(const char* bytes, size_t size);
		void addPaddingBytes(size_t n);

		void addString(const std::string& value);

		void addDouble(double value, uint8_t precision = 2);

		// write functions for complex types
		void addPosition(const Position& pos);
		void addItemId(uint16_t itemId);

		CanaryLib::MsgSize_t getLength() const {
			return m_info.m_messageSize;
		}

		void setLength(CanaryLib::MsgSize_t newLength) {
			m_info.m_messageSize = newLength;
		}

		CanaryLib::MsgSize_t getBufferPosition() const {
			return m_info.m_bufferPos;
		}

		void setBufferPosition(CanaryLib::MsgSize_t newPosition) {
			m_info.m_bufferPos = newPosition;
		}

		uint16_t getLengthHeader() const {
			return static_cast<uint16_t>(buffer[0] | buffer[1] << 8);
		}

		bool isOverrun() const {
			return m_info.overflow;
		}

		uint8_t* getBuffer() {
			return buffer;
		}

		const uint8_t* getBuffer() const {
			return buffer;
		}

		uint8_t* getBodyBuffer() {
			m_info.m_bufferPos = CanaryLib::HEADER_LENGTH;
			return buffer + CanaryLib::HEADER_LENGTH;
		}

	protected:
		struct NetworkMessageInfo {
			CanaryLib::MsgSize_t m_messageSize = 0;
			CanaryLib::MsgSize_t m_bufferPos = CanaryLib::MAX_HEADER_SIZE;
			bool overflow = false;
		};

		NetworkMessageInfo m_info;
		uint8_t buffer[CanaryLib::NETWORKMESSAGE_MAXSIZE];

	private:
		bool canAdd(size_t size) const {
			return (size + m_info.m_bufferPos) < CanaryLib::MAX_BODY_LENGTH;
		}

		bool canRead(int32_t size) {
			if ((m_info.m_bufferPos + size) > (m_info.m_messageSize + 8) || size >= (CanaryLib::NETWORKMESSAGE_MAXSIZE - m_info.m_bufferPos)) {
				m_info.overflow = true;
				return false;
			}
			return true;
		}
};

#endif // #ifndef __NETWORK_MESSAGE_H__
