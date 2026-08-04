/****************************************************************************
 *
 *   Copyright (c) 2023 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/**
 * @file DatamanClient.cpp
 */

#include <dataman_client/DatamanClient.hpp>
#include <inttypes.h>
#include <drivers/drv_hrt.h>
#include <px4_platform_common/tasks.h>
#include <lib/perf/perf_counter.h>
#include <px4_platform_common/log.h>
#include <uORB/Publication.hpp>

DatamanClient::DatamanClient()
{
	_sync_perf = perf_alloc(PC_ELAPSED, "DatamanClient: sync");

	_dataman_request_pub.advertise();
	_dataman_response_sub = orb_subscribe(ORB_ID(dataman_response));

	if (_dataman_response_sub < 0) {
		PX4_ERR("Failed to subscribe (%i)", errno);

	} else {
		// make sure we don't get any stale response by doing an orb_copy
		dataman_response_s response{};
		orb_copy(ORB_ID(dataman_response), _dataman_response_sub, &response);

		_fds.fd = _dataman_response_sub;
		_fds.events = POLLIN;

		hrt_abstime timestamp = hrt_absolute_time();

		dataman_request_s request{};
		request.timestamp = timestamp;
		request.request_type = DM_GET_ID;
		request.client_id = CLIENT_ID_NOT_SET;
		
		/*
		//BOB - 20260410 - debug
		PX4_INFO("dm client ctor: GET_ID send this=%p task=%s ts=%llu type=%u item=%u index=%lu len=%lu client=%u",
         this,
         px4_get_taskname(),
         (unsigned long long)request.timestamp,
         (unsigned)request.request_type,
         (unsigned)request.item,
         (unsigned long)request.index,
         (unsigned long)request.data_length,
         (unsigned)request.client_id); 
		//EOB - 20260410 - debug
		*/
		
		bool success = syncHandler(request, response, timestamp, 1000_ms);

		if (success && (response.client_id > CLIENT_ID_NOT_SET)) {

			_client_id = response.client_id;
			
			/*
			//BOB - 20260410 - debug
			PX4_INFO("dm client ctor: this=%p assigned client_id=%u module=%s",
				this, _client_id, px4_get_taskname());
			//EOB - 20260410 - debug
			*/
		} else {
			PX4_ERR("Failed to get client ID!");
		}
	}
}

DatamanClient::~DatamanClient()
{
	perf_free(_sync_perf);

	if (_dataman_response_sub >= 0) {
		orb_unsubscribe(_dataman_response_sub);
	}
}

