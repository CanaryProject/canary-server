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

#include <thread>

#include "configmanager.h"
#include "connection.h"
#include "protocol.h"
#include "tasks.h"
#include "server.h"

Connection_ptr ConnectionManager::createConnection(boost::asio::io_service& io_service, ConstServicePort_ptr servicePort)
{
	std::lock_guard<std::mutex> lockClass(connectionManagerLock);

	auto connection = std::make_shared<Connection>(io_service, servicePort);
	connections.insert(connection);
	return connection;
}

void ConnectionManager::releaseConnection(const Connection_ptr& connection)
{
	std::lock_guard<std::mutex> lockClass(connectionManagerLock);

	connections.erase(connection);
}

void ConnectionManager::closeAll()
{
	std::lock_guard<std::mutex> lockClass(connectionManagerLock);

	for (const auto& connection : connections) {
		try {
			boost::system::error_code error;
			connection->socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
			connection->socket.close(error);
		} catch (boost::system::system_error&) {
		}
	}
	connections.clear();
}

// Connection

void Connection::close(bool force)
{
	//any thread
	ConnectionManager::getInstance().releaseConnection(shared_from_this());

	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);

	if (protocol) {
		g_dispatcher().addTask(std::bind(&Protocol::release, protocol));
	}

	if (messageQueue.empty() || force) {
		closeSocket();
	} else {
		//will be closed by the destructor or onWriteOperation
	}
}

void Connection::closeSocket()
{
	if (socket.is_open()) {
		try {
			readTimer.cancel();
			writeTimer.cancel();
			boost::system::error_code error;
			socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, error);
			socket.close(error);
		} catch (boost::system::system_error& e) {
			std::cout << "[Network error - Connection::closeSocket] " << e.what() << std::endl;
		}
	}
}

Connection::~Connection()
{
	closeSocket();
}

void Connection::accept(Protocol_ptr protocol /* = nullptr */)
{
  if (protocol) {
	  this->protocol = protocol;
	  g_dispatcher().addTask(std::bind(&Protocol::onConnect, protocol));
  }
  resumeWork(true);
}

void Connection::parseHeader(const boost::system::error_code& error)
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	readTimer.cancel();

	if (error) {
		close(FORCE_CLOSE);
		return;
	}

	uint32_t timePassed = std::max<uint32_t>(1, (time(nullptr) - timeConnected) + 1);
	if ((++packetsSent / timePassed) > static_cast<uint32_t>(g_config().getNumber(ConfigManager::MAX_PACKETS_PER_SECOND))) {
    spdlog::warn("{} disconnected for exceeding packet per second limit.", convertIPToString(getIP()));
		close();
		return;
	}

	if (timePassed > 2) {
		timeConnected = time(nullptr);
		packetsSent = 0;
	}

  inputWrapper.copy(boost::asio::buffer_cast<const uint8_t*>(m_inputStream.data()), true);
	uint16_t size = inputWrapper.size();
  
	if (size == 0 || size > INPUTMESSAGE_MAXSIZE) {
		close(FORCE_CLOSE);
		return;
	}

	try {
		readTimer.expires_from_now(boost::posix_time::seconds(CONNECTION_READ_TIMEOUT));
		readTimer.async_wait(std::bind(&Connection::handleTimeout, std::weak_ptr<Connection>(shared_from_this()), std::placeholders::_1));

		// Read packet content
		boost::asio::async_read(socket,
		                        boost::asio::buffer(m_inputStream.prepare(size)),
		                        std::bind(&Connection::parsePacket, shared_from_this(), std::placeholders::_1));
	} catch (boost::system::system_error& e) {
    spdlog::error("[Connection::parseHeader]: Network error during header reading - {}", e.what());
		close(FORCE_CLOSE);
	}
}

