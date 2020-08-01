/**
 * Canary Lib - Canary Project a free 2D game platform
 * Copyright (C) 2020  Lucas Grossi <lucas.ggrossi@gmail.com>
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

#ifndef FS_FLATBUFFERS_WRAPPER_POOL_H_C06AAED85C7A43939F22D229297C0CC1
#define FS_FLATBUFFERS_WRAPPER_POOL_H_C06AAED85C7A43939F22D229297C0CC1

#include "networkmessage.h"
#include "connection.h"
#include "tools.h"

class Protocol;

class FlatbuffersWrapperPool
{
	public:
		// non-copyable
		FlatbuffersWrapperPool(const FlatbuffersWrapperPool&) = delete;
		FlatbuffersWrapperPool& operator=(const FlatbuffersWrapperPool&) = delete;

		static FlatbuffersWrapperPool& getInstance() {
			static FlatbuffersWrapperPool instance;
			return instance;
		}

		void sendAll();
		void scheduleSendAll();

		static Wrapper_ptr getOutputWrapper();

		void addProtocolToAutosend(Protocol_ptr protocol);
		void removeProtocolFromAutosend(const Protocol_ptr& protocol);
	private:
		FlatbuffersWrapperPool() = default;
		//NOTE: A vector is used here because this container is mostly read
		//and relatively rarely modified (only when a client connects/disconnects)
		std::vector<Protocol_ptr> bufferedProtocols;
};


#endif