bool DatamanClient::syncHandler(const dataman_request_s &request, dataman_response_s &response,
				const hrt_abstime &start_time, hrt_abstime timeout)
{
	bool response_received = false;
	int32_t ret = 0;
	hrt_abstime time_elapsed = hrt_elapsed_time(&start_time);

	perf_begin(_sync_perf);
	
	/*
	//BOB - 20260410 - debug
	PX4_INFO("dm client: req pub this=%p task=%s req[c=%llu t=%llu i=%llu idx=%llu len=%llu ts=%llu]",
		 this,
		 px4_get_taskname(),
		 (unsigned long long)request.client_id,
		 (unsigned long long)request.request_type,
		 (unsigned long long)request.item,
		 (unsigned long long)request.index,
		 (unsigned long long)request.data_length,
		 (unsigned long long)request.timestamp);
	//EOB - 20260410 - debug
	*/
	
	_dataman_request_pub.publish(request);

	while (!response_received && (time_elapsed < timeout)) {

		// 짧게 poll 반복하면서 응답을 계속 drain
		const uint32_t timeout_ms = 100;
		ret = px4_poll(&_fds, 1, timeout_ms);

		if (ret < 0) {
			/*
			//BOB - 20260410 - debug
			PX4_ERR("dm client: px4_poll error this=%p task=%s ret=%ld req[c=%llu t=%llu i=%llu idx=%llu]",
				this,
				px4_get_taskname(),
				(long)ret,
				(unsigned long long)request.client_id,
				(unsigned long long)request.request_type,
				(unsigned long long)request.item,
				(unsigned long long)request.index);
			//EOB - 20260410 - debug
			*/
			break;

		} else if (ret == 0) {
			// timeout window 내에서는 재전송하지 않고 계속 기다림
			time_elapsed = hrt_elapsed_time(&start_time);
			continue;

		} else {
			if (!(_fds.revents & POLLIN)) {
				time_elapsed = hrt_elapsed_time(&start_time);
				continue;
			}

			bool updated = false;
			orb_check(_dataman_response_sub, &updated);

			// queue에 쌓인 응답을 가능한 만큼 모두 읽는다
			while (updated) {
				orb_copy(ORB_ID(dataman_response), _dataman_response_sub, &response);

				// 1) 현재 요청 시작 시각보다 오래된 응답은 버림
				if (response.timestamp < start_time) {
					/*
					//BOB - 20260410 - debug
					PX4_WARN("dm client: stale resp drop this=%p task=%s resp_ts=%llu start_ts=%llu got[c=%llu t=%llu i=%llu idx=%llu st=%llu]",
						 this,
						 px4_get_taskname(),
						 (unsigned long long)response.timestamp,
						 (unsigned long long)start_time,
						 (unsigned long long)response.client_id,
						 (unsigned long long)response.request_type,
						 (unsigned long long)response.item,
						 (unsigned long long)response.index,
						 (unsigned long long)response.status);
					//EOB - 20260410 - debug
					*/
					orb_check(_dataman_response_sub, &updated);
					continue;
				}

				/*
				//BOB - 20260410 - debug
				// 2) stale이 아닌 응답만 기존 로직으로 처리
				PX4_INFO("dm client: resp recv this=%p task=%s got[c=%llu t=%llu i=%llu idx=%llu st=%llu ts=%llu] expect[c=%llu t=%llu i=%llu idx=%llu]",
					 this,
					 px4_get_taskname(),
					 (unsigned long long)response.client_id,
					 (unsigned long long)response.request_type,
					 (unsigned long long)response.item,
					 (unsigned long long)response.index,
					 (unsigned long long)response.status,
					 (unsigned long long)response.timestamp,
					 (unsigned long long)request.client_id,
					 (unsigned long long)request.request_type,
					 (unsigned long long)request.item,
					 (unsigned long long)request.index);
				//EOB - 20260410 - debug
				*/
				
				if (response.client_id == request.client_id) {
					if ((response.request_type == request.request_type) &&
						(response.item == request.item) &&
						(response.index == request.index)) {
						
						/*
						//BOB - 20260410 - debug
						PX4_INFO("dm client: resp match this=%p task=%s c=%llu t=%llu i=%llu idx=%llu st=%llu",
							 this,
							 px4_get_taskname(),
							 (unsigned long long)response.client_id,
							 (unsigned long long)response.request_type,
							 (unsigned long long)response.item,
							 (unsigned long long)response.index,
							 (unsigned long long)response.status);
						//EOB - 20260410 - debug
						*/
						
						response_received = true;
						break;

					} else {
						/*
						//BOB - 20260410 - debug
						PX4_WARN("dm client: resp same-client mismatch this=%p task=%s got[c=%llu t=%llu i=%llu idx=%llu st=%llu] expect[c=%llu t=%llu i=%llu idx=%llu]",
							 this,
							 px4_get_taskname(),
							 (unsigned long long)response.client_id,
							 (unsigned long long)response.request_type,
							 (unsigned long long)response.item,
							 (unsigned long long)response.index,
							 (unsigned long long)response.status,
							 (unsigned long long)request.client_id,
							 (unsigned long long)request.request_type,
							 (unsigned long long)request.item,
							 (unsigned long long)request.index);
						//EOB - 20260410 - debug
						*/
					}

				} else if (request.client_id == CLIENT_ID_NOT_SET) {
					if (0 == memcmp(&(request.timestamp), &(response.data), sizeof(hrt_abstime))) {
						/*
						//BOB - 20260410 - debug
						PX4_INFO("dm client: resp match get-id this=%p task=%s assigned_client=%llu ts=%llu",
							 this,
							 px4_get_taskname(),
							 (unsigned long long)response.client_id,
							 (unsigned long long)response.timestamp);
						//EOB - 20260410 - debug
						*/
						response_received = true;
						break;

					} else {
						/*
						//BOB - 20260410 - debug
						PX4_WARN("dm client: resp get-id mismatch this=%p task=%s got[c=%llu ts=%llu]",
							 this,
							 px4_get_taskname(),
							 (unsigned long long)response.client_id,
							 (unsigned long long)response.timestamp);
						//EOB - 20260410 - debug
						*/
					}

				} else {
					/*
					//BOB - 20260410 - debug
					PX4_WARN("dm client: resp other-client this=%p task=%s got[c=%llu t=%llu i=%llu idx=%llu st=%llu] expect[c=%llu t=%llu i=%llu idx=%llu]",
						 this,
						 px4_get_taskname(),
						 (unsigned long long)response.client_id,
						 (unsigned long long)response.request_type,
						 (unsigned long long)response.item,
						 (unsigned long long)response.index,
						 (unsigned long long)response.status,
						 (unsigned long long)request.client_id,
						 (unsigned long long)request.request_type,
						 (unsigned long long)request.item,
						 (unsigned long long)request.index);
					//EOB - 20260410 - debug
					*/
				}

				orb_check(_dataman_response_sub, &updated);
			}

			if (response_received) {
				break;
			}
		}

		time_elapsed = hrt_elapsed_time(&start_time);
	}

	perf_end(_sync_perf);

	if (!response_received && ret >= 0) {
		PX4_ERR("dm client: timeout after %llu ms this=%p task=%s req[c=%llu t=%llu i=%llu idx=%llu len=%llu ts=%llu]",
			(unsigned long long)(timeout / 1000),
			this,
			px4_get_taskname(),
			(unsigned long long)request.client_id,
			(unsigned long long)request.request_type,
			(unsigned long long)request.item,
			(unsigned long long)request.index,
			(unsigned long long)request.data_length,
			(unsigned long long)request.timestamp);
	}

	return response_received;
}