void Connection::parsePacket(const boost::system::error_code& error)
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	readTimer.cancel();

	if (error) {
		close(FORCE_CLOSE);
		return;
	}

  inputWrapper.write(boost::asio::buffer_cast<const uint8_t*>(m_inputStream.data()), inputWrapper.size());

  // validate checksum
  bool checksummed = inputWrapper.readChecksum();
  inputWrapper.deserialize();
  
  if (protocol && protocol->encryptionEnabled) {
    inputWrapper.decryptXTEA(protocol->xtea);
  }
  
  NetworkMessage msg;
  msg.write(inputWrapper.body(), inputWrapper.msgSize(), CanaryLib::MESSAGE_OPERATION_PEEK);

	if (!receivedFirst) {
		// First message received
		receivedFirst = true;

		if (!protocol) {
			// Game protocol has already been created at this point
			protocol = service_port->make_protocol(checksummed, msg, shared_from_this());
			if (!protocol) {
				close(FORCE_CLOSE);
				return;
			}
		} else {
			msg.skip(1);    // Skip protocol ID
		}
		protocol->onRecvFirstMessage(msg);
	} else if (checksummed) {
    protocol->onRecvMessage(msg);
    return;
	}

  resumeWork(true);
}

void Connection::resumeWork(bool checkTimer)
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);

	try {
    if (checkTimer) {
      readTimer.expires_from_now(boost::posix_time::seconds(CONNECTION_READ_TIMEOUT));
      readTimer.async_wait(std::bind(&Connection::handleTimeout, std::weak_ptr<Connection>(shared_from_this()), std::placeholders::_1));
    }

		// Wait to the next packet
		boost::asio::async_read(socket, boost::asio::buffer(m_inputStream.prepare(CanaryLib::WRAPPER_HEADER_SIZE)), std::bind(&Connection::parseHeader, shared_from_this(), std::placeholders::_1));
	} catch (boost::system::system_error& e) {
    spdlog::error("[Connection::resumeWork]: Network error during reading - {}", e.what());
		close(FORCE_CLOSE);
	}
}

void Connection::send(const Wrapper_ptr& wrapper)
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);
	bool noPendingWrite = messageQueue.empty();
	messageQueue.emplace_back(wrapper);
	if (noPendingWrite) {
		// Make asio thread handle xtea encryption instead of dispatcher
		try {
			#if BOOST_VERSION >= 106600
			boost::asio::post(socket.get_executor(), std::bind(&Connection::internalWorker, shared_from_this()));
			#else
			socket.get_io_service().post(std::bind(&Connection::internalWorker, shared_from_this()));
			#endif
		} catch (boost::system::system_error& e) {
			std::cout << "[Network error - Connection::send] " << e.what() << std::endl;
			messageQueue.clear();
			close(FORCE_CLOSE);
		}
	}
}

void Connection::internalWorker()
{
	std::unique_lock<std::recursive_mutex> lockClass(connectionLock);
	if (!messageQueue.empty()) {
		const Wrapper_ptr& wrapper = messageQueue.front();
		lockClass.unlock();
		protocol->onSendMessage(wrapper);
		lockClass.lock();
		internalSend(wrapper);
	}
}

void Connection::internalSend(const Wrapper_ptr& wrapper)
{
	try {
		writeTimer.expires_from_now(boost::posix_time::seconds(CONNECTION_WRITE_TIMEOUT));
		writeTimer.async_wait(std::bind(&Connection::handleTimeout, std::weak_ptr<Connection>(shared_from_this()), std::placeholders::_1));

		boost::asio::async_write(socket,
		                         boost::asio::buffer(wrapper->buffer(), wrapper->outputSize()),
		                         std::bind(&Connection::onWriteOperation, shared_from_this(), std::placeholders::_1));
	} catch (boost::system::system_error& e) {
		std::cout << "[Network error - Connection::internalSend] " << e.what() << std::endl;
		close(FORCE_CLOSE);
	}
}

uint32_t Connection::getIP()
{
	std::lock_guard<std::recursive_mutex> lockClass(connectionLock);

	// IP-address is expressed in network byte order
	boost::system::error_code error;
	const boost::asio::ip::tcp::endpoint endpoint = socket.remote_endpoint(error);
	if (error) {
		return 0;
	}

	return htonl(endpoint.address().to_v4().to_ulong());
}

void Connection::onWriteOperation(const boost::system::error_code& error)
{
	writeTimer.cancel();
	messageQueue.pop_front();

	if (error) {
		messageQueue.clear();
		close(FORCE_CLOSE);
		return;
	}

	internalWorker();
}

void Connection::handleTimeout(ConnectionWeak_ptr connectionWeak, const boost::system::error_code& error)
{
	if (error == boost::asio::error::operation_aborted) {
		//The timer has been manually cancelled
		return;
	}

	if (auto connection = connectionWeak.lock()) {
		connection->close(FORCE_CLOSE);
	}
}
