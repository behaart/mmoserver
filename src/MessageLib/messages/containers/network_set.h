// This file is part of SWGANH which is released under the MIT license.
// See file LICENSE or go to http://swganh.com/LICENSE
#pragma once

#include <set>
#include <queue>
#include <functional>

#include "MessageLib/messages/baselines_message.h"
#include "MessageLib/messages/deltas_message.h"

#include "default_serializer.h"

namespace swganh
{
namespace containers
{

template<typename T, typename Serializer=DefaultSerializer<T>>
		/*
		*@brief NetworkSET is the helper Class to deserialize a Set to baselines and deltas
		*it is used for datastructures where the client does NOT expect an index of the elements position in the delta
		*
		*/
        class NetworkSet
        {
            public:
            typedef typename std::set<T>::const_iterator const_iterator;
            typedef typename std::set<T>::iterator iterator;

            NetworkSet()
                : update_counter_(0)
{
}

void remove(const T& data, bool update=true)
{
    remove(data_.find(data));
}

void remove(iterator itr, bool update=true)
{
    if(itr != data_.end())
    {
        if(update)
        {
            const T& data = *itr;
            deltas_.push([=] (swganh::messages::DeltasMessage& message)
            {
				message.data.write<uint8_t>(0);
                serialize_it(message.data, (T)data);
            });
        }
        data_.erase(itr);
    }
}

void serialize_it(swganh::ByteBuffer& data, const T& t)
{
	Serializer::SerializeDelta(data, (T)t);
}

void serialize_base(swganh::ByteBuffer& data, const T& t)
{
	Serializer::SerializeBaseline(data, (T)t);
}

void add(const T& data, bool update=true)
{
    auto pair = data_.insert(data);
    if(pair.second)
    {
        if(update)
        {
            deltas_.push([=] (swganh::messages::DeltasMessage& message)
            {
                message.data.write<uint8_t>(1);
                serialize_it(message.data, *pair.first);
            });
        }
    }
}

void clear(bool update=true)
{
    data_.clear();
    if(update)
    {
        deltas_.push([=] (swganh::messages::DeltasMessage& message)
        {
            message.data.write<uint8_t>(2);
        });
    }
}

bool contains(const T& data)
{
    return data_.find(data) != data_.end();
}

std::set<T> data()
{
    return data_;
}

std::set<T>& raw()
{
    return data_;
}

iterator begin()
{
    return data_.begin();
}

iterator end()
{
    return data_.end();
}

bool Serialize(swganh::messages::BaseSwgMessage* message)
{
    if(message->Opcode() == swganh::messages::BaselinesMessage::opcode)
    {
        Serialize(*((swganh::messages::BaselinesMessage*)message));
		return true;
    }
    else if(message->Opcode() == swganh::messages::DeltasMessage::opcode)
    {
		if(deltas_.size())	{
			Serialize(*((swganh::messages::DeltasMessage*)message));
			return true;
		}
		return false;
    }
	return false;
}

void Serialize(swganh::messages::BaselinesMessage& message)
{
    message.data.write<uint32_t>(data_.size());
    message.data.write<uint32_t>(0);
	update_counter_ += data_.size();
    auto& item = data_.begin();
	for(item = data_.begin(); item != data_.end(); item ++)
    {
        serialize_base(message.data, *item);
    }
}

void Serialize(swganh::messages::DeltasMessage& message)
{
    message.data.write<uint32_t>(deltas_.size());
    message.data.write<uint32_t>(update_counter_);
	update_counter_ += 1;

    while(!deltas_.empty())
    {
        deltas_.front()(message);
        deltas_.pop();
    }
}

private:
std::set<T> data_;

uint32_t update_counter_;
std::queue<std::function<void(swganh::messages::DeltasMessage&)>> deltas_;

/*
*most lists in the protocol use 0 for delete and 1 for add
*at least skillmods use 0 for add and 1 for remove!!!!! that is what the network_map is for!!!
*/
enum delta_flag {
	_remove = 0,
	_add,
	_clear
};
        };

}
}