bool DatamanClient::readSync(dm_item_t item, uint32_t index, uint8_t *buffer, uint32_t length, hrt_abstime timeout)
{
	if (_client_id == CLIENT_ID_NOT_SET) {
		return false;
	}

	if (length > g_per_item_size[item]) {
		PX4_ERR("Length  %" PRIu32 " can't fit in data size for item  %" PRIi8, length, static_cast<uint8_t>(item));
		return false;
	}

	hrt_abstime timestamp = hrt_absolute_time();

	dataman_request_s request{};
	request.timestamp = timestamp;
	request.index = index;
	request.data_length = length;
	request.client_id = _client_id;
	request.request_type = DM_READ;
	request.item = static_cast<uint8_t>(item);

	/*
	//BOB - 20260410 - debug
    PX4_INFO("dm client: READ send this=%p task=%s ts=%llu type=%u item=%u index=%lu len=%lu client=%u",
         this,
         px4_get_taskname(),
         (unsigned long long)request.timestamp,
         (unsigned)request.request_type,
         (unsigned)request.item,
         (unsigned long)request.index,
         (unsigned long)request.data_length,
         (unsigned)request.client_id);
	//EOB - 20260410 - debug
	*/
	
	dataman_response_s response{};
	bool success = syncHandler(request, response, timestamp, timeout);

	if (success) {

		if (response.status != dataman_response_s::STATUS_SUCCESS) {

			success = false;
			PX4_ERR("readSync failed! status=%" PRIu8 ", item=%" PRIu8 ", index=%" PRIu32 ", length=%" PRIu32,
				response.status, static_cast<uint8_t>(item), index, length);

		} else {
			memcpy(buffer, response.data, length);
		}
	}

	return success;
}

bool DatamanClient::writeSync(dm_item_t item, uint32_t index, uint8_t *buffer, uint32_t length, hrt_abstime timeout)
{
	if (_client_id == CLIENT_ID_NOT_SET) {
		return false;
	}

	if (length > g_per_item_size[item]) {
		PX4_ERR("Length  %" PRIu32 " can't fit in data size for item  %" PRIi8, length, static_cast<uint8_t>(item));
		return false;
	}

	hrt_abstime timestamp = hrt_absolute_time();

	dataman_request_s request{};
	request.timestamp = timestamp;
	request.index = index;
	request.data_length = length;
	request.client_id = _client_id;
	request.request_type = DM_WRITE;
	request.item = static_cast<uint8_t>(item);

	memcpy(request.data, buffer, length);
	
	/*
	//BOB - 20260410 - debug
	PX4_INFO("dm client: WRITE send this=%p task=%s ts=%llu type=%u item=%u index=%lu len=%lu client=%u",
         this,
         px4_get_taskname(),
         (unsigned long long)request.timestamp,
         (unsigned)request.request_type,
         (unsigned)request.item,
         (unsigned long)request.index,
         (unsigned long)request.data_length,
         (unsigned)request.client_id);
	//EOB - 20260410 - debug
	*/
	
	dataman_response_s response{};
	bool success = syncHandler(request, response, timestamp, timeout);

	if (success) {

		if (response.status != dataman_response_s::STATUS_SUCCESS) {

			success = false;
			PX4_ERR("writeSync failed! status=%" PRIu8 ", item=%" PRIu8 ", index=%" PRIu32 ", length=%" PRIu32,
				response.status, static_cast<uint8_t>(item), index, length);
		}
	}

	return success;
}

bool DatamanClient::clearSync(dm_item_t item, hrt_abstime timeout)
{
	if (_client_id == CLIENT_ID_NOT_SET) {
		return false;
	}

	hrt_abstime timestamp = hrt_absolute_time();

	dataman_request_s request{};
	request.timestamp = timestamp;
	request.client_id = _client_id;
	request.request_type = DM_CLEAR;
	request.item = static_cast<uint8_t>(item);
	
	/*
	//BOB - 20260410 - debug
    PX4_INFO("dm client: CLEAR send this=%p task=%s ts=%llu type=%u item=%u index=%lu len=%lu client=%u",
         this,
         px4_get_taskname(),
         (unsigned long long)request.timestamp,
         (unsigned)request.request_type,
         (unsigned)request.item,
         (unsigned long)request.index,
         (unsigned long)request.data_length,
         (unsigned)request.client_id);
	//EOB - 20260410 - debug
	*/
	
	dataman_response_s response{};
	bool success = syncHandler(request, response, timestamp, timeout);

	if (success) {

		if (response.status != dataman_response_s::STATUS_SUCCESS) {

			success = false;
			PX4_ERR("clearSync failed! status=%" PRIu8 ", item=%" PRIu8,
				response.status, static_cast<uint8_t>(item));
		}
	}

	return success;
}

bool DatamanClient::readAsync(dm_item_t item, uint32_t index, uint8_t *buffer, uint32_t length)
{
	if (_client_id == CLIENT_ID_NOT_SET) {
		return false;
	}

	if (length > g_per_item_size[item]) {
		PX4_ERR("Length  %" PRIu32 " can't fit in data size for item  %" PRIi8, length, static_cast<uint8_t>(item));
		return false;
	}

	bool success = false;

	if (_state == State::Idle) {

		hrt_abstime timestamp = hrt_absolute_time();

		dataman_request_s request;
		request.timestamp = timestamp;
		request.index = index;
		request.data_length = length;
		request.client_id = _client_id;
		request.request_type = DM_READ;
		request.item = static_cast<uint8_t>(item);

		_active_request.timestamp = timestamp;
		_active_request.request_type = DM_READ;
		_active_request.item = item;
		_active_request.index = index;
		_active_request.buffer = buffer;
		_active_request.length = length;

		_state = State::RequestSent;

		_dataman_request_pub.publish(request);

		success = true;
	}

	return success;
}

bool DatamanClient::writeAsync(dm_item_t item, uint32_t index, uint8_t *buffer, uint32_t length)
{
	if (_client_id == CLIENT_ID_NOT_SET) {
		return false;
	}

	if (length > g_per_item_size[item]) {
		PX4_ERR("Length  %" PRIu32 " can't fit in data size for item  %" PRIi8, length, static_cast<uint8_t>(item));
		return false;
	}

	bool success = false;

	if (_state == State::Idle) {

		hrt_abstime timestamp = hrt_absolute_time();

		dataman_request_s request;
		request.timestamp = timestamp;
		request.index = index;
		request.data_length = length;
		request.client_id = _client_id;
		request.request_type = DM_WRITE;
		request.item = static_cast<uint8_t>(item);

		memcpy(request.data, buffer, length);

		_active_request.timestamp = timestamp;
		_active_request.request_type = DM_WRITE;
		_active_request.item = item;
		_active_request.index = index;
		_active_request.buffer = buffer;
		_active_request.length = length;

		_state = State::RequestSent;

		_dataman_request_pub.publish(request);

		success = true;
	}

	return success;
}

bool DatamanClient::clearAsync(dm_item_t item)
{
	if (_client_id == CLIENT_ID_NOT_SET) {
		return false;
	}

	bool success = false;

	if (_state == State::Idle) {

		hrt_abstime timestamp = hrt_absolute_time();

		dataman_request_s request;
		request.timestamp = timestamp;
		request.client_id = _client_id;
		request.request_type = DM_CLEAR;
		request.item = static_cast<uint8_t>(item);
		request.index = 0;

		_active_request.timestamp = timestamp;
		_active_request.request_type = DM_CLEAR;
		_active_request.item = item;
		_active_request.index = request.index;
		_state = State::RequestSent;

		_dataman_request_pub.publish(request);

		success = true;
	}

	return success;
}

void DatamanClient::update()
{
	if (_state == State::RequestSent) {

		bool updated = false;
		orb_check(_dataman_response_sub, &updated);

		dataman_response_s response;

		if (updated) {
			orb_copy(ORB_ID(dataman_response), _dataman_response_sub, &response);

			if ((response.client_id == _client_id) &&
			    (response.request_type == _active_request.request_type) &&
			    (response.item == _active_request.item) &&
			    (response.index == _active_request.index)) {

				if (response.request_type == DM_READ) {
					memcpy(_active_request.buffer, response.data, _active_request.length);
				}

				_response_status = response.status;

				if (_response_status != dataman_response_s::STATUS_SUCCESS) {

					PX4_ERR("Async request type %" PRIu8 " failed! status=%" PRIu8 " item=%" PRIu8 " index=%" PRIu32,
						response.request_type, response.status, static_cast<uint8_t>(_active_request.item), _active_request.index);
				}

				_state = State::ResponseReceived;
			}
		}

		if (_state == State::RequestSent) {

			/* Retry the request if there is no answer */
			if (((_active_request.request_type != DM_CLEAR) && (hrt_elapsed_time(&_active_request.timestamp) > 100_ms)) ||
			    (hrt_elapsed_time(&_active_request.timestamp) > 1000_ms)
			   ) {

				hrt_abstime timestamp = hrt_absolute_time();

				_active_request.timestamp = timestamp;

				dataman_request_s request;
				request.timestamp = timestamp;
				request.index = _active_request.index;
				request.data_length = _active_request.length;
				request.client_id = _client_id;
				request.request_type = static_cast<uint8_t>(_active_request.request_type);
				request.item = static_cast<uint8_t>(_active_request.item);

				if (_active_request.request_type == DM_WRITE) {
					memcpy(request.data, _active_request.buffer, _active_request.length);
				}

				_dataman_request_pub.publish(request);

				_state = State::RequestSent;
			}
		}
	}
}

bool DatamanClient::lastOperationCompleted(bool &success)
{
	bool completed = false;
	success = false;

	if (_state == State::ResponseReceived) {

		if (_response_status == dataman_response_s::STATUS_SUCCESS) {
			success = true;
		}

		_state = State::Idle;
		completed = true;
	}

	return completed;
}

void DatamanClient::abortCurrentOperation()
{
	_state = State::Idle;
}

DatamanCache::DatamanCache(const char *cache_miss_perf_counter_name, uint32_t num_items)
	: _cache_miss_perf(perf_alloc(PC_COUNT, cache_miss_perf_counter_name))
{
	_items = new Item[num_items] {};

	if (_items != nullptr) {
		_num_items = num_items;

	} else {
		PX4_ERR("alloc failed");
	}
}

DatamanCache::~DatamanCache()
{
	delete[] _items;
	perf_free(_cache_miss_perf);
}

void DatamanCache::resize(uint32_t num_items)
{
	Item *new_items = new Item[num_items] {};

	if (new_items != nullptr) {
		uint32_t num_min = num_items < _num_items ? num_items : _num_items;

		for (uint32_t i = 0; i < num_min; ++i) {
			new_items[i] = _items[i];
		}

		delete[] _items;
		_items = new_items;
		_num_items = num_items;

	} else {
		PX4_ERR("alloc failed");
	}
}

bool DatamanCache::load(dm_item_t item, uint32_t index)
{
	if (!_items) {
		return false;
	}

	bool success = false;
	bool duplicate = false;

	//Prevent duplicates
	for (uint32_t i = 0; i < _num_items; ++i) {

		if (_items[i].cache_state != State::Idle &&
		    _items[i].cache_state != State::Error &&
		    _items[i].response.item == item &&
		    _items[i].response.index == index) {
			duplicate = true;
			break;
		}
	}

	if (!duplicate && (_item_counter < _num_items)) {

		_items[_load_index].cache_state = State::RequestPrepared;
		_items[_load_index].response.item = item;
		_items[_load_index].response.index = index;

		_load_index = (_load_index + 1) % _num_items;

		++_item_counter;

		success = true;
	}

	return success;
}

bool DatamanCache::loadWait(dm_item_t item, uint32_t index, uint8_t *buffer, uint32_t length, hrt_abstime timeout)
{
	if (length > g_per_item_size[item]) {
		PX4_ERR("Length %" PRIu32 " can't fit in data size for item %" PRIi8, length, static_cast<uint8_t>(item));
		return false;
	}

	if (!_items) {
		return false;
	}

	bool success = false;
	bool item_found = false;

	for (uint32_t i = 0; i < _num_items; ++i) {
		if ((_items[i].response.item == item) &&
		    (_items[i].response.index == index)) {
			item_found = true;

			if (_items[i].cache_state == State::ResponseReceived) {
				memcpy(buffer, _items[i].response.data, length);
				success = true;
				break;
			}
		}
	}

	if (!success && (timeout > 0)) {
		perf_count(_cache_miss_perf);
		success = _client.readSync(item, index, buffer, length, timeout);

		// Cache the item if not found already (it could be in the process of being loaded)
		if (success && !item_found && _item_counter < _num_items) {
			_items[_load_index].cache_state = State::ResponseReceived;
			_items[_load_index].response.item = item;
			_items[_load_index].response.index = index;
			memcpy(_items[_load_index].response.data, buffer, length);

			_load_index = (_load_index + 1) % _num_items;

			++_item_counter; // Still increase the counter here
		}
	}

	return success;
}

bool DatamanCache::writeWait(dm_item_t item, uint32_t index, uint8_t *buffer, uint32_t length, hrt_abstime timeout)
{
	if (length > g_per_item_size[item]) {
		PX4_ERR("Length  %" PRIu32 " can't fit in data size for item  %" PRIi8, length, static_cast<uint8_t>(item));
		return false;
	}

	bool success = _client.writeSync(item, index, buffer, length, timeout);

	if (success && _items) {
		for (uint32_t i = 0; i < _num_items; ++i) {
			if ((_items[i].response.item == item) &&
			    (_items[i].response.index == index) &&
			    ((_items[i].cache_state == State::ResponseReceived) ||
			     (_items[i].cache_state == State::RequestPrepared))) {

				memcpy(_items[i].response.data, buffer, length);
				_items[i].cache_state = State::ResponseReceived;
				break;
			}
		}
	}

	return success;
}

void DatamanCache::update()
{
	/*
	//BOB - 20260410 - debug
	return;
	//EOB - 20260410 - debug
	*/
	
	if (_item_counter > 0) {

		_client.update();

		bool success = false;
		bool response_success = false;

		switch (_items[_update_index].cache_state) {
		case State::Idle:
			break;

		case State::ResponseReceived:
			// Skip it
			changeUpdateIndex();
			break;

		case State::RequestPrepared:

			success = _client.readAsync(static_cast<dm_item_t>(_items[_update_index].response.item),
						    _items[_update_index].response.index,
						    _items[_update_index].response.data,
						    g_per_item_size[_items[_update_index].response.item]);

			if (success) {
				_items[_update_index].cache_state = State::RequestSent;

			} else {
				_items[_update_index].cache_state = State::Error;
			}

			break;

		case State::RequestSent:

			if (_client.lastOperationCompleted(response_success)) {

				if (response_success) {

					_items[_update_index].cache_state = State::ResponseReceived;
					changeUpdateIndex();

				} else {
					_items[_update_index].cache_state = State::Error;
				}
			}

			break;

		case State::Error:
			// Handled below
			break;
		}

		if (_items[_update_index].cache_state == State::Error) {
			PX4_ERR("Caching: item %" PRIu8 ", index %" PRIu32", status %" PRIu8,
				_items[_update_index].response.item, _items[_update_index].response.index,
				_items[_update_index].response.status);

			changeUpdateIndex();
		}

	}
}

void DatamanCache::invalidate()
{
	for (uint32_t i = 0; i < _num_items; ++i) {
		_items[i].cache_state = State::Idle;
	}

	_update_index = 0;
	_item_counter = 0;
	_load_index = 0;
	_client.abortCurrentOperation();
}

inline void DatamanCache::changeUpdateIndex()
{
	_update_index = (_update_index + 1) % _num_items;

	if (_item_counter > 0) {
		--_item_counter;
	}
}
